FROM mcr.microsoft.com/devcontainers/cpp:1-ubuntu-24.04

# Apache Arrow / Parquet
RUN apt-get update && apt-get install -y -V \
        ca-certificates lsb-release wget unzip \
    && wget https://packages.apache.org/artifactory/arrow/$(lsb_release --id --short | tr 'A-Z' 'a-z')/apache-arrow-apt-source-latest-$(lsb_release --codename --short).deb \
    && apt-get install -y -V ./apache-arrow-apt-source-latest-$(lsb_release --codename --short).deb \
    && apt-get update \
    && apt-get install -y -V libarrow-dev libparquet-dev parquet-tools \
    && rm apache-arrow-apt-source-latest-$(lsb_release --codename --short).deb \
    && rm -rf /var/lib/apt/lists/*

# DuckDB CLI (arm64 / amd64 両対応)
RUN ARCH=$(dpkg --print-architecture) \
    && if [ "$ARCH" = "amd64" ]; then DUCKDB_ARCH="amd64"; else DUCKDB_ARCH="arm64"; fi \
    && curl -L "https://github.com/duckdb/duckdb/releases/latest/download/duckdb_cli-linux-${DUCKDB_ARCH}.zip" -o /tmp/duckdb.zip \
    && unzip /tmp/duckdb.zip -d /usr/local/bin \
    && rm /tmp/duckdb.zip \
    && chmod +x /usr/local/bin/duckdb
