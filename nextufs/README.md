# nextufs

`nextufs` provides the unified command-line interface, shared filesystem
library, FUSE frontend, and offline mutation utilities.

## Programs

- `nextufs inspect`: inspect a filesystem source and resolve paths
- `nextufs mount`: mount a source through FUSE
- `nextufs mkimg`: create raw or labeled UFS images
- `nextufs resize`: grow supported images offline
- `nextufs mkfile`: offline mutation tool used by tests and development
- `nextufs_test`: library regression binary

## Supported Sources

- raw disk images
- standalone VDI images
- VDI differencing chains

Writable operations are supported for raw sources, standalone VDIs, and
writable VDI chains.

## Build

```sh
make -f Makefile.linux
```

## Examples

Inspect a source:

```sh
./nextufs inspect /path/to/source
./nextufs inspect /path/to/source /etc/passwd
```

Mount a source read-only:

```sh
mkdir -p /tmp/nextufs-mnt
./nextufs mount /path/to/source /tmp/nextufs-mnt -f -s
```

Mount a source read-write:

```sh
./nextufs mount /path/to/source /tmp/nextufs-mnt -o rw -f -s
```

Create a labeled image:

```sh
./nextufs mkimg /tmp/nextufs.img 256M
```

Grow an image:

```sh
./nextufs resize grow /path/to/source 2097152
```

## FUSE Modes

`nextufs` supports two mount modes:

- `su` (default)
  - ignores normal ownership and permission checks
  - use this for inspection, repair, migration, and administrative editing
- `user`
  - enforces read, write, and search checks using a Unix-style `uid` and `gid`
  - defaults to the mounting user's `uid` and `gid`
  - accepts `uid=` and `gid=` overrides when you want to emulate a different user

Default mount behavior is read-only:

```sh
./nextufs mount /path/to/source /tmp/nextufs-mnt -f -s
```

Read-write superuser mode:

```sh
./nextufs mount /path/to/source /tmp/nextufs-mnt -o rw,mode=su -f -s
```

Read-write user mode:

```sh
./nextufs mount /path/to/source /tmp/nextufs-mnt -o rw,mode=user -f -s
```

Read-only user mode with explicit identity:

```sh
./nextufs mount /path/to/source /tmp/nextufs-mnt -o ro,mode=user,uid=1000,gid=1000 -f -s
```

Apply an offline mutation:

```sh
./nextufs mkfile --policy user --uid 1000 --gid 100 \
  --chmod /path/to/source /private/tmp/example 0600
```

## Tests

Run the component suite:

```sh
make -f Makefile.linux test
```

Additional mutation and stress targets are defined in
[`Makefile.linux`](Makefile.linux).
