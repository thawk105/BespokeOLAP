# TPC-H Q1 — Storage Plan

## 1. Tables Touched

Only `lineitem`. Q1 is a single-table scan + group-by + aggregation.

## 2. Columns Referenced

From `reference/q1.sql`:

| Column            | Use                       | Source type      |
|-------------------|---------------------------|------------------|
| `l_shipdate`      | WHERE predicate           | DATE             |
| `l_returnflag`    | GROUP BY key              | VARCHAR (1 char) |
| `l_linestatus`    | GROUP BY key              | VARCHAR (1 char) |
| `l_quantity`      | SUM, AVG                  | DECIMAL(15,2)    |
| `l_extendedprice` | SUM, AVG (and in 2 exprs) | DECIMAL(15,2)    |
| `l_discount`      | SUM, AVG (and in 2 exprs) | DECIMAL(15,2)    |
| `l_tax`           | factor in `sum_charge`    | DECIMAL(15,2)    |

All other lineitem columns are dropped at load time.

## 3. Cardinality / Selectivity (from DuckDB SUMMARIZE)

- `lineitem` rows: 6,001,215 (SF=1)
- After `l_shipdate <= DATE '1998-09-02'`: **5,916,591 rows** (~98.6% pass)
- `l_returnflag` distinct: 3 (`A`, `N`, `R`)
- `l_linestatus` distinct: 2 (`F`, `O`)
- Observed groups (post-filter): 4 — `(A,F)`, `(N,F)`, `(N,O)`, `(R,F)`
- `l_quantity` range: 1.00..50.00, only 57 distinct values
- `l_discount` range: 0.00..0.10, 12 distinct values
- `l_tax` range: 0.00..0.08, 10 distinct values

## 4. Load-Time Pre-Filter (recommended)

Push `l_shipdate <= 1998-09-02` into the parquet loader:

- Decode `l_shipdate` first, build a survivor bitmap or compact index list, then materialize the other six columns only for surviving rows.
- After materialization, **discard `l_shipdate` entirely** — it is not needed for the aggregation.
- Predicate is computed once; main aggregation loop never touches a date or branches on the predicate.
- Working set after filter ~5.92M rows is the operative size for sizing buffers below.

Even though selectivity is ~98.6% (filter prunes little), the win is:
1. removing the date column from the hot loop (saves 24 MB of int32 at 6M rows),
2. removing one comparison and one branch per row in the aggregation kernel.

## 5. Physical Layout (Struct-of-Arrays, columnar)

All arrays are 64-byte aligned, length = N (post-filter row count, ~5.92M).
Stored contiguously in this declaration / allocation order so the hot loop walks them in lockstep (hardware prefetcher friendly):

```
struct LineitemQ1 {
    uint64_t  n;                 // surviving rows
    uint8_t*  group_key;         // packed (returnflag, linestatus) -> 0..G-1, G<=6
    int64_t*  quantity;          // scaled int (scale = 100)
    int64_t*  extendedprice;     // scaled int (scale = 100)
    int64_t*  discount;          // scaled int (scale = 100)
    int64_t*  tax;               // scaled int (scale = 100)
};
```

### Column representations

| Column            | In-memory type | Encoding                                           | Bytes/row |
|-------------------|----------------|----------------------------------------------------|-----------|
| group_key         | `uint8_t`      | direct-encoded composite of returnflag, linestatus | 1         |
| l_quantity        | `int64_t`      | DECIMAL(15,2) -> int64 scaled x100                 | 8         |
| l_extendedprice   | `int64_t`      | DECIMAL(15,2) -> int64 scaled x100                 | 8         |
| l_discount        | `int64_t`      | DECIMAL(15,2) -> int64 scaled x100                 | 8         |
| l_tax             | `int64_t`      | DECIMAL(15,2) -> int64 scaled x100                 | 8         |

Total hot footprint ~ 33 bytes/row * 5.92M ~ **195 MB**.

Notes on choices:

- **Scaled int64 (scale 100)** preserves exact decimal semantics, matches DuckDB output to the cent, and lets the kernel do plain integer adds/mults. No floating-point rounding drift. `l_quantity` is always integral cents (X.00) but keeping it int64-scaled keeps the kernel uniform.
- We do **not** use `int32` for `l_quantity` even though 50*100 fits in int16, because the sum runs into the 1e8 range at SF=1 (1478493 * 5000 ~ 7.4e9) which still fits int64 trivially; uniform width helps SIMD later.
- Could compress `l_discount`, `l_tax`, `l_quantity` further (dict / 8-bit) since they have <=57 distinct values, but the extra indirection per row would hurt the hot aggregation loop. Keep them raw int64. (Revisit in optimization phase if memory bandwidth turns out to be the bottleneck.)

### Group-key packing (no hash table)

Map the two 1-byte chars to a dense 8-bit key at load time:

```
returnflag_idx: 'A'->0, 'N'->1, 'R'->2          (2 bits)
linestatus_idx: 'F'->0, 'O'->1                  (1 bit)
group_key     = returnflag_idx * 2 + linestatus_idx   // range 0..5, only 4 used
```

- Lookup is a 2-entry / 3-entry switch (or 256-byte LUT keyed by ASCII char), evaluated once per row at load time.
- The aggregation kernel then indexes accumulators directly by `group_key` (no hashing, no probing, no comparisons). 6 slots is a constant; the unused (A,O) and (R,O) slots simply stay zero and are filtered out at emit time.
- 8-bit key is overkill for 4 groups but is the natural addressable unit; storage cost is 5.92 MB.

### Column order rationale

Order matches access order in the aggregation expression list:

```
group_key                                        // group dispatch
quantity                                         // sum_qty, avg_qty
extendedprice                                    // sum_base_price, avg_price, factor in disc/charge
discount                                         // avg_disc, factor in disc/charge
tax                                              // factor in charge only
```

Walking all five arrays in lockstep with sequential indices is the canonical case for hardware prefetchers and lets the compiler keep the inner loop branch-free.

## 6. Sort Order

**Unsorted.** The query has no range predicate on the surviving data and the group-by uses only 4 small groups; pre-sorting buys nothing for the aggregation and would cost a load-time sort.

The final result is `ORDER BY l_returnflag, l_linestatus` over **4 rows** — trivial in-place sort at emit time.

## 7. Auxiliary Structures

None.

- No hash index needed (group cardinality is 4; direct addressing wins).
- No zone maps needed (no range predicates after the load-time filter).
- No dictionaries persisted (group-key dict has 2 tiny LUTs hardcoded at load time; output emit reverses 0..3 back to (returnflag, linestatus) char pairs).

## 8. Accumulator Layout (for the query kernel)

Six-slot SoA accumulator (one slot per possible `group_key`, 0..5):

```
struct Accumulators {
    int64_t  sum_qty[6];           // sum of scaled int64 -> scale 100
    int64_t  sum_base_price[6];    // sum of scaled int64 -> scale 100
    __int128 sum_disc_price[6];    // sum( ep * (100 - disc) ) -> scale 10_000
    __int128 sum_charge[6];        // sum( ep * (100 - disc) * (100 + tax) ) -> scale 1_000_000
    int64_t  sum_disc[6];          // sum of scaled int64 -> scale 100  (for avg_disc)
    uint64_t count[6];             // count_order, also denominator for avgs
};
```

### Overflow analysis at SF=1 (verified against DuckDB)

- Largest single-row product `ep * qty * 100` observed: 5.247475e8 -> fits int64 with ~10 orders of magnitude headroom.
- Observed `sum_charge` total: ~2.236e11 in decimal(38,6) (i.e. ~2.236e17 as int with scale 1e6). int64 max is ~9.22e18 — fits with ~40x headroom.
- Per-group `sum_disc_price` (scale 1e4) worst case ~ 1.478e6 rows * 5.247e8 ~ 7.76e14 -> int64 safe.
- Per-group `sum_charge` (scale 1e6) worst case ~ 1.478e6 rows * 5.247e10 ~ 7.76e16 -> int64 safe.

int64 is sufficient at SF=1. **`__int128` is chosen for `sum_disc_price` and `sum_charge` defensively** because (a) costs ~zero perf vs int64 on aarch64 for accumulation-only, (b) the storage plan should not silently break at SF=10 / SF=100. If the optimization phase shows `__int128` is a real cost, drop to int64 with a static assert on SF.

### Emit phase

1. Iterate `group_key` slots 0..5; skip those with `count==0`.
2. Reverse map slot -> (returnflag char, linestatus char).
3. Compute averages once at the end (single division per group per agg). No division inside the hot loop.
4. Apply decimal scale corrections to taste:
   - `sum_qty` printed as `value / 100` with 2 decimals
   - `sum_base_price` likewise
   - `sum_disc_price` printed with scale 1e4 -> divide by 10000 for 2-decimal output but keep 4-decimal precision to match DuckDB Q1 reference
   - `sum_charge` printed with scale 1e6 similarly
   - `avg_*` = sum / count, applying the column's scale
5. Sort the (<=4) emitted rows by `(l_returnflag, l_linestatus)`.

## 9. Summary of Decisions and Why

| Decision                                          | Driver                                                                       |
|---------------------------------------------------|------------------------------------------------------------------------------|
| Load only 7 source columns, then drop shipdate    | Q1 references exactly those, shipdate dies with the predicate                |
| Columnar SoA, 5 arrays of length N                | Inner loop is a pure streaming scan over fixed-width primitives              |
| Push WHERE predicate into loader                  | Removes a column and a branch from the hot loop                              |
| Scaled int64 (x100) for decimals                  | Exact decimal arithmetic, integer ALU, SIMD-friendly                         |
| Direct-addressed 6-slot accumulator (no hash)     | Group cardinality is 4 — known from SUMMARIZE                                |
| Pre-packed `group_key` uint8 at load              | Pay char->key cost once at load, hot loop is a plain array index             |
| No sort, no zone map, no index                    | No range predicate on the materialized data; group-by is direct-addressed    |
| int128 for `sum_disc_price` and `sum_charge`      | Headroom for higher SFs at negligible cost on a 4-slot accumulator           |
