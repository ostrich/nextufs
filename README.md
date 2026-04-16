# nextufs

`nextufs` is a small toolchain for working with NEXTSTEP/OPENSTEP UFS filesystems.
It includes:

- `nextufs/`: shared library, probe tool, writable FUSE frontend, and stress tools
- `fsck.nextufs/`: filesystem checker and repair tool
- `mkfs.nextufs/`: filesystem builder

## Features

- inspect NEXTSTEP/OPENSTEP UFS filesystems
- mount them through FUSE, read-only by default
- create and mutate filesystems offline
- check and repair filesystem metadata
- work with raw sources and VirtualBox VDI containers

## Source Support

- raw disk images
- standalone VDI images
- VDI differencing chains

Writable support is available in `nextufs` for raw sources, standalone VDIs,
and writable VDI chains. `fsck.nextufs` checks and repairs raw sources,
standalone VDIs, and VDI differencing chains directly through the shared
`nextufs` source backend. `mkfs.nextufs` creates raw filesystem images.

## Build

```sh
make
```

Install to the default prefix:

```sh
make install
```

Override the install location:

```sh
make prefix=$HOME/.local install
DESTDIR=/tmp/pkgroot make install
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
./nextufs/nextufs /path/to/source /tmp/nextufs-mnt -f -s
./nextufs/nextufs /path/to/source /tmp/nextufs-mnt -o rw,mode=su -f -s
```

Check a filesystem:

```sh
./fsck.nextufs/fsck.nextufs -n /path/to/filesystem.img
./fsck.nextufs/fsck.nextufs -y /path/to/disk-or-vdi-source
```

Create a filesystem:

```sh
./mkfs.nextufs/mkfs.nextufs /tmp/nextufs.img 65536 63 16 8192 1024 16 10 60 2048 t
```

## Tests

Run the main repo-wide test entrypoint:

```sh
make test
```

Component-specific entrypoints:

```sh
make test-nextufs
make test-fsck
make test-mkfs
```

The project is also exercised with:

- deterministic mutation stress tests for raw and FUSE-backed sources
- copied chained-VDI repair and mutation testing, including offline and
  FUSE-backed runs
- a cached `fsck.nextufs` corruption corpus with repair and comparison workflows
- `mkfs.nextufs` reproducibility checks for canonical invocations

Component-specific notes are in:

- [nextufs/README.md](nextufs/README.md)
- [fsck.nextufs/README.md](fsck.nextufs/README.md)
- [mkfs.nextufs/README.md](mkfs.nextufs/README.md)
