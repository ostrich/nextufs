#define _FILE_OFFSET_BITS 64

#include "nextufs_label.h"
#include "nextufs_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#define DEV_SECTOR_SIZE 512U
#define LABEL_COPY_COUNT 4U
#define LABEL_COPY_STRIDE 7680U
#define LABEL_VALIDATE_SIZE 0x660U
#define LABEL_WRITE_SIZE 0x300U
#define LABEL_CKSUM_SIZE 0x230U
#define DL_V1 0x4e655854U
#define DL_V2 0x646c5632U
#define DL_V3 0x646c5633U
#define DL_DISKTAB_OFF 0x02cU
#define DL_V3_CKSUM_OFF 0x022eU
#define DL_CKSUM_OFF 0x1c58U
#define DL_V3_CKSUM_SIZE 0x0230U
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
#define PART_UNUSED_BASE 0xffffffU
#define PART_UNUSED_SIZE 0xffffffU

static uint32_t
read_be24(const uint8_t *p)
{
	return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}

static void
write_be24(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 16);
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)v;
}

static uint16_t
checksum_be16(const uint8_t *buf, size_t size)
{
	uint32_t sum = 0;
	size_t i;

	for (i = 0; i + 1 < size; i += 2) {
		sum += nextufs__read_be16(buf + i);
		if (sum > 65535U)
			sum -= 65535U;
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

static void
copy_cstr_field(char *dst, size_t dst_size, const uint8_t *src, size_t src_size)
{
	size_t n = 0;

	while (n < src_size && src[n] != '\0')
		n++;
	if (n >= dst_size)
		n = dst_size - 1;
	memcpy(dst, src, n);
	dst[n] = '\0';
}

static int
label_partition_present(const uint8_t *part)
{
	uint32_t base = read_be24(part + PART_BASE_OFF);
	uint32_t size = read_be24(part + PART_SIZE_OFF);
	uint16_t bsize = nextufs__read_be16(part + PART_BSIZE_OFF);
	uint16_t fsize = nextufs__read_be16(part + PART_FSIZE_OFF);
	const uint8_t *type = part + PART_TYPE_OFF;
	size_t i;
	int type_empty = 1;

	for (i = 0; i < NEXTUFS_LABEL_FSTYPE_LEN; i++) {
		if (type[i] != '\0') {
			type_empty = 0;
			break;
		}
	}
	if (base == PART_UNUSED_BASE || size == 0 || size == PART_UNUSED_SIZE ||
	    bsize == 0 || bsize == 0xffffU || fsize == 0 || fsize == 0xffffU ||
	    type_empty)
		return 0;
	return 1;
}

int
nextufs_label_is_version(uint32_t version)
{
	return version == DL_V1 || version == DL_V2 || version == DL_V3;
}

int
nextufs_label_decode(struct nextufs_disk_label *dl, const uint8_t *buf,
    size_t size, off_t off)
{
	size_t i;
	uint8_t tmp[DL_V3_CKSUM_SIZE];
	const uint8_t *dt;

	if (size < NEXTUFS_LABEL_DECODE_SIZE)
		return -1;
	memset(dl, 0, sizeof(*dl));
	dl->version = nextufs__read_be32(buf + 0x00);
	if (!nextufs_label_is_version(dl->version))
		return -1;
	dl->label_off = off;
	dl->label_blkno = nextufs__read_be32(buf + 0x04);
	copy_cstr_field(dl->name, sizeof(dl->name), buf + 0x0c,
	    NEXTUFS_LABEL_NAME_LEN);
	dt = buf + DL_DISKTAB_OFF;
	dl->secsize = nextufs__read_be16(dt + DT_SECSIZE_OFF);
	dl->front = nextufs__read_be16(dt + DT_FRONT_OFF);
	dl->rootpartition = (char)dt[DT_ROOTPART_OFF];
	if (dl->version == DL_V3) {
		dl->checksum = nextufs__read_be16(buf + DL_V3_CKSUM_OFF);
		dl->checksum_present = dl->checksum != 0;
		memcpy(tmp, buf, sizeof(tmp));
		memset(tmp + 0x04, 0, 4);
		memset(tmp + DL_V3_CKSUM_OFF, 0, 2);
		dl->checksum_valid =
		    checksum_be16(tmp, sizeof(tmp)) == dl->checksum;
	} else {
		dl->checksum = nextufs__read_be16(buf + DL_CKSUM_OFF);
		dl->checksum_present = dl->checksum != 0;
		dl->checksum_valid = 0;
	}
	for (i = 0; i < NEXTUFS_LABEL_PART_COUNT; i++) {
		const uint8_t *p = dt + DT_PARTITIONS_OFF +
		    (i * DT_PARTITION_SIZE);
		struct nextufs_label_partition *part = &dl->part[i];
		int all_zero = 1;
		int all_ff = 1;
		size_t j;

		for (j = 0; j < DT_PARTITION_SIZE; j++) {
			if (p[j] != 0x00)
				all_zero = 0;
			if (p[j] != 0xff)
				all_ff = 0;
		}
		part->base_blocks = read_be24(p + PART_BASE_OFF);
		part->size_blocks = read_be24(p + PART_SIZE_OFF);
		part->block_size = nextufs__read_be16(p + PART_BSIZE_OFF);
		part->frag_size = nextufs__read_be16(p + PART_FSIZE_OFF);
		copy_cstr_field(part->type, sizeof(part->type),
		    p + PART_TYPE_OFF, NEXTUFS_LABEL_FSTYPE_LEN);
		part->present = !all_zero && !all_ff &&
		    part->base_blocks != 0xffffffU &&
		    part->size_blocks != 0 &&
		    part->size_blocks != 0xffffffU &&
		    part->block_size != 0 &&
		    part->block_size != 0xffff &&
		    part->frag_size != 0 &&
		    part->frag_size != 0xffff &&
		    part->block_size >= part->frag_size &&
		    part->type[0] != '\0';
	}
	if (dl->secsize == 0 ||
	    (dl->secsize % NEXTUFS_LABEL_DEV_SECTOR_SIZE) != 0)
		return -1;
	if ((uint64_t)dl->label_blkno * NEXTUFS_LABEL_DEV_SECTOR_SIZE !=
	    (uint64_t)off)
		return -1;
	if (dl->checksum_present && !dl->checksum_valid)
		return -1;
	return 0;
}

int
nextufs_label_pick_slice(const struct nextufs_disk_label *dl,
    off_t *slice_base_out, off_t *slice_size_out)
{
	size_t i;

	if (dl->rootpartition >= 'a' &&
	    dl->rootpartition < 'a' + NEXTUFS_LABEL_PART_COUNT) {
		i = (size_t)(dl->rootpartition - 'a');
		if (dl->part[i].present && dl->part[i].size_blocks != 0) {
			*slice_base_out =
			    (off_t)(((uint64_t)dl->front +
			    dl->part[i].base_blocks) * dl->secsize);
			if (slice_size_out != NULL)
				*slice_size_out =
				    (off_t)((uint64_t)dl->part[i].size_blocks *
				    dl->secsize);
			return 0;
		}
	}
	for (i = 0; i < NEXTUFS_LABEL_PART_COUNT; i++) {
		if (!dl->part[i].present || dl->part[i].size_blocks == 0)
			continue;
		if (strcmp(dl->part[i].type, "4.3BSD") != 0)
			continue;
		*slice_base_out = (off_t)(((uint64_t)dl->front +
		    dl->part[i].base_blocks) * dl->secsize);
		if (slice_size_out != NULL)
			*slice_size_out =
			    (off_t)((uint64_t)dl->part[i].size_blocks *
			    dl->secsize);
		return 0;
	}
	return -1;
}

static int
write_label_copy(int fd, off_t off, uint32_t total_blocks,
    uint32_t slice_blocks, uint16_t ncyl, const char *label)
{
	uint8_t buf[LABEL_WRITE_SIZE];
	uint8_t *dt;
	uint8_t *part;

	memset(buf, 0, sizeof(buf));
	nextufs__write_be32(buf + 0x00, DL_V3);
	nextufs__write_be32(buf + 0x04, (uint32_t)(off / DEV_SECTOR_SIZE));
	nextufs__write_be32(buf + 0x08, total_blocks);
	put_string(buf + 0x0c, 24, label);

	dt = buf + DL_DISKTAB_OFF;
	put_string(dt + DT_NAME_OFF, 24, "nextufs");
	put_string(dt + DT_TYPE_OFF, 24, "fixed_rw_scsi");
	nextufs__write_be16(dt + DT_SECSIZE_OFF, NEXTUFS_LABEL_SECTOR_SIZE);
	nextufs__write_be16(dt + DT_NTRACKS_OFF, 4);
	nextufs__write_be16(dt + DT_NSECTORS_OFF, 32);
	nextufs__write_be16(dt + DT_NCYLINDERS_OFF, ncyl);
	nextufs__write_be16(dt + DT_RPM_OFF, 3600);
	nextufs__write_be16(dt + DT_FRONT_OFF,
	    NEXTUFS_LABEL_FRONT_PORCH_BYTES / NEXTUFS_LABEL_SECTOR_SIZE);
	nextufs__write_be16(dt + DT_BACK_OFF, 0);
	nextufs__write_be32(dt + DT_BOOT0_BLKNO_OFF, 32);
	nextufs__write_be32(dt + DT_BOOT0_BLKNO_OFF + 4, 96);
	put_string(dt + DT_BOOTFILE_OFF, 24, "sdmach");
	put_string(dt + DT_HOSTNAME_OFF, 32, "localhost");
	dt[DT_ROOTPART_OFF] = 'a';
	dt[DT_RWPART_OFF] = 'b';

	memset(dt + DT_PARTITIONS_OFF, 0xff,
	    NEXTUFS_LABEL_PART_COUNT * DT_PARTITION_SIZE);
	part = dt + DT_PARTITIONS_OFF;
	memset(part, 0, DT_PARTITION_SIZE);
	write_be24(part + PART_BASE_OFF, 0);
	write_be24(part + PART_SIZE_OFF, slice_blocks);
	nextufs__write_be16(part + PART_BSIZE_OFF, 8192);
	nextufs__write_be16(part + PART_FSIZE_OFF, 1024);
	part[PART_OPT_OFF] = 't';
	nextufs__write_be16(part + PART_CPG_OFF, 16);
	nextufs__write_be16(part + PART_DENSITY_OFF, 4096);
	part[PART_MINFREE_OFF] = 10;
	part[PART_NEWFS_OFF] = 1;
	part[PART_AUTOMNT_OFF] = 1;
	put_string(part + PART_TYPE_OFF, 8, "4.3BSD");

	memset(buf + 0x04, 0, 4);
	memset(buf + DL_V3_CKSUM_OFF, 0, 2);
	nextufs__write_be16(buf + DL_V3_CKSUM_OFF,
	    checksum_be16(buf, LABEL_CKSUM_SIZE));
	nextufs__write_be32(buf + 0x04, (uint32_t)(off / DEV_SECTOR_SIZE));

	return nextufs__write_exact_fd(fd, buf, sizeof(buf), off);
}

int
nextufs_label_write_path(const char *target, uint64_t image_bytes,
    uint64_t slice_bytes, const char *label)
{
	uint32_t total_blocks = (uint32_t)(image_bytes / NEXTUFS_LABEL_SECTOR_SIZE);
	uint32_t slice_blocks = (uint32_t)(slice_bytes / NEXTUFS_LABEL_SECTOR_SIZE);
	uint32_t ncyl32 = total_blocks / (4U * 32U);
	uint16_t ncyl;
	int fd;
	unsigned i;
	int rc = 0;

	if (total_blocks > 0xffffffU || slice_blocks > 0xffffffU)
		return -EFBIG;
	if (ncyl32 > 0xffffU)
		return -EFBIG;
	ncyl = (uint16_t)ncyl32;
	fd = open(target, O_RDWR);
	if (fd < 0)
		return -errno;
	for (i = 0; i < LABEL_COPY_COUNT; i++) {
		rc = write_label_copy(fd, (off_t)(i * LABEL_COPY_STRIDE),
		    total_blocks, slice_blocks, ncyl, label);
		if (rc < 0)
			break;
	}
	close(fd);
	return rc;
}

static int
validate_one_label_copy(int fd, off_t label_off, off_t slice_base,
    int *matched_out)
{
	uint8_t label[LABEL_VALIDATE_SIZE];
	const uint8_t *dt;
	const uint8_t *root_part;
	uint32_t version;
	uint32_t label_blkno;
	uint32_t secsize;
	uint16_t front;
	char rootpartition;
	int root_index;
	int present_count = 0;
	int i;
	int rc;

	*matched_out = 0;
	rc = nextufs__read_exact_fd(fd, label, sizeof(label), label_off);
	if (rc < 0)
		return rc;
	version = nextufs__read_be32(label);
	if (version != DL_V1 && version != DL_V2 && version != DL_V3)
		return 0;
	label_blkno = nextufs__read_be32(label + 0x04);
	if ((uint64_t)label_blkno * DEV_SECTOR_SIZE != (uint64_t)label_off)
		return 0;
	dt = label + DL_DISKTAB_OFF;
	secsize = nextufs__read_be16(dt + DT_SECSIZE_OFF);
	front = nextufs__read_be16(dt + DT_FRONT_OFF);
	rootpartition = (char)dt[DT_ROOTPART_OFF];
	if (rootpartition < 'a' ||
	    rootpartition >= 'a' + NEXTUFS_LABEL_PART_COUNT)
		return -EINVAL;
	root_index = rootpartition - 'a';
	for (i = 0; i < NEXTUFS_LABEL_PART_COUNT; i++) {
		const uint8_t *part = dt + DT_PARTITIONS_OFF +
		    ((size_t)i * DT_PARTITION_SIZE);

		if (label_partition_present(part))
			present_count++;
	}
	root_part = dt + DT_PARTITIONS_OFF +
	    ((size_t)root_index * DT_PARTITION_SIZE);
	if (!label_partition_present(root_part))
		return -EINVAL;
	if (present_count != 1)
		return -ENOTSUP;
	if (strcmp((const char *)(root_part + PART_TYPE_OFF), "4.3BSD") != 0)
		return -ENOTSUP;
	if (((uint64_t)front + read_be24(root_part + PART_BASE_OFF)) *
	    secsize != (uint64_t)slice_base)
		return -EINVAL;
	*matched_out = 1;
	return 0;
}

int
nextufs_label_validate_single_slice_fd(int fd, off_t slice_base)
{
	off_t off;
	int matched = 0;
	int rc;

	for (off = 0; off + LABEL_VALIDATE_SIZE <= NEXTUFS_LABEL_SCAN_LIMIT;
	    off += DEV_SECTOR_SIZE) {
		int copy_matched;

		rc = validate_one_label_copy(fd, off, slice_base, &copy_matched);
		if (rc < 0)
			return rc;
		matched += copy_matched;
	}
	return matched == 0 ? -EINVAL : 0;
}

static int
patch_one_label_partition_size(int fd, off_t label_off, off_t slice_base,
    uint64_t slice_bytes, int *patched_out)
{
	uint8_t label[LABEL_VALIDATE_SIZE];
	uint8_t tmp[LABEL_CKSUM_SIZE];
	uint8_t *dt;
	uint8_t *part;
	uint32_t version;
	uint32_t label_blkno;
	uint32_t secsize;
	uint16_t front;
	uint32_t size_blocks;
	char rootpartition;
	int part_index;
	int rc;

	*patched_out = 0;
	rc = nextufs__read_exact_fd(fd, label, sizeof(label), label_off);
	if (rc < 0)
		return rc;
	version = nextufs__read_be32(label);
	if (version != DL_V1 && version != DL_V2 && version != DL_V3)
		return 0;
	label_blkno = nextufs__read_be32(label + 0x04);
	if ((uint64_t)label_blkno * DEV_SECTOR_SIZE != (uint64_t)label_off)
		return 0;
	dt = label + DL_DISKTAB_OFF;
	secsize = nextufs__read_be16(dt + DT_SECSIZE_OFF);
	front = nextufs__read_be16(dt + DT_FRONT_OFF);
	rootpartition = (char)dt[DT_ROOTPART_OFF];
	if (rootpartition < 'a' ||
	    rootpartition >= 'a' + NEXTUFS_LABEL_PART_COUNT ||
	    secsize == 0 || (slice_bytes % secsize) != 0)
		return 0;
	size_blocks = (uint32_t)(slice_bytes / secsize);
	if (size_blocks > 0xffffffU)
		return -EFBIG;
	part_index = rootpartition - 'a';
	part = dt + DT_PARTITIONS_OFF + ((size_t)part_index * DT_PARTITION_SIZE);
	if (((uint64_t)front + read_be24(part + PART_BASE_OFF)) * secsize !=
	    (uint64_t)slice_base)
		return 0;
	write_be24(part + PART_SIZE_OFF, size_blocks);
	if (version == DL_V3) {
		memcpy(tmp, label, sizeof(tmp));
		memset(tmp + 0x04, 0, 4);
		memset(tmp + DL_V3_CKSUM_OFF, 0, 2);
		nextufs__write_be16(label + DL_V3_CKSUM_OFF,
		    checksum_be16(tmp, sizeof(tmp)));
	}
	rc = nextufs__write_exact_fd(fd, label, sizeof(label), label_off);
	if (rc == 0)
		*patched_out = 1;
	return rc;
}

int
nextufs_label_patch_slice_size_fd(int fd, off_t slice_base,
    uint64_t slice_bytes, int *patched_count)
{
	off_t off;
	int patched = 0;
	int rc;

	for (off = 0; off + LABEL_VALIDATE_SIZE <= NEXTUFS_LABEL_SCAN_LIMIT;
	    off += DEV_SECTOR_SIZE) {
		int did_patch;

		rc = patch_one_label_partition_size(fd, off, slice_base,
		    slice_bytes, &did_patch);
		if (rc < 0)
			return rc;
		patched += did_patch;
	}
	if (patched == 0)
		return -EINVAL;
	if (patched_count != NULL)
		*patched_count = patched;
	return 0;
}
