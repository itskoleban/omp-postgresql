#!/usr/bin/env bash
# Build only libpq with TLS from an unpacked official PostgreSQL 17+ source release.
set -euo pipefail
if [[ $# != 2 ]]; then
	echo "Usage: $0 /path/to/postgresql-source /absolute/install/prefix" >&2
	exit 2
fi
source_dir=$(cd "$1" && pwd)
mkdir -p "$2"
install_dir=$(cd "$2" && pwd)
for program in gcc make bison flex perl; do
	command -v "$program" >/dev/null
done
cd "$source_dir"
CC="gcc -m32" CFLAGS="-O2 -m32" LDFLAGS="-m32" ./configure \
	--prefix="$install_dir" \
	--without-readline \
	--without-zlib \
	--without-icu \
	--with-ssl=openssl \
	--without-lz4 \
	--without-zstd
make -C src/interfaces/libpq -j4
make -C src/interfaces/libpq install
install -m644 \
	src/include/postgres_ext.h \
	src/include/pg_config.h \
	src/include/pg_config_ext.h \
	"$install_dir/include/"
