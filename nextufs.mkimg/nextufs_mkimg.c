#define _FILE_OFFSET_BITS 64

#include "format.h"
#include "nextufs_label.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <libgen.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define COMPAT_MAX_BYTES UINT64_C(4294836224)
#define UFS_SECTOR_SIZE 1024U

struct options {
	int raw;
	int dry_run;
	int force_size;
	int force_overwrite;
	const char *label;
	const char *target;
	uint64_t bytes;
	int extra_argc;
	char **extra_argv;
};

static void
usage(FILE *out)
{
	fprintf(out, "usage: nextufs.mkimg [options] <target> <size> [raw-geometry-args...]\n\n");
	fprintf(out, "Options:\n");
	fprintf(out, "  --raw              create raw UFS at byte zero\n");
	fprintf(out, "  --label NAME       set the NeXT disk label string\n");
	fprintf(out, "  --dry-run          print planned layout without writing\n");
	fprintf(out, "  --force-size       allow sizes above the compatibility limit\n");
	fprintf(out, "  --force-overwrite  replace an existing output file\n");
	fprintf(out, "  -h, --help         show this help\n");
}

static int
parse_size(const char *s, int raw_mode, uint64_t *bytes_out)
{
	char *end;
	unsigned long long value;
	uint64_t mult = 1;

	errno = 0;
	value = strtoull(s, &end, 10);
	if (errno != 0 || end == s)
		return -1;
	if (*end == '\0' && raw_mode) {
		mult = 1024ULL;
	} else if (*end != '\0') {
		if (end[1] != '\0')
			return -1;
		switch (*end) {
		case 'k':
		case 'K':
			mult = 1024ULL;
			break;
		case 'm':
		case 'M':
			mult = 1024ULL * 1024ULL;
			break;
		case 'g':
		case 'G':
			mult = 1024ULL * 1024ULL * 1024ULL;
			break;
		default:
			return -1;
		}
	}
	if (value == 0 || (uint64_t)value > UINT64_MAX / mult)
		return -1;
	*bytes_out = (uint64_t)value * mult;
	return 0;
}

static int
parse_int_arg(const char *s, int *out)
{
	char *end;
	long value;

	errno = 0;
	value = strtol(s, &end, 10);
	if (errno != 0 || end == s || *end != '\0' ||
	    value < INT_MIN || value > INT_MAX)
		return -1;
	*out = (int)value;
	return 0;
}

static const char *
default_label(const char *path)
{
	static char buf[25];
	char tmp[1024];
	char *base;

	snprintf(tmp, sizeof(tmp), "%s", path);
	base = basename(tmp);
	snprintf(buf, sizeof(buf), "%s", base);
	return buf;
}

static int
parse_args(int argc, char **argv, struct options *opts)
{
	int i;

	memset(opts, 0, sizeof(*opts));
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--raw") == 0)
			opts->raw = 1;
		else if (strcmp(argv[i], "--dry-run") == 0)
			opts->dry_run = 1;
		else if (strcmp(argv[i], "--force-size") == 0)
			opts->force_size = 1;
		else if (strcmp(argv[i], "--force-overwrite") == 0)
			opts->force_overwrite = 1;
		else if (strcmp(argv[i], "--label") == 0) {
			if (++i >= argc)
				return -1;
			opts->label = argv[i];
		} else if (strcmp(argv[i], "-h") == 0 ||
		    strcmp(argv[i], "--help") == 0) {
			usage(stdout);
			exit(0);
		} else if (argv[i][0] == '-') {
			return -1;
		} else if (opts->target == NULL) {
			opts->target = argv[i];
		} else if (opts->bytes == 0) {
			if (parse_size(argv[i], opts->raw, &opts->bytes) < 0)
				return -1;
		} else if (opts->raw) {
			opts->extra_argv = &argv[i];
			opts->extra_argc = argc - i;
			break;
		} else {
			return -1;
		}
	}
	if (opts->target == NULL || opts->bytes == 0)
		return -1;
	if (opts->raw && opts->label != NULL) {
		fprintf(stderr, "nextufs.mkimg: --label is not valid with --raw\n");
		return -1;
	}
	if (!opts->raw && opts->extra_argc != 0) {
		fprintf(stderr, "nextufs.mkimg: explicit geometry arguments are only valid with --raw\n");
		return -1;
	}
	if (opts->label == NULL)
		opts->label = default_label(opts->target);
	return 0;
}

static int
create_output(const struct options *opts)
{
	int flags = O_RDWR | O_CREAT;
	int fd;

	flags |= opts->force_overwrite ? O_TRUNC : O_EXCL;
	fd = open(opts->target, flags, 0666);
	if (fd < 0) {
		fprintf(stderr, "nextufs.mkimg: cannot create %s: %s\n",
		    opts->target, strerror(errno));
		return -1;
	}
	if (ftruncate(fd, (off_t)opts->bytes) < 0) {
		fprintf(stderr, "nextufs.mkimg: cannot size %s: %s\n",
		    opts->target, strerror(errno));
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}

static void
print_plan(const struct options *opts, uint64_t slice_bytes)
{
	printf("target: %s\n", opts->target);
	printf("mode: %s\n", opts->raw ? "raw UFS" : "labeled disk image");
	printf("image size: %" PRIu64 " bytes (%" PRIu64 " 1K sectors)\n",
	    opts->bytes, opts->bytes / 1024U);
	if (!opts->raw) {
		printf("label: %s\n", opts->label);
		printf("front porch: %u bytes\n", NEXTUFS_LABEL_FRONT_PORCH_BYTES);
		printf("root slice size: %" PRIu64 " bytes (%" PRIu64 " 1K sectors)\n",
		    slice_bytes, slice_bytes / 1024U);
		printf("geometry: 4 tracks, 32 sectors/track, 1024-byte sectors\n");
	}
}

static int
format_options_from_cli(const struct options *opts, uint64_t fs_sectors,
    struct nextufs_format_options *fmt)
{
	nextufs_format_defaults(fmt);
	fmt->target = opts->target;
	fmt->size_1k_sectors = fs_sectors;
	fmt->dry_run = opts->dry_run;
	fmt->force_size = opts->force_size;
	fmt->no_create = 1;
	fmt->base_offset = opts->raw ? 0 : NEXTUFS_LABEL_FRONT_PORCH_BYTES;
	if (!opts->raw) {
		fmt->nsect = 32;
		fmt->ntrak = 4;
		fmt->bsize = 8192;
		fmt->fsize = 1024;
		fmt->cpg = 16;
		fmt->minfree = 10;
		fmt->rps = 60;
		fmt->nbpi = 4096;
		fmt->opt = 't';
		return 0;
	}
	if (opts->extra_argc > 9) {
		fprintf(stderr, "nextufs.mkimg: too many raw geometry arguments\n");
		return -1;
	}
	if (opts->extra_argc > 0 &&
	    parse_int_arg(opts->extra_argv[0], &fmt->nsect) < 0)
		return -1;
	if (opts->extra_argc > 1 &&
	    parse_int_arg(opts->extra_argv[1], &fmt->ntrak) < 0)
		return -1;
	if (opts->extra_argc > 2 &&
	    parse_int_arg(opts->extra_argv[2], &fmt->bsize) < 0)
		return -1;
	if (opts->extra_argc > 3 &&
	    parse_int_arg(opts->extra_argv[3], &fmt->fsize) < 0)
		return -1;
	if (opts->extra_argc > 4 &&
	    parse_int_arg(opts->extra_argv[4], &fmt->cpg) < 0)
		return -1;
	if (opts->extra_argc > 5 &&
	    parse_int_arg(opts->extra_argv[5], &fmt->minfree) < 0)
		return -1;
	if (opts->extra_argc > 6 &&
	    parse_int_arg(opts->extra_argv[6], &fmt->rps) < 0)
		return -1;
	if (opts->extra_argc > 7 &&
	    parse_int_arg(opts->extra_argv[7], &fmt->nbpi) < 0)
		return -1;
	if (opts->extra_argc > 8) {
		if (opts->extra_argv[8][0] == '\0' ||
		    opts->extra_argv[8][1] != '\0')
			return -1;
		fmt->opt = opts->extra_argv[8][0];
	}
	return 0;
}

int
main(int argc, char **argv)
{
	struct options opts;
	struct nextufs_format_options fmt;
	uint64_t slice_bytes;
	uint64_t fs_sectors;

	if (parse_args(argc, argv, &opts) < 0) {
		usage(stderr);
		return 2;
	}
	if (!opts.force_size && opts.bytes > COMPAT_MAX_BYTES) {
		fprintf(stderr,
		    "nextufs.mkimg: requested size exceeds the NEXTSTEP/OPENSTEP compatibility limit; use --force-size to override\n");
		return 1;
	}
	if ((opts.bytes % UFS_SECTOR_SIZE) != 0) {
		fprintf(stderr, "nextufs.mkimg: size must be a multiple of 1024 bytes\n");
		return 1;
	}
	if (!opts.raw && opts.bytes <= NEXTUFS_LABEL_FRONT_PORCH_BYTES) {
		fprintf(stderr, "nextufs.mkimg: labeled image is too small for the front porch\n");
		return 1;
	}

	slice_bytes = opts.raw ? opts.bytes :
	    opts.bytes - NEXTUFS_LABEL_FRONT_PORCH_BYTES;
	fs_sectors = slice_bytes / UFS_SECTOR_SIZE;
	if (fs_sectors > LONG_MAX) {
		fprintf(stderr, "nextufs.mkimg: filesystem is too large\n");
		return 1;
	}
	if (format_options_from_cli(&opts, fs_sectors, &fmt) < 0) {
		fprintf(stderr, "nextufs.mkimg: invalid raw geometry argument\n");
		return 1;
	}
	print_plan(&opts, slice_bytes);
	if (opts.dry_run)
		return 0;
	if (create_output(&opts) < 0)
		return 1;
	if (!opts.raw && nextufs_label_write_path(opts.target, opts.bytes,
	    slice_bytes, opts.label) < 0) {
		fprintf(stderr, "nextufs.mkimg: failed to write disk label\n");
		return 1;
	}

	return nextufs_format(&fmt);
}
