# configuration

Configuration is generated by default should be OK in the most of cases,
however you might want to tune it.

All timeouts are specified in milliseconds

```toml

# settings peer connection
[bep]
rx_buff_size = 33554432     # preallocated buffer size
connect_timeout = 5000
request_timeout = 60000
rx_timeout = 300000
tx_timeout = 10000
blocks_max_requested = 16   # maximum amount of concurrently requested blocks

[db]
upper_limit = 0x400000000   # maximum amount of database, in bytes
uncommited_threshold = 150  # how often flush db to disk, i.e. how much operations
                            # might be not commited. Affects disk performance

[dialer]
enabled = true
redial_timeout = 300000     # how often try to redial to offline peers

[fs]
temporally_timeout = 86400000   # remove incomplete file after this timeout
mru_size = 10                   # maximum amount of cached files


[global_discovery]
announce_url = 'https://discovery.syncthing.net/'
# this device certificate location
cert_file = '/home/b/.config/syncthing/cert.pem'
# the device_id of global discovery server
device_id = 'LYXKCHX-VI3NYZR-ALCJBHF-WMZYSPK-QG6QJA3-MPFYMSO-U56GTUK-NA2MIAW'
enabled = true
# this device key location
key_file = '/home/b/.config/syncthing/key.pem'
rx_buff_size = 32768
timeout = 4000

[local_discovery]
enabled = true
# how often send announcements in LAN, in milliseconds
frequency = 10000
port = 21026

# default log settings
[[log]]
name = 'default'
# possible values: trace, debug, info, warn, error, critical
level = 'trace'
# where do output logs
sinks = ['stdout', 'file:/tmp/log.txt']

# actor specific log (for net.db in the case)
[[log]]
name = 'net.db'
level = 'debug'

[main]
# where folders are created by dfault
default_location = '/tmp/syncspirit'
# this device name
device_name = 'this-device-name'
timeout = 5000
# the amount of hasher threads
hasher_threads = 3

[relay]
enabled = true
# where pick the list of relay servers pool
discovery_url = 'https://relays.syncthing.net/endpoint'
rx_buff_size = 1048576

[upnp]
enabled = true
# do output of upnp requests
debug = false
discovery_attempts = 2
# external port for communication, opened on router
external_port = 22001
max_wait = 1
rx_buff_size = 65536

```

