# faq

## How to handle SSL errors like "certificate verify failed (SSL routines)" (actual
for windows xp)

This is caused by outdates system certificates as using them SSL-layer of
`syncspirit` cannot validate authentity of remote responces from
web urls like https://discovery-announce-v4.syncthing.net.

To fix it you can

1. download mozilla ca certificates as single file from https://curl.se/docs/caextract.html

2. point to that certificate in `syncspirit.toml`

```
...
[main]
...
root_ca_file = 'c:\path\to\..\cacert.pem'
```

and restart `syncspirit`.


## How to import existing directories into shared folder, i.e. to avoid  full
synchronization and transfer over network?

The [BEP-protocol](https://docs.syncthing.net/specs/bep-v1.html) tracks the
following information on a synchinized file:

1. Content, i.e. data blocks,

2. File metainformation, i.e. permissions and modification times

3. Syncrhonization information, i.e. file modification history from cluster point
of view.


Hence, simple file copying into the destination directory (1) is not enough.
Copying files full metainformation (i.e. (1) and (2)), can be done via the
following steps (on linux and mac-os):

- Create an archive with preserved permissions via (`p` is essential here):

```
tar -cjpf folder.tar.bz2 folder
```

- Unpack in the target location via

```
tar -xpf folder.tar.bz2
```

However, the synchronization information cannot be obtained outside of `syncspririt`,
to get it the following steps should be done:

1. Share folder with a peer, set the `scheduled` checkmark on the folder.

2. Wait until metainformation is fetched from a peer

3. Unpack the folder into the destination directory (`tar -xpf ...`)

4. Do "scan" on the folder.

You are not forced to import the whole folder. That way partial per-directory
import is also supported. The only requirement is tha the unpacked directories
should be located in the proper places.

## Why would I want to use it instead of or alongside with Syncthing? What does it do better?

- syncspirit is faster. According to my measurements it is able to sync linux
sources tree folder for 2 mins vs 15 mins of syncing when using syncthing
(that’s over a localhost, of course)
- syncspirit has a different UI. That’s matter of personal taste, of course,
but I like to see the exact picture what is synchronized and what is not.
- syncspirit is able to run on more older software (i.e. from windows xp and up).
Syncthing uses golang, and its software support is indirectly controlled by
google (i.e. “artificial aging”); recent builds are running only on
windows 10 and windows 11. Microsoft already dropped windows 10 support,
so, I expect that in near future google will do the same.
- the long-term goal of syncspirit is to allow “selective sync” feature,
which unlikely to be implemented by syncthing.

