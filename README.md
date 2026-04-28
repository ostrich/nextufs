# nextufs

`nextufs` is a small toolchain for working with NEXTSTEP/OPENSTEP UFS filesystems.
It includes:

- `nextufs/`: unified CLI, shared library, writable FUSE frontend, and stress tools
- `nextufs.fsck/`: filesystem checker and repair tool

## Features

- show NEXTSTEP/OPENSTEP UFS filesystem information
- browse files and directories inside NEXTSTEP/OPENSTEP UFS filesystems
- mount them through FUSE
- create and mutate filesystems offline
- check and repair filesystem metadata
- work with raw sources and VirtualBox VDI containers

## Source Support

- raw disk images
- standalone VDI images
- VDI differencing chains

Writable support is available in `nextufs` for raw sources, standalone VDIs,
and writable VDI chains. `nextufs.fsck` checks and repairs raw sources,
standalone VDIs, and VDI differencing chains directly through the shared
`nextufs` source backend. `nextufs mkimg` creates labeled disk images that
NEXTSTEP/OPENSTEP can recognize as initialized, and `nextufs mkimg --raw`
creates raw filesystem images. `nextufs info` reports image layout and
growability details. `nextufs resize` grows raw UFS images and supported
single-slice labeled disk images.

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

Show image and filesystem information:

```sh
./nextufs/nextufs info /path/to/source
```

Browse filesystem contents:

```sh
./nextufs/nextufs browse /path/to/source
./nextufs/nextufs browse /path/to/source /etc/passwd
```

Mount through FUSE:

```sh
mkdir -p /tmp/nextufs-mnt
./nextufs/nextufs mount /path/to/source /tmp/nextufs-mnt -f -s
./nextufs/nextufs mount /path/to/source /tmp/nextufs-mnt -o rw,mode=su -f -s
```

Check a filesystem:

```sh
./nextufs.fsck/nextufs.fsck -n /path/to/filesystem.img
./nextufs.fsck/nextufs.fsck -y /path/to/disk-or-vdi-source
```

Create a labeled disk image:

```sh
./nextufs/nextufs mkimg /tmp/nextufs.img 256M
```

Create a raw filesystem:

```sh
./nextufs/nextufs mkimg --raw /tmp/nextufs.img 65536 63 16 8192 1024 16 10 60 2048 t
```

Show information or grow an image:

```sh
./nextufs/nextufs info /path/to/source
./nextufs/nextufs resize grow /path/to/source 2097152
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
```

Component-specific notes are in:

- [nextufs/README.md](nextufs/README.md)
- [nextufs.fsck/README.md](nextufs.fsck/README.md)
