# mkfs.nextufs

`mkfs.nextufs` creates NeXT/OpenStep-style UFS filesystem images.

## Build

```sh
make -f Makefile.linux
```

## Minimal Usage

```sh
./mkfs.nextufs /tmp/nextufs.img 65536
```

Or use the full size of an existing target:

```sh
./mkfs.nextufs /path/to/existing.img
```

With only `target` and `size`, `mkfs.nextufs` uses built-in defaults for the
filesystem geometry and allocation policy.

With only `target`, `mkfs.nextufs` uses the full size of an existing target and
the same built-in defaults.

`mkfs.nextufs` caps implicit target sizing at 4 GiB and rejects explicit sizes
above 4 GiB for NEXTSTEP/OPENSTEP compatibility.

`size` is expressed in 1 KiB sectors, so `65536` creates a 64 MiB image.

The size does not need to be a special multiple, but it does need to be large
enough for a valid UFS layout.

If you specify geometry or policy arguments, include `size` explicitly.

## Full Example

```sh
./mkfs.nextufs /tmp/nextufs.img 65536 63 16 8192 1024 16 10 60 2048 t
```

Argument order:

- `target`
  output image or device path
- `size`
  filesystem size in 1 KiB sectors
- `nsect`
  sectors per track
- `ntrak`
  tracks per cylinder
- `bsize`
  filesystem block size
- `fsize`
  fragment size
- `cpg`
  cylinders per group
- `minfree`
  minimum free-space reserve percentage
- `rps`
  disk revolutions per second
- `nbpi`
  bytes per inode
- `opt`
  allocation preference: `t` for time, `s` for space

Default values:

- `nsect=32`
- `ntrak=16`
- `bsize=8192`
- `fsize=1024`
- `cpg=16` or a derived compatible value
- `minfree=10`
- `rps=60`
- `nbpi=2048`
- `opt=t`

The example above creates a raw filesystem image with:

- `size=65536`: 64 MiB total size
- `nsect=63`, `ntrak=16`: disk geometry values
- `bsize=8192`: 8 KiB blocks
- `fsize=1024`: 1 KiB fragments
- `cpg=16`: 16 cylinders per group
- `minfree=10`: 10% reserved free space
- `rps=60`: 60 revolutions per second
- `nbpi=2048`: one inode per 2048 bytes
- `opt=t`: optimize for time

## Validate

```sh
../fsck.nextufs/fsck.nextufs -n /tmp/nextufs.img
../nextufs/nextufs_probe /tmp/nextufs.img
```

Check reproducibility:

```sh
make -f Makefile.linux test-reproducible
```

For command-line help:

```sh
./mkfs.nextufs -h
```
