#!/bin/bash
# Builds a release from scratch into
# dist/csgo-steam-reconnect-v<version>-linux.zip, holding addons/ the way it
# goes into a server's csgo/ folder. Warnings and shellcheck findings fail the
# build. Given a tag, also fails unless it matches PLUGIN_VERSION.

set -Eeuo pipefail

if [[ $# -gt 1 ]]; then
    echo "usage: $0 [tag]" >&2
    exit 2
fi

cd "$(dirname "$0")/.."

version=$(sed -n 's/^#define PLUGIN_VERSION "\(.*\)"$/\1/p' src/plugin.cpp)
if [[ -z $version ]]; then
    echo "no PLUGIN_VERSION in src/plugin.cpp" >&2
    exit 1
fi
if [[ $# -eq 1 && $1 != "v$version" ]]; then
    echo "tag $1 doesn't match PLUGIN_VERSION $version" >&2
    exit 1
fi

make clean
make lint
make CXXFLAGS=-Werror

staging=build/dist
mkdir -p "$staging/addons" dist
cp build/csgo-steam-reconnect.so csgo-steam-reconnect.vdf "$staging/addons/"
zip=dist/csgo-steam-reconnect-v$version-linux.zip
(cd "$staging" && zip -qr "../../$zip" addons)
echo "$zip"
