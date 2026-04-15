# nextufs

`nextufs` is a small toolchain for working with NeXT/OpenStep UFS filesystems.
It includes:

- `nextufs/`: shared library, probe tool, writable FUSE frontend, and stress tools
- `fsck.nextufs/`: filesystem checker and repair tool
- `mkfs.nextufs/`: filesystem builder

## Features

- inspect NeXT/OpenStep UFS filesystems
- mount them through FUSE
- create and mutate filesystems offline
- check and repair filesystem metadata
- work with raw sources and VirtualBox VDI containers

## Source Support

- raw disk images
- standalone VDI images
- VDI differencing chains

Writable support is available in `nextufs` for raw sources and writable VDI
chains. `fsck.nextufs` can also inspect raw disk images and VDI sources by
staging the active filesystem slice read-only. `mkfs.nextufs` creates raw
filesystem images.

## Build

```sh
make
```

## Quick Start

Inspect a source:

```sh
./nextufs/nextufs_probe /path/to/source
./nextufs/nextufs_probe /path/to/source /etc/passwd
```

Mount through FUSE:

```sh
mkdir -p /tmp/nextufs-mnt
./nextufs/nextufs_fuse /path/to/source /tmp/nextufs-mnt -f -s
```

Check a filesystem:

```sh
./fsck.nextufs/fsck.nextufs -n /path/to/filesystem.img
```

Create a filesystem:

```sh
./mkfs.nextufs/mkfs.nextufs /tmp/nextufs.img 65536 63 16 8192 1024 16 10 60 2048 t
```

## Tests

Run the main regression suite:

```sh
make test
```

The project is also exercised with:

- deterministic mutation stress tests for raw and FUSE-backed sources
- a cached `fsck.nextufs` corruption corpus with repair and comparison workflows
- `mkfs.nextufs` reproducibility checks for canonical invocations

Component-specific notes are in:

- [nextufs/README.md](nextufs/README.md)
- [fsck.nextufs/README.md](fsck.nextufs/README.md)
- [mkfs.nextufs/README.md](mkfs.nextufs/README.md)
