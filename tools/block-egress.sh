#!/bin/bash
# Cuts a test container off from the internet until interrupted or for a set
# number of seconds, then removes the block. Used to make srcds lose its Steam
# connection on demand. That reproduces an ordinary outage, not the stuck state
# csgo-steam-reconnect fixes. Run as root on the Docker host.

set -Eeuo pipefail

table=csr_block_egress

usage() {
    cat >&2 <<EOF
usage: $0 <container-ip> [seconds]

Blocks all outgoing IPv4 traffic from <container-ip> until Ctrl-C, or for
[seconds]. Make sure it's the test container's IP. If it belongs to another
container, that one gets cut off instead.
Find the IP with:
  docker inspect -f '{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}' <container>
EOF
    exit 2
}

[[ $# -eq 1 || $# -eq 2 ]] || usage
ip=$1
seconds=${2:-}

if [[ ! $ip =~ ^([0-9]{1,3})\.([0-9]{1,3})\.([0-9]{1,3})\.([0-9]{1,3})$ ]]; then
    echo "not an IPv4 address: $ip" >&2
    exit 2
fi
for octet in "${BASH_REMATCH[@]:1}"; do
    if (( 10#$octet > 255 )); then
        echo "not an IPv4 address: $ip" >&2
        exit 2
    fi
done
if [[ -n $seconds && ! $seconds =~ ^[1-9][0-9]*$ ]]; then
    echo "seconds must be a positive whole number: $seconds" >&2
    exit 2
fi
if [[ $EUID -ne 0 ]]; then
    echo "run as root, nft needs it" >&2
    exit 1
fi

# A leftover table means an earlier run didn't clean up, or something else
# owns the name. Deleting it here could remove rules this run didn't create.
if nft list table inet "$table" >/dev/null 2>&1; then
    echo "table inet $table already exists. Remove it with: nft delete table inet $table" >&2
    exit 1
fi

cleanup() {
    if [[ -n ${sleeper:-} ]]; then
        kill "$sleeper" 2>/dev/null || true
    fi
    if nft list table inet "$table" >/dev/null 2>&1; then
        nft delete table inet "$table"
        echo "unblocked $ip"
    fi
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

# Its own table so one delete removes exactly what was added. A reject in any
# table is final, whatever Docker's own rules accept. TCP gets a reset so open
# connections drop immediately instead of waiting for a timeout.
nft -f - <<EOF
table inet $table {
    chain forward {
        type filter hook forward priority -10; policy accept;
        ip saddr $ip meta l4proto tcp reject with tcp reset
        ip saddr $ip reject
    }
}
EOF

if [[ -n $seconds ]]; then
    echo "blocked $ip for $seconds seconds"
else
    echo "blocked $ip, press Ctrl-C to unblock"
    seconds=infinity
fi
# In the background so a plain kill still runs the traps. Bash defers traps
# until a foreground child exits, which sleep infinity never does.
sleep "$seconds" &
sleeper=$!
wait "$sleeper"
