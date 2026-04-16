# nextufs

`nextufs` provides the shared filesystem library used by the probe tool, the
FUSE frontend, and the offline mutation utilities.

## Programs

- `nextufs_probe`: inspect a filesystem source and resolve paths
- `nextufs`: mount a source through FUSE
- `nextufs_mkfile`: offline mutation tool used by tests and development
- `nextufs_stress`: deterministic mutation stress harness
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

Probe a source:

```sh
./nextufs_probe /path/to/source
./nextufs_probe /path/to/source /etc/passwd
```

Mount a source read-only:

```sh
mkdir -p /tmp/nextufs-mnt
./nextufs /path/to/source /tmp/nextufs-mnt -f -s
```

Mount a source read-write:

```sh
./nextufs /path/to/source /tmp/nextufs-mnt -o rw -f -s
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
./nextufs /path/to/source /tmp/nextufs-mnt -f -s
```

Read-write superuser mode:

```sh
./nextufs /path/to/source /tmp/nextufs-mnt -o rw,mode=su -f -s
```

Read-write user mode:

```sh
./nextufs /path/to/source /tmp/nextufs-mnt -o rw,mode=user -f -s
```

Read-only user mode with explicit identity:

```sh
./nextufs /path/to/source /tmp/nextufs-mnt -o ro,mode=user,uid=1000,gid=1000 -f -s
```

Apply an offline mutation:

```sh
./nextufs_mkfile --policy permissions --uid 1000 --gid 100 \
  --chmod /path/to/source /private/tmp/example 0600
```

Run stress:

```sh
./nextufs_stress --seed 0x13579bdf --ops 250 /path/to/source
./nextufs_stress --backend fuse --seed 0x13579bdf --ops 120 /path/to/source
```

## Tests

Run the component suite:

```sh
make -f Makefile.linux test
```

Additional mutation and stress targets are defined in
[`Makefile.linux`](Makefile.linux).
