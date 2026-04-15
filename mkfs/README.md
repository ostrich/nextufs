# mkfs.nextufs

Linux port of the historical NeXT/BSD `mkfs` source from
an older NeXT/BSD source drop.

Current state:

- builds as `mkfs.nextufs`
- writes big-endian NeXT/OpenStep-style UFS metadata
- validated locally by creating a raw filesystem image and checking it with
  `../fsck.nextufs/fsck.nextufs`
- the generated image is also readable with
  `../nextufs/nextufs_probe`

Build:

```sh
make -f Makefile.linux
```

Example:

```sh
./mkfs.nextufs /tmp/mkfs-next-test.img 65536 63 16 8192 1024 16 10 60 2048 t
```

That creates a 64 MiB raw filesystem image using:

- `size=65536` 1 KiB sectors
- `nsect=63`
- `ntrak=16`
- `bsize=8192`
- `fsize=1024`
- `cpg=16`
- `minfree=10`
- `rps=60`
- `nbpi=2048`
- `opt=t`

Validation workflow:

```sh
./mkfs.nextufs /tmp/mkfs-next-test.img 65536 63 16 8192 1024 16 10 60 2048 t
../fsck.nextufs/fsck.nextufs -n /tmp/mkfs-next-test.img
../nextufs/nextufs_probe /tmp/mkfs-next-test.img
```

The port builds cleanly with the current `Makefile.linux` warning settings.
