#define _FILE_OFFSET_BITS 64

#include "format.h"

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
#define DEV_SECTOR_SIZE 512U
#define UFS_SECTOR_SIZE 1024U
#define FRONT_PORCH_BYTES (160U * 1024U)
#define LABEL_COPY_COUNT 4U
#define LABEL_PART_COUNT 8U
#define LABEL_COPY_STRIDE 7680U
#define LABEL_CKSUM_SIZE 0x230U
#define LABEL_WRITE_SIZE 0x300U
#define DL_V3 0x646c5633U
#define DL_DISKTAB_OFF 0x02cU
#define DL_V3_CKSUM_OFF 0x022eU
#define DT_NAME_OFF 0x000U
#define DT_TYPE_OFF 0x018U
#define DT_SECSIZE_OFF 0x032U
#define DT_NTRACKS_OFF 0x036U
#define DT_NSECTORS_OFF 0x03aU
#define DT_NCYLINDERS_OFF 0x03eU
#define DT_RPM_OFF 0x042U
#define DT_FRONT_OFF 0x044U
#define DT_BACK_OFF 0x046U
#define DT_BOOT0_BLKNO_OFF 0x056U
#define DT_BOOTFILE_OFF 0x058U
#define DT_HOSTNAME_OFF 0x070U
#define DT_ROOTPART_OFF 0x090U
#define DT_RWPART_OFF 0x091U
#define DT_PARTITIONS_OFF 0x094U
#define DT_PARTITION_SIZE 0x040U
#define PART_BASE_OFF 0x00U
#define PART_SIZE_OFF 0x03U
#define PART_BSIZE_OFF 0x06U
#define PART_FSIZE_OFF 0x08U
#define PART_OPT_OFF 0x0aU
#define PART_CPG_OFF 0x0cU
#define PART_DENSITY_OFF 0x0eU
#define PART_MINFREE_OFF 0x10U
#define PART_NEWFS_OFF 0x11U
#define PART_AUTOMNT_OFF 0x22U
#define PART_TYPE_OFF 0x23U

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

static void
write_be16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)(v >> 8);
	p[1] = (uint8_t)v;
}

static void
write_be24(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 16);
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)v;
}

static void
write_be32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

static uint16_t
checksum_be16(const uint8_t *buf, size_t size)
{
	uint32_t sum = 0;
	size_t i;

	for (i = 0; i + 1 < size; i += 2) {
		sum += ((uint32_t)buf[i] << 8) | (uint32_t)buf[i + 1];
		if (sum > 0xffffU)
			sum -= 0xffffU;
	}
	return (uint16_t)sum;
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
put_string(uint8_t *dst, size_t len, const char *s)
{
	size_t n = strlen(s);

	if (n >= len)
		n = len - 1U;
	memcpy(dst, s, n);
}

static int
write_label_copy(int fd, off_t off, uint32_t total_blocks,
    uint32_t slice_blocks, uint16_t ncyl, const char *label)
{
	uint8_t buf[LABEL_WRITE_SIZE];
	uint8_t *dt;
	uint8_t *part;

	memset(buf, 0, sizeof(buf));
	write_be32(buf + 0x00, DL_V3);
	write_be32(buf + 0x04, (uint32_t)(off / DEV_SECTOR_SIZE));
	write_be32(buf + 0x08, total_blocks);
	put_string(buf + 0x0c, 24, label);

	dt = buf + DL_DISKTAB_OFF;
	put_string(dt + DT_NAME_OFF, 24, "nextufs");
	put_string(dt + DT_TYPE_OFF, 24, "fixed_rw_scsi");
	write_be16(dt + DT_SECSIZE_OFF, UFS_SECTOR_SIZE);
	write_be16(dt + DT_NTRACKS_OFF, 4);
	write_be16(dt + DT_NSECTORS_OFF, 32);
	write_be16(dt + DT_NCYLINDERS_OFF, ncyl);
	write_be16(dt + DT_RPM_OFF, 3600);
	write_be16(dt + DT_FRONT_OFF, FRONT_PORCH_BYTES / UFS_SECTOR_SIZE);
	write_be16(dt + DT_BACK_OFF, 0);
	write_be32(dt + DT_BOOT0_BLKNO_OFF, 32);
	write_be32(dt + DT_BOOT0_BLKNO_OFF + 4, 96);
	put_string(dt + DT_BOOTFILE_OFF, 24, "sdmach");
	put_string(dt + DT_HOSTNAME_OFF, 32, "localhost");
	dt[DT_ROOTPART_OFF] = 'a';
	dt[DT_RWPART_OFF] = 'b';

	memset(dt + DT_PARTITIONS_OFF, 0xff, LABEL_PART_COUNT * DT_PARTITION_SIZE);
	part = dt + DT_PARTITIONS_OFF;
	memset(part, 0, DT_PARTITION_SIZE);
	write_be24(part + PART_BASE_OFF, 0);
	write_be24(part + PART_SIZE_OFF, slice_blocks);
	write_be16(part + PART_BSIZE_OFF, 8192);
	write_be16(part + PART_FSIZE_OFF, 1024);
	part[PART_OPT_OFF] = 't';
	write_be16(part + PART_CPG_OFF, 16);
	write_be16(part + PART_DENSITY_OFF, 4096);
	part[PART_MINFREE_OFF] = 10;
	part[PART_NEWFS_OFF] = 1;
	part[PART_AUTOMNT_OFF] = 1;
	put_string(part + PART_TYPE_OFF, 8, "4.3BSD");

	memset(buf + 0x04, 0, 4);
	memset(buf + DL_V3_CKSUM_OFF, 0, 2);
	write_be16(buf + DL_V3_CKSUM_OFF, checksum_be16(buf, LABEL_CKSUM_SIZE));
	write_be32(buf + 0x04, (uint32_t)(off / DEV_SECTOR_SIZE));

	if (pwrite(fd, buf, sizeof(buf), off) != (ssize_t)sizeof(buf))
		return -1;
	return 0;
}

static int
write_labels(const struct options *opts, uint64_t slice_bytes)
{
	uint32_t total_blocks = (uint32_t)(opts->bytes / UFS_SECTOR_SIZE);
	uint32_t slice_blocks = (uint32_t)(slice_bytes / UFS_SECTOR_SIZE);
	uint32_t ncyl32 = total_blocks / (4U * 32U);
	uint16_t ncyl;
	int fd;
	unsigned i;

	if (total_blocks > 0xffffffU || slice_blocks > 0xffffffU) {
		fprintf(stderr, "nextufs.mkimg: image is too large for the NeXT label fields\n");
		return -1;
	}
	if (ncyl32 > 0xffffU) {
		fprintf(stderr, "nextufs.mkimg: image is too large for the NeXT geometry fields\n");
		return -1;
	}
	ncyl = (uint16_t)ncyl32;
	fd = open(opts->target, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "nextufs.mkimg: cannot open %s: %s\n",
		    opts->target, strerror(errno));
		return -1;
	}
	for (i = 0; i < LABEL_COPY_COUNT; i++) {
		if (write_label_copy(fd, (off_t)(i * LABEL_COPY_STRIDE),
		    total_blocks, slice_blocks, ncyl, opts->label) < 0) {
			fprintf(stderr, "nextufs.mkimg: failed to write disk label copy %u\n", i);
			close(fd);
			return -1;
		}
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
		printf("front porch: %u bytes\n", FRONT_PORCH_BYTES);
		printf("root slice size: %" PRIu64 " bytes (%" PRIu64 " 1K sectors)\n",
		    slice_bytes, slice_bytes / 1024U);
		printf("geometry: 4 tracks, 32 sectors/track, 1024-byte sectors\n");
	}
}

int
main(int argc, char **argv)
{
	struct options opts;
	uint64_t slice_bytes;
	uint64_t fs_sectors;
	char sectors_arg[32];
	char *format_argv[32];
	int format_argc = 0;

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
	if (!opts.raw && opts.bytes <= FRONT_PORCH_BYTES) {
		fprintf(stderr, "nextufs.mkimg: labeled image is too small for the front porch\n");
		return 1;
	}

	slice_bytes = opts.raw ? opts.bytes : opts.bytes - FRONT_PORCH_BYTES;
	fs_sectors = slice_bytes / UFS_SECTOR_SIZE;
	if (fs_sectors > LONG_MAX) {
		fprintf(stderr, "nextufs.mkimg: filesystem is too large\n");
		return 1;
	}
	print_plan(&opts, slice_bytes);
	if (opts.dry_run)
		return 0;
	if (create_output(&opts) < 0)
		return 1;
	if (!opts.raw && write_labels(&opts, slice_bytes) < 0)
		return 1;

	snprintf(sectors_arg, sizeof(sectors_arg), "%" PRIu64, fs_sectors);
	format_base_offset = opts.raw ? 0 : FRONT_PORCH_BYTES;
	format_no_create = 1;
	format_argv[format_argc++] = "nextufs.mkimg";
	if (opts.force_size)
		format_argv[format_argc++] = "--force-size";
	format_argv[format_argc++] = (char *)opts.target;
	format_argv[format_argc++] = sectors_arg;
	if (opts.raw) {
		int i;

		if (format_argc + opts.extra_argc + 1 >
		    (int)(sizeof(format_argv) / sizeof(format_argv[0]))) {
			fprintf(stderr, "nextufs.mkimg: too many raw geometry arguments\n");
			return 1;
		}
		for (i = 0; i < opts.extra_argc; i++)
			format_argv[format_argc++] = opts.extra_argv[i];
		format_argv[format_argc] = NULL;
		return nextufs_format_main(format_argc, format_argv);
	}
	format_argv[format_argc++] = "32";
	format_argv[format_argc++] = "4";
	format_argv[format_argc++] = "8192";
	format_argv[format_argc++] = "1024";
	format_argv[format_argc++] = "16";
	format_argv[format_argc++] = "10";
	format_argv[format_argc++] = "60";
	format_argv[format_argc++] = "4096";
	format_argv[format_argc++] = "t";
	format_argv[format_argc] = NULL;
	return nextufs_format_main(format_argc, format_argv);
}
