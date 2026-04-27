# nextufs.mkimg

`nextufs.mkimg` creates NeXT UFS images.

By default it creates a labeled NEXTSTEP/OPENSTEP-style disk image with a NeXT
disk label, a standard hard-disk front porch, and a single root UFS partition.

Use `--raw` when you only want a raw UFS filesystem at byte zero.

## Examples

```sh
./nextufs.mkimg disk.img 256M
./nextufs.mkimg --label "Data" disk.img 256M
./nextufs.mkimg --raw raw.ufs 256M
./nextufs.mkimg --dry-run disk.img 2G
```

## Safety Options

- `--force-size` allows image sizes above the NEXTSTEP/OPENSTEP compatibility
  ceiling.
- `--force-overwrite` allows replacing an existing output file.

The default compatibility ceiling is `4,294,836,224` bytes, matching the
largest fallback-geometry disk below the 32768-cylinder boundary observed in
NEXTSTEP 3.3.

## Size Meaning

In labeled mode, size is the final disk image size.

In raw mode, size is the raw filesystem size.
