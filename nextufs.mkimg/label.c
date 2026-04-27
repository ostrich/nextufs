#define _FILE_OFFSET_BITS 64

#include "label.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define DEV_SECTOR_SIZE 512U
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
	write_be16(dt + DT_SECSIZE_OFF, NEXTUFS_LABEL_SECTOR_SIZE);
	write_be16(dt + DT_NTRACKS_OFF, 4);
	write_be16(dt + DT_NSECTORS_OFF, 32);
	write_be16(dt + DT_NCYLINDERS_OFF, ncyl);
	write_be16(dt + DT_RPM_OFF, 3600);
	write_be16(dt + DT_FRONT_OFF,
	    NEXTUFS_LABEL_FRONT_PORCH_BYTES / NEXTUFS_LABEL_SECTOR_SIZE);
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

int
nextufs_label_write(const char *target, uint64_t image_bytes,
    uint64_t slice_bytes, const char *label)
{
	uint32_t total_blocks = (uint32_t)(image_bytes / NEXTUFS_LABEL_SECTOR_SIZE);
	uint32_t slice_blocks = (uint32_t)(slice_bytes / NEXTUFS_LABEL_SECTOR_SIZE);
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
	fd = open(target, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "nextufs.mkimg: cannot open %s: %s\n",
		    target, strerror(errno));
		return -1;
	}
	for (i = 0; i < LABEL_COPY_COUNT; i++) {
		if (write_label_copy(fd, (off_t)(i * LABEL_COPY_STRIDE),
		    total_blocks, slice_blocks, ncyl, label) < 0) {
			fprintf(stderr, "nextufs.mkimg: failed to write disk label copy %u\n", i);
			close(fd);
			return -1;
		}
	}
	close(fd);
	return 0;
}
