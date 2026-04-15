# fsck.nextufs

`fsck.nextufs` checks and repairs NeXT/OpenStep-style UFS filesystems.

## Current Scope

- raw filesystem images
- raw disk images that contain a NeXT/OpenStep filesystem slice
- standalone VDI images
- VDI differencing chains
- read-only checking with `-n`
- repair mode with `-y`
- byte-swapped filesystem handling on little-endian hosts

## Build

```sh
make -f Makefile.linux
```

## Usage

Check without modifying:

```sh
./fsck.nextufs -n /path/to/filesystem.img
./fsck.nextufs -n /path/to/disk-or-vdi-source
```

Attempt repairs:

```sh
./fsck.nextufs -y /path/to/filesystem.img
```

Notes:

- raw filesystem images are checked in place
- container-backed and sliced sources are currently staged to a temporary raw
  slice before checking
- those staged sources are therefore read-only from `fsck.nextufs`'s point of
  view and will show `NO WRITE`

## Test Lab

The deterministic corruption and repair corpus lives under [`tests/`](tests).
For details, see [tests/README.md](tests/README.md).
