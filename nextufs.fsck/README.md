# nextufs.fsck

`nextufs.fsck` checks and repairs NEXTSTEP/OPENSTEP-style UFS filesystems.

## Current Scope

- raw filesystem images
- raw disk images that contain a NEXTSTEP/OPENSTEP filesystem slice
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
./nextufs.fsck -n /path/to/filesystem.img
./nextufs.fsck -n /path/to/disk-or-vdi-source
```

Attempt repairs:

```sh
./nextufs.fsck -y /path/to/filesystem.img
./nextufs.fsck -y /path/to/disk-or-vdi-source
```

Notes:

- raw filesystem images, disk images, and VDI sources are checked directly
  through the shared `nextufs` source backend
- `-n` still opens the source read-only and will show `NO WRITE`
- `-y` can repair standalone and chained VDI sources in place when the image
  files are writable
- for a VDI differencing chain, point `nextufs.fsck` at the chain head, not
  the base image
- use [`../tools/vdi_chain.py`](../tools/vdi_chain.py) to identify the current
  head of a VirtualBox snapshot chain before running `nextufs.fsck`

## Test Lab

The deterministic corruption and repair corpus lives under [`tests/`](tests).
For details, see [tests/README.md](tests/README.md).
