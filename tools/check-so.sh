#!/bin/bash
# Fails if the plugin needs a newer glibc or more libraries than servers can be
# expected to have. Run by make after linking.

set -Eeuo pipefail

# CS:GO's own binaries need glibc 2.15. The plugin's monotonic clock
# (clock_gettime) needs 2.17, which every supported system has.
max_glibc=2.17
allowed=(libtier0.so libsteam_api.so libc.so.6)

so=$1

newest=$(objdump -T "$so" | grep -o 'GLIBC_[0-9.]*' | sed 's/^GLIBC_//' |
    sort -uV | tail -1)
higher=$(printf '%s\n' "$newest" "$max_glibc" | sort -V | tail -1)
if [[ $higher != "$max_glibc" ]]; then
    echo "$so needs glibc $newest, newer than $max_glibc" >&2
    exit 1
fi

needed=$(readelf -d "$so" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p')
for lib in $needed; do
    if [[ ! " ${allowed[*]} " =~ " $lib " ]]; then
        echo "$so depends on $lib, which isn't in the allowed list" >&2
        exit 1
    fi
done
