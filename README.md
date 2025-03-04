# syncspirit

Sites:
[github](https://github.com/basiliscos/syncspirit),
[notabug](https://notabug.org/basiliscos/syncspirit)
[gitflic](https://gitflic.ru/project/basiliscos/syncspirit)

`syncspirit` is a continuous file synchronization program, which synchronizes files between devices.
It is built using the C++ [rotor](https://github.com/basiliscos/cpp-rotor) actor framework. It implements
the [BEP-protocol](https://docs.syncthing.net/specs/bep-v1.html) for files synchronization, or, 
simplistically speaking, it is a [syncthing](https://syncthing.net)-compatible synchronization
program at protocol level, which uses the [syncthing](https://syncthing.net) infrastructure (for global discovery
and relaying).

Despite being functional, `syncspirit` is much less feature-rich than [syncthing](https://syncthing.net)
and is still in heavy development


# front-ends (aka UIs)

![different UIs](docs/different-uis.gif)

- [fltk](https://www.fltk.org/)-based gui [app](docs/ui-fltk.md)

- headless [daemon](docs/ui-daemon.md)

All front-ends share the same configuration and database when running on same host.

# status

- [x] full-powered files synchronization (aka send and receive)

- [x] [global peer discovery](https://docs.syncthing.net/specs/globaldisco-v3.html)

- [x] [local (LAN) peer discovery](https://docs.syncthing.net/specs/localdisco-v4.html)

- [x] upnp & nat passthough

- [x] certificates generation

- [x] relay transport

- [x] conflict resolution

# missing features

This list is probably incomplete. Here are the most important changes:

- [ ] inotify support (aka realtime files watching)

- [ ] ignoring pattern files

- [ ] [QUIC transport](https://en.wikipedia.org/wiki/QUIC)

- [ ] introducer support

- [ ] outgoing messages compression

- [ ] [untrusted devices encryption](https://docs.syncthing.net/specs/untrusted.html)

- [ ] send only/receive only dirs

- [ ] ...

# run

(headless ui-daemon only, atm)

    syncspirit-daemon --log_level debug \
        --config_dir=/tmp/my_dir \
        --command add_peer:peer_label:KUEQE66-JJ7P6AD-BEHD4ZW-GPBNW6Q-Y4C3K4Y-X44WJWZ-DVPIDXS-UDRJMA7 \
        --command add_folder:label=my_label:id=nagkw-srrjz:path=/tmp/my_dir/data \
        --command share:folder=my_label:device=KUEQE66 \
        --command inactivate:120

the output should be like

[![asciicast](https://asciinema.org/a/474217.svg)](https://asciinema.org/a/474217)

i.e. it records some peer, adds a folder, then shares the folder with the peer device, connects to
the peer and downloads all files into `/tmp/my_dir/data` . The peer device currently can only be
[syncthing](https://syncthing.net). Then `syncspirit` either exits after 2 minutes of inactivity
or when you press `ctrl+c`. The output is successful, because I previously authorized this device
with the [syncthing](https://syncthing.net) web interface, and shared the folder with this device
(`syncspirit`).

I also assume some familiarity with [syncthing](https://syncthing.net), so you should understand
what's going on here.

For more details see [ui-daemon](docs/ui-daemon.md) docs and [configuration](docs/config.md) docs.

# design and ideas

[syncthing](https://syncthing.net) is implemented using [go](https://go.dev/) programming
language, which is a good fit for services. As a result, [syncthing](https://syncthing.net)
itself is written as a web-service, which exposes a REST-API for clients. So, yes, the end-user
software should also have a front-end, which is usually web-browser (or embeds web-browser),
which is written in different programming language (e.g. javascript or java).

I feel myself a bit uneasy with that design; maybe it's my personal nostalgia, but I like
the good old programs, where everything is "in memory" of one program. They are fast, 
ecological, secure, manageable, have lower CPU and memory pressures.

The [actor model](https://en.wikipedia.org/wiki/Actor_model), blurs the boundaries between
classical desktop and client-server application models. I think, 
[rotor](https://github.com/basiliscos/cpp-rotor) makes it possible to have (and embed) some 
"core" into multiple different user interfaces (GUIs). 

Currently, syncspirit has only a "daemon-ui", i.e. a simple non-interactive application,
which shows a synchronization log, and the only possibility is just to stop it. However, as soon
as the "core" is complete, there are plans to develop multiple `syncspirit` UIs:
[wx-widgets](https://www.wxwidgets.org/), qt, gtk, maybe native, maybe even native mobile UIs...

Another major idea is scripting support: it should be possible to expose the "core" to lua 
scripts, and have some user-defined actions like synchronizing files with external folders including
flash-sticks, user-defined files ordering and filtering for synchronization, maybe even selective
sync, like in [resilio](https://www.resilio.com/). This, however, still needs to be researched,
after the core completion.

# platforms

- linux
- windows
- mac os x (it is know that it can be build and run on that platform, but I don't know to make
distributions on it)

# changes

## 0.4.0 (22-Feb-2025)
 - [feature] new [fltk-fronted](docs/ui-fltk.md)
 - major app refactoring
 
## 0.3.3 (12-May-2024)
- [bugfix, win32] governor actor, fix parsing folder path

## 0.3.2 (11-May-2024)
- [bugfix] folder scan isn't triggered on startup

## 0.3.1 (23-Apr-2024)
- [feature] added `syncspirit` binary fow windows xp
- [build, docs] improved build documentation

## 0.3.0 (14-Apr-2024)
- [feature] implemented complete files synchronization
- [feature] added local files watcher and updates streamer
- [build] switched from git submodules to [conan2](https://conan.io)
- [win32] better platform support


## 0.2.0 (22-May-2022)
- [feature] implement [relay transport](https://docs.syncthing.net/specs/relay-v1.html),
the relay is randomly chosen from the public relays [pool](https://relays.syncthing.net/endpoint)
- [feature] output binary is compressed via [upx](https://upx.github.io)
- [feature] small optimization, use thread less in overall program
- [bugfix] sometimes fs::scan_actor request timeout occurs, which is fatal
- [bugfix] global discovery sometimes skipped announcements

## 0.1.0 (18-Arp-2022)
 - initial release

# building from source

[see](docs/building.md)

# license


This software is licensed under the [GPLv3 license](https://www.gnu.org/licenses/gpl-3.0.en.html).

    Copyright (C) 2019-2022 Ivan Baidakou (aka basiliscos)

    This file is part of syncspirit.

    syncspirit is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    syncspirit is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with syncspirit.  If not, see <http://www.gnu.org/licenses/>.

[![GPL3 Logo](https://www.gnu.org/graphics/gplv3-127x51.png)](https://www.gnu.org/licenses/gpl-3.0.en.html)
