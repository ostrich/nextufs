# fsck

Read-only NeXT/OpenStep UFS validation tooling.

Current scope:

- open a raw image with `pread`
- inspect the first sector as an MBR
- decode a NeXT `dlV3` disk label and select a filesystem slice
- decode the UFS superblock and inode geometry
- run read-only `fsck`-style checks:
- phase 1: inode scan and basic data-block range/dup checks
- phase 2/3: recursive directory traversal from root and connectivity checks
- phase 4: compare observed directory references against inode link counts
- phase 5: placeholder only for now

Build:

```sh
make -f Makefile.linux
```

Reference sources:

- the original historical `fsck` sources came from a NeXT/BSD source drop
- the local compatibility include layer in `include/` was derived from
  NeXTMach-era UFS and BSD/Mach headers

Run against the OpenStep sample image:

```sh
./fsck_probe openstep42-base.raw
```

Historical `fsck` port:

- `fsck_next` builds from the original source with a local compatibility layer
- the source now compiles in both `i386` and `x86_64` Linux configurations
- `fsck_next` is the only historical-port binary that gets produced
- by default it builds for the host ABI
- if you want a 32-bit build from the same source tree, use `ARCH_CFLAGS=-m32`
- both native and `-m32` builds now complete the same clean five-phase read-only run on the OpenStep 4.2 sample slice
- for the OpenStep 4.2 sample, the on-disk metadata is byte-swapped relative to this host, so the current port forces read-only operation on that filesystem

Build the original port:

```sh
make -f Makefile.linux fsck_next
make -f Makefile.linux ARCH_CFLAGS=-m32 fsck_next
```

Run it on the extracted filesystem slice from the sample image:

```sh
dd if=openstep42-base.raw of=/tmp/openstep42-a.raw bs=1024 skip=160 count=2096480 status=none
./fsck_next -n /tmp/openstep42-a.raw
```

Expected result on the sample image is a clean five-phase read-only pass ending with:

```text
15874 files, 191561 used, 1869540 free (812 frags, 233591 blocks, 0.0% fragmentation)
```
