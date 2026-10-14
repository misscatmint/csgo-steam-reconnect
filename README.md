# csgo-steam-reconnect

A Valve server plugin for Linux CS:GO dedicated servers (the legacy build) that
gets the server back onto Steam when steamclient gives up reconnecting, without
restarting the server.

## The problem

During Steam maintenance, a server can lose its Steam connection and never get
it back. The console shows

```
Connection to Steam servers lost.  (Result = 3)
src/clientdll/cminterface.cpp (3329) : m_VecAddressWebSocketCMs.Count()
```

and never `Connection to Steam servers successful.` afterwards.

That's a bug in the `steamclient.so` that ships with the CS:GO server. When
Steam's directory briefly answers with no WebSocket servers, steamclient's
connect attempt gives up without scheduling another one, and nothing ever
retries.

While the server is in that state, players can still join, but they never
authenticate. The engine never prints `STEAM USERID validated`, SourceMod never
calls `OnClientAuthorized`, and anything that waits for it doesn't work for
them. For example, GOKZ never verifies them, so their runs aren't submitted.
Until now the only fix was a restart.

## What it does

Once a second the plugin checks whether the server is logged on to Steam. After
2 minutes logged off, it logs the server back on, the same call the engine makes
at startup. If that doesn't get it back, it tries again after 4 minutes, then 8,
then every 10.

A normal reconnect takes about a minute, so ordinary outages usually resolve
before the plugin does anything. It can't tell a stuck server from a long
outage, so in a long one it logs on again anyway, which is harmless.

It only acts on servers that are meant to be on Steam anonymously:

- **Loaded at startup** (through the `.vdf`), it arms once it has seen the
  server logged on.
- **Loaded with `plugin_load`** onto a server that's already off Steam, it arms
  immediately. So it can fix a server that's stuck right now, without a restart.

Console output looks like this:

```
csgo-steam-reconnect: v0.1.0 loaded, the server is logged on to Steam
csgo-steam-reconnect: armed for anonymous server [A:1:2165926917:51890]
...
csgo-steam-reconnect: not logged on to Steam, re-logging on in 120 seconds if it stays that way
csgo-steam-reconnect: re-logging on to Steam (attempt 1, off for 120 seconds)
csgo-steam-reconnect: back on Steam after 323 seconds, 1 re-logon(s) by this plugin
```

## Installing

Copy `csgo-steam-reconnect.so` and `csgo-steam-reconnect.vdf` into
`csgo/addons/` and restart the server.

To fix a server that's stuck right now, copy only the `.so` and run this in the
server console:

```
plugin_load addons/csgo-steam-reconnect
```

## Building

Needs an x86 Linux machine with a 32-bit capable `g++` or `clang++`
(`g++-multilib` on Debian and Ubuntu). Nothing else: the engine libraries the
plugin calls into are stood in for by stubs in `stubs/` at build time.

```sh
make                # build with g++
make CXX=clang++    # or with clang
```

The plugin ends up in `build/`. The version is `PLUGIN_VERSION` in
`src/plugin.cpp`.

## Releasing

`tools/dist.sh` builds a release from scratch, treating warnings as errors, and
packages it as `dist/csgo-steam-reconnect-v<version>-linux.zip`, with an
`addons/` folder to extract into the server's `csgo/` folder. Given a tag, as in
`tools/dist.sh v0.1.0`, it also checks that the tag matches `PLUGIN_VERSION`.

GitHub Actions runs the same script on every push and pull request, and
publishes the zip as a release when a `v*` tag is pushed.

## Compatibility

The plugin needs glibc 2.17, one version above the game's own 2.15, so it runs
on Steam Runtime 3 (sniper, glibc 2.31) and on any distro from the last decade.
The build checks this, and fails if a toolchain would produce something that
needs a newer glibc or extra libraries.

## Limitations

- **Servers with a login token (GSLT) aren't covered.** Logging one back on
  needs its token, which the plugin can't read. It logs that it isn't arming and
  then does nothing.
- **Only the engine's own Steam session is covered.** Other sessions in the same
  process, like csgo-multi-appid's validator, can get stuck the same way, and
  this plugin can't see them.
- **Nothing happens while the server hibernates.** The plugin checks from
  `GameFrame`, which the engine doesn't call while hibernating. A stuck,
  hibernating server gets fixed on the first frame after someone joins. Set
  `sv_hibernate_when_empty 0` to avoid that.
- **Never overwrite the `.so` while the server is running.** The engine keeps
  unloaded plugins in memory, so overwriting the file crashes the server on the
  next `plugin_load`, and a `plugin_load` after a `plugin_unload` gets the old
  copy anyway. Restart the server to update.

## Things to expect

- On a stuck server, the re-logon makes steamclient print another
  `cminterface.cpp` assert and possibly write a minidump. That's expected, and
  the server keeps running.
- During an ordinary outage, a re-logon restarts steamclient's own retries with
  shorter delays. That's harmless, and it's why later re-logons back off.
- "re-logon(s) by this plugin" counts the re-logons the plugin sent.
  steamclient might have recovered on its own anyway.
- Players who joined while the server was stuck should become authenticated
  once it's back, without reconnecting, because steamclient resends their
  pending tickets after logging on. That hasn't been confirmed on a real stuck
  server yet.

## Testing

`tools/block-egress.sh` cuts a test container off from the internet so the
server loses Steam on demand:

```sh
docker inspect -f '{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}' <container>
sudo tools/block-egress.sh <container-ip> 300
```

It adds its own nftables table and removes it when it finishes, is interrupted,
or its terminal closes. It only blocks IPv4, which is all a default Docker
network uses. Blocking traffic doesn't reproduce the stuck state itself, only an
ordinary outage. Get the IP right: blocking the wrong container cuts that one
off instead.
