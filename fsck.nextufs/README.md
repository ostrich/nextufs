# fsck.nextufs

`fsck.nextufs` checks and repairs NeXT/OpenStep-style UFS filesystems.

## Current Scope

- raw filesystem images
- raw disk images that contain a NeXT/OpenStep filesystem slice
- standalone VDI images
- VDI differencing chains
- read-only checking with `-n`
- in-place repair mode with `-y` when the source is writable
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
./fsck.nextufs -y /path/to/disk-or-vdi-source
```

Notes:

- raw filesystem images, disk images, and VDI sources are checked directly
  through the shared `nextufs` source backend
- `-n` still opens the source read-only and will show `NO WRITE`
- `-y` can repair standalone and chained VDI sources in place when the image
  files are writable
- for a VDI differencing chain, point `fsck.nextufs` at the chain head, not
  the base image
- use [`../tools/vdi_chain.py`](../tools/vdi_chain.py) to identify the current
  head of a VirtualBox snapshot chain before running `fsck.nextufs`

## Test Lab

The deterministic corruption and repair corpus lives under [`tests/`](tests).
For details, see [tests/README.md](tests/README.md).
