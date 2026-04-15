# fsck.nextufs

`fsck.nextufs` checks and repairs NeXT/OpenStep-style UFS filesystems on raw
disk images.

Current capabilities:
- raw-image checking and repair
- byte-swapped filesystem handling with writable buffer-boundary swapping
- deterministic corruption corpus and repair lab under [`tests/`](tests)

Build:

```sh
make -f Makefile.linux
```

Run:

```sh
./fsck.nextufs -n /path/to/filesystem.img
./fsck.nextufs -y /path/to/filesystem.img
```

Notes:
- the checker currently operates on raw filesystem sources
- the repair lab is documented in [tests/README.md](tests/README.md)
