# syncspirit-daemon

The program connects to peers (and listens incoming connections from peers)
and downloads files to local disk. The program stays always "online"
(similar to [syncthing](https://syncthing.net)), unless `inactivate` 
command is defined. 

`syncspirit-daemon` shares the same database, config folder and files with
other `syncspirit` programs, so it is possible to "setup" configuration
in one program and sync in other.

## generic command line options

 - `--log_level` log verbosity. Possible values are: `trace`, `debug`,
`info`, `warn`, `error`, `crit`, `off`

 - `--config_dir` path to directory with `syncspirit.toml`, ssh keys
and database

 - `--command` invoke a command within `syncspirit` core. It is possible
to specify several commands, the next command is executed after the 
previous one is successfully applied.

## commands

 - `add_peer:$label:$device` record the target `$device` (locally name 
it `$label`) into the `syncspirit` database, this allows to stay connected 
with `$peer`.

 - `add_folder:label=$label:id=$id:path=$path` add the folder `$label` 
with `$id` into the `syncspirit` database. All downloaded files will
be located under `$path`

 - `share:folder=$folder:device=$device` shares the specified folder with
the specified peer device. The `$folder` can refer folder label or 
folder id; the `$device` can refer device either via full device id, 
short device id or via device label.

 - `inactivate:$seconds` shutdown the programs after `$seconds` of
inactivity (i.e. no traffic between peers).


# launch example
 
```
syncspirit-daemon --log_level info \
    --config_dir=/tmp/my_dir \
    --command add_peer:peer_label:KUEQE66-JJ7P6AD-BEHD4ZW-GPBNW6Q-Y4C3K4Y-X44WJWZ-DVPIDXS-UDRJMA7 \
    --command add_folder:label=my_folder-label:id=nagkw-srrjz:path=/tmp/my_dir/data \
    --command share:folder=my_folder-label:device=KUEQE66 \
    --command inactivate:120
```
