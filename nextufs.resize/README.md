# nextufs.resize

`nextufs.resize` analyzes and grows NEXTSTEP/OPENSTEP UFS images offline.

It is intentionally conservative. It supports grow-only resizing for raw UFS
images and for labeled NeXT disk images that contain a single root UFS slice.

## Build

```sh
make -C nextufs.resize -f Makefile.linux
```

## Analyze

```sh
./nextufs.resize/nextufs.resize analyze /path/to/source
```

`analyze` reports:

- whether the source is raw or labeled
- backing-file size
- root slice base and size for labeled images
- filesystem size and unused trailing slice space
- cylinder-group and cylinder-summary layout
- free block, fragment, and inode counts

## Grow

```sh
./nextufs.resize/nextufs.resize grow /path/to/source 2097152
```

The size argument is a count of 1 KiB sectors.

For raw filesystem images, the requested size is the new filesystem size.

For labeled disk images, the requested size is the final backing-file size.
The root UFS slice grows to fill the remaining image space after the disk-label
front porch.

## Safety Rules

`nextufs.resize` refuses:

- shrinking
- mounted filesystems
- VDI and VDI differencing-chain containers
- labeled images with more than one real partition entry
- target sizes above `4,294,836,224` bytes (`4,194,176` 1 KiB sectors) unless
  `--force-size` is supplied
- target sizes that are not compatible with the existing cylinder-group layout
- growth that would require unsupported metadata relocation

Use `--force-size` only when you intentionally want to exceed the observed
NEXTSTEP/OPENSTEP compatibility ceiling:

```sh
./nextufs.resize/nextufs.resize grow --force-size /path/to/source 5242880
```

## Recommended Workflow

Work on a disposable copy, not the only copy of a VM disk:

```sh
cp disk.img disk-grow-test.img
./nextufs.fsck/nextufs.fsck -n disk-grow-test.img
./nextufs.resize/nextufs.resize analyze disk-grow-test.img
./nextufs.resize/nextufs.resize grow disk-grow-test.img 2097152
./nextufs.fsck/nextufs.fsck -n disk-grow-test.img
```

## Validation

The grow path has been tested on disposable images, including:

- a 64 MiB raw filesystem grown to 128 MiB
- offline mutation stress after raw growth
- a NEXTSTEP 3.3 labeled disk image grown from 360 MiB to 384 MiB
- a NEXTSTEP 3.3 labeled disk image grown from 360 MiB to 2 GiB
- direct data-block evacuation from the low cg0 cylinder-summary extension
  range before a 2 GiB grow
- clean `nextufs.fsck -n` checks after growth and mutation tests

## Implementation Notes

The primary superblock and alternate superblocks use different addressing
conventions in this filesystem layout. `nextufs.resize` updates both forms.

When growth turns the previous final cylinder group into an ordinary group,
`nextufs.resize` updates that group's bitmap, summary counts, and `cg_ncyl`
value before extending the filesystem.

Large growth may require expanding the cylinder-summary table in cg0.
`nextufs.resize` can conservatively relocate direct file data blocks that occupy
the future summary-extension range. It refuses unsupported cases, including
indirect metadata in that range.
