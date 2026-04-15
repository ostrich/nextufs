# nextufs

`nextufs` provides the shared filesystem library used by the probe tool, the
FUSE frontend, and the offline mutation utilities.

## Programs

- `nextufs_probe`: inspect a filesystem source and resolve paths
- `nextufs_fuse`: mount a source through FUSE
- `nextufs_mkfile`: offline mutation tool used by tests and development
- `nextufs_stress`: deterministic mutation stress harness
- `nextufs_test`: library regression binary

## Supported Sources

- raw disk images
- standalone VDI images
- VDI differencing chains

Writable operations are supported for raw sources and writable VDI chains.

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

Mount a source:

```sh
mkdir -p /tmp/nextufs-mnt
./nextufs_fuse /path/to/source /tmp/nextufs-mnt -f -s
```

## FUSE Modes

`nextufs_fuse` supports two write-policy modes:

- `permissions` (default)
  - operations are checked against the calling process `uid` and `gid`
  - this is the closer match to normal Unix filesystem behavior
  - use this when you want the mounted source to behave like a regular filesystem
- `editor`
  - metadata edits are allowed without normal ownership and permission checks
  - this is useful for image repair, migration, and administrative editing
  - use this when you want the mount to behave like an image editor

Default mount behavior:

```sh
./nextufs_fuse /path/to/source /tmp/nextufs-mnt -f -s
```

Explicit editor mode:

```sh
./nextufs_fuse /path/to/source /tmp/nextufs-mnt -o nextufs_mode=editor -f -s
```

Explicit permissions mode:

```sh
./nextufs_fuse /path/to/source /tmp/nextufs-mnt -o nextufs_mode=permissions -f -s
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
