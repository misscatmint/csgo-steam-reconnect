# csgo-steam-reconnect

A Valve server plugin for Linux CS:GO dedicated servers that gets the server
back onto Steam when steamclient gives up reconnecting, without a restart.

## The problem

Rarely, a server that loses its connection to Steam during Steam's maintenance
never reconnects. Players can still join, but Steam never authenticates them,
so anything that relies on their SteamID stops working until the server is
restarted. It happens because of a bug in the Steam library the server ships
with: if a reconnect attempt lands at just the wrong moment during maintenance,
it gives up for good.

## What it does

If the server has been off Steam for 2 minutes, the plugin logs it back in, and
keeps retrying with growing delays until it's back.

Install it like any server plugin. To fix a server that's already stuck, load it
with `plugin_load addons/csgo-steam-reconnect`.

## Limitations

- Only servers that log in to Steam anonymously are supported. Servers with a
  login token (`sv_setsteamaccount`, a GSLT) aren't.
- Only the engine's own Steam session is covered, not other sessions in the
  same process, like csgo-multi-appid's validator.
- Nothing happens while the server hibernates. Set `sv_hibernate_when_empty 0`.

## Building

```sh
make                  # needs g++ with 32-bit support (g++-multilib)
make CXX=clang++      # or clang
tools/dist.sh [tag]   # release zip in dist/
```
