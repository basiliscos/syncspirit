# syncspirit

sites: [github](https://github.com/basiliscos/syncspirit), [abf](https://github.com/basiliscos/syncspirit)

syncspirit is a continuous file synchronization program, which synchronizes files between devices.
It is build using C++ [rotor](github.com/basiliscos/cpp-rotor) actor framework. It implements
[BEP-protocol](https://docs.syncthing.net/specs/bep-v1.html) for files syncrhonization, or, 
simplistically speaking, it is [syncthing](https://syncthing.net)-compatible syncrhonization
program.


# status

[x] downloading files from peer devices (aka all folders are receive only)
[x] [global peer discovery](https://docs.syncthing.net/specs/globaldisco-v3.html)
[x] [local (LAN) peer discovery](https://docs.syncthing.net/specs/localdisco-v4.html)
[x] upnp & nat passthough
[x] certificates generation


# missing features

This list is probably incomplete, here are the most important changes

[ ] relay transport
[ ] full-powered files synchronization (aka send and receive)
[ ] conflict resolution
[ ] ingoring files
[ ] [QUIC transport](https://en.wikipedia.org/wiki/QUIC)
[ ] introducer support
[ ] outgoing messages compression
[ ] [untrusted devices encryption](https://docs.syncthing.net/specs/untrusted.html)
[ ] ...

# design and ideas

[syncthing](https://syncthing.net) is implemented using [go](https://go.dev/) programming
language, which good fits for services. As the result, [syncthing](https://syncthing.net)
itself is written as web-service, which exposes REST-API for clients. So, yes, the end-user
software should also have a front-end, which is usually web-browser (or embeds web-browser),
which is written in different programming language (e.g. javascript or java).

I feel myself a bit uneasy with that design; maybe it's my personal nostalgia, but I like
the good old programs, where everything is "in memory" of one program. They are fast, 
ecological, secure, manageable, have lower CPU and memory pressures.

The [actor model](https://en.wikipedia.org/wiki/Actor_model), blurs the boundaries between
classical desktop and client-server application models. I think, 
[rotor](github.com/basiliscos/cpp-rotor) makes it possible to have (and embed) some 
"core" into multiple different user interfaces (GUIs). 

Currently, there syncspirit has only "daemon-ui", i.e. a simple non-interactive application,
which shows synchronization log, and the only possible just stop it. However, as soon
as the "core" will be complete, there are plans to develop multiple `syncspirit` UIs:
[wx-widgets](https://www.wxwidgets.org/), qt, gtk, may be native, may be even native mobile UIs...

Another major idea is scripting support: it should be possible to have exposes the "core" to lua 
scripts, and have some user-defined actions like synchonizing files with external folders including
flash-sticks, user-defined files ordering and filtering for synchronization, may be even selective
sync, like in [resilio](https://www.resilio.com/). This, however, still needs to be researched,
after the core completion.

# UI

- [daemon](docs/daemon.md)
- [wx-widgets](https://www.wxwidgets.org/) (planned)
- ...

# plaforms

- linux
- windows
- (may be) *nix
- (may be) mac os x
