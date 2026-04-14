#include "nextufs_read_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define SECTOR_SIZE 512
#define UFS_FS_MAGIC 0x00011954U
#define UFS_SBLOCK_OFFSET 0x2000U
#define UFS_SUPER_MAGIC_OFFSET 0x55cU
#define UFS_INODE_SIZE 128U
#define LABEL_SCAN_LIMIT (128U * 1024U)
#define LABEL_PART_COUNT 8
#define LABEL_STR_LEN 24U
#define LABEL_HOST_LEN 32U
#define LABEL_MOUNT_LEN 16U
#define LABEL_FSTYPE_LEN 8U
#define DL_V1 0x4e655854U
#define DL_V2 0x646c5632U
#define DL_V3 0x646c5633U
#define DL_DISKTAB_OFF 0x02cU
#define DL_V3_CKSUM_OFF 0x0240U
#define DL_CKSUM_OFF 0x1c58U
#define DL_V3_CKSUM_SIZE 0x0244U
#define LABEL_DECODE_SIZE (DL_CKSUM_OFF + 2U)
#define DEFAULT_SCAN_LIMIT (32U * 1024U * 1024U)
#define DT_NAME_OFF 0x000U
#define DT_TYPE_OFF 0x018U
#define DT_SECSIZE_OFF 0x032U
#define DT_NTRACKS_OFF 0x036U
#define DT_NSECTORS_OFF 0x03aU
#define DT_NCYLINDERS_OFF 0x03eU
#define DT_RPM_OFF 0x042U
#define DT_FRONT_OFF 0x044U
#define DT_BACK_OFF 0x046U
#define DT_NGROUPS_OFF 0x048U
#define DT_AG_SIZE_OFF 0x04aU
#define DT_AG_ALTS_OFF 0x04cU
#define DT_AG_OFF_OFF 0x052U
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
#define PART_MOUNTPT_OFF 0x12U
#define PART_AUTOMNT_OFF 0x22U
#define PART_TYPE_OFF 0x23U
#define IC_FASTLINK 0x0001U

struct next_partition {
	int present;
	uint32_t base_blocks;
	uint32_t size_blocks;
	uint16_t block_size;
	uint16_t frag_size;
	char type[9];
};

struct next_disk_label {
	off_t label_off;
	uint32_t version;
	uint32_t label_blkno;
	uint32_t secsize;
	uint16_t front;
	char rootpartition;
	struct next_partition part[LABEL_PART_COUNT];
	uint16_t checksum;
	int checksum_valid;
	int checksum_present;
};

static uint16_t read_be16(const uint8_t *p);
static uint32_t read_be24(const uint8_t *p);
static uint32_t read_be32(const uint8_t *p);
static uint64_t read_be64(const uint8_t *p);
static int read_exact(int fd, void *buf, size_t size, off_t offset);
static void copy_cstr_field(char *dst, size_t dst_size, const uint8_t *src, size_t src_size);
static uint16_t checksum_be16(const uint8_t *buf, size_t size);
static void decode_superblock(struct nextufs_superblock *sb, const uint8_t *buf);
static void decode_inode(struct nextufs_inode *ino, const uint8_t *buf);
static int decode_next_disk_label(struct next_disk_label *dl, const uint8_t *buf, off_t off);
static int pick_label_slice(const struct next_disk_label *dl, off_t *slice_base_out,
	off_t *slice_size_out);
static off_t inode_offset_guess(const struct nextufs_image *img, unsigned inode_no);
static int read_indirect_entry(const struct nextufs_image *img, uint32_t block_frag, uint64_t entry_index, uint32_t *entry_out);
static int resolve_indirect_block_frag(const struct nextufs_image *img, uint32_t block_frag, unsigned level, uint64_t logical_index, uint32_t *data_frag_out);
static int resolve_file_block_frag(const struct nextufs_image *img, const struct nextufs_inode *ino, uint64_t logical_block_index, uint32_t *data_frag_out);
static size_t decode_inline_symlink(const struct nextufs_inode *ino, char *out, size_t out_size);

static uint16_t
read_be16(const uint8_t *p)
{
	return ((uint16_t)p[0] << 8) | (uint16_t)p[1];
}

static uint32_t
read_be24(const uint8_t *p)
{
	return ((uint32_t)p[0] << 16) |
	    ((uint32_t)p[1] << 8) |
	    (uint32_t)p[2];
}

static uint32_t
read_be32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) |
	    ((uint32_t)p[1] << 16) |
	    ((uint32_t)p[2] << 8) |
	    (uint32_t)p[3];
}

static uint64_t
read_be64(const uint8_t *p)
{
	return ((uint64_t)read_be32(p) << 32) | read_be32(p + 4);
}

static int
read_exact(int fd, void *buf, size_t size, off_t offset)
{
	uint8_t *out = buf;
	size_t done = 0;

	while (done < size) {
		ssize_t n;

		n = pread(fd, out + done, size - done, offset + (off_t)done);
		if (n < 0)
			return -errno;
		if (n == 0)
			return -EIO;
		done += (size_t)n;
	}
	return 0;
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

static uint16_t
checksum_be16(const uint8_t *buf, size_t size)
{
	uint32_t sum = 0;
	size_t i;

	for (i = 0; i + 1 < size; i += 2) {
		sum += read_be16(buf + i);
		if (sum >= 0x10000U) {
			sum -= 0x10000U;
			sum++;
		}
	}
	return (uint16_t)sum;
}

static void
decode_superblock(struct nextufs_superblock *sb, const uint8_t *buf)
{
	sb->sb_off = read_be32(buf + 0x08);
	sb->cg_off = read_be32(buf + 0x0c);
	sb->ino_off = read_be32(buf + 0x10);
	sb->data_off = read_be32(buf + 0x14);
	sb->cg_delta = read_be32(buf + 0x18);
	sb->cg_cyc_mask = read_be32(buf + 0x1c);
	sb->write_time = read_be32(buf + 0x20);
	sb->frag_count = read_be32(buf + 0x24);
	sb->data_frag_count = read_be32(buf + 0x28);
	sb->cg_count = read_be32(buf + 0x2c);
	sb->block_size = read_be32(buf + 0x30);
	sb->frag_size = read_be32(buf + 0x34);
	sb->frags_per_block = read_be32(buf + 0x38);
	sb->minfree = read_be32(buf + 0x3c);
	sb->rotdelay = read_be32(buf + 0x40);
	sb->rps = read_be32(buf + 0x44);
	sb->block_mask = read_be32(buf + 0x48);
	sb->frag_mask = read_be32(buf + 0x4c);
	sb->block_shift = read_be32(buf + 0x50);
	sb->frag_shift_calc = read_be32(buf + 0x54);
	sb->maxcontig = read_be32(buf + 0x58);
	sb->maxbpg = read_be32(buf + 0x5c);
	sb->frag_shift = read_be32(buf + 0x60);
	sb->fsbtodb = read_be32(buf + 0x64);
	sb->super_size = read_be32(buf + 0x68);
	sb->csum_mask = read_be32(buf + 0x6c);
	sb->csum_shift = read_be32(buf + 0x70);
	sb->nindir = read_be32(buf + 0x74);
	sb->inodes_per_block = read_be32(buf + 0x78);
	sb->sectors_per_frag = read_be32(buf + 0x7c);
	sb->optim = read_be32(buf + 0x80);
	sb->cyl_summary_addr = read_be32(buf + 0x98);
	sb->csum_size = read_be32(buf + 0x9c);
	sb->cg_size = read_be32(buf + 0x0a0);
	sb->tracks_per_cyl = read_be32(buf + 0x0a4);
	sb->sectors_per_track = read_be32(buf + 0x0a8);
	sb->sectors_per_cyl = read_be32(buf + 0x0ac);
	sb->ncyl = read_be32(buf + 0x0b0);
	sb->cpg = read_be32(buf + 0x0b4);
	sb->inodes_per_group = read_be32(buf + 0x0b8);
	sb->frags_per_group = read_be32(buf + 0x0bc);
	sb->dir_count = read_be32(buf + 0x0c0);
	sb->free_block_count = read_be32(buf + 0x0c4);
	sb->free_inode_count = read_be32(buf + 0x0c8);
	sb->free_frag_count = read_be32(buf + 0x0cc);
	sb->state = buf[0x0d1];
	sb->fs_magic = read_be32(buf + UFS_SUPER_MAGIC_OFFSET);
}

static void
decode_inode(struct nextufs_inode *ino, const uint8_t *buf)
{
	size_t i;

	ino->mode = read_be16(buf + 0x00);
	ino->nlink = read_be16(buf + 0x02);
	ino->uid = read_be16(buf + 0x04);
	ino->gid = read_be16(buf + 0x06);
	ino->size = read_be64(buf + 0x08);
	ino->atime = read_be32(buf + 0x10);
	ino->mtime = read_be32(buf + 0x18);
	ino->ctime = read_be32(buf + 0x20);
	for (i = 0; i < 12; i++)
		ino->db[i] = read_be32(buf + 0x28 + (i * 4));
	for (i = 0; i < 3; i++)
		ino->ib[i] = read_be32(buf + 0x58 + (i * 4));
	ino->flags = read_be32(buf + 0x64);
	ino->blocks = read_be32(buf + 0x68);
	ino->gen = read_be32(buf + 0x6c);
}

static int
decode_next_disk_label(struct next_disk_label *dl, const uint8_t *buf, off_t off)
{
	size_t i;
	uint8_t tmp[DL_V3_CKSUM_SIZE];
	const uint8_t *dt;

	memset(dl, 0, sizeof(*dl));
	dl->version = read_be32(buf + 0x00);
	if (dl->version != DL_V1 && dl->version != DL_V2 && dl->version != DL_V3)
		return -1;
	dl->label_off = off;
	dl->label_blkno = read_be32(buf + 0x04);
	dt = buf + DL_DISKTAB_OFF;
	dl->secsize = read_be16(dt + DT_SECSIZE_OFF);
	dl->front = read_be16(dt + DT_FRONT_OFF);
	dl->rootpartition = (char)dt[DT_ROOTPART_OFF];
	if (dl->version == DL_V3) {
		dl->checksum = read_be16(buf + DL_V3_CKSUM_OFF);
		dl->checksum_present = dl->checksum != 0;
		memcpy(tmp, buf, sizeof(tmp));
		memset(tmp + 0x04, 0, 4);
		memset(tmp + DL_V3_CKSUM_OFF, 0, 2);
		dl->checksum_valid = checksum_be16(tmp, sizeof(tmp)) == dl->checksum;
	} else {
		dl->checksum = read_be16(buf + DL_CKSUM_OFF);
		dl->checksum_present = dl->checksum != 0;
		dl->checksum_valid = 0;
	}
	for (i = 0; i < LABEL_PART_COUNT; i++) {
		const uint8_t *p = dt + DT_PARTITIONS_OFF + (i * DT_PARTITION_SIZE);
		struct next_partition *part = &dl->part[i];
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
		part->block_size = read_be16(p + PART_BSIZE_OFF);
		part->frag_size = read_be16(p + PART_FSIZE_OFF);
		copy_cstr_field(part->type, sizeof(part->type), p + PART_TYPE_OFF, LABEL_FSTYPE_LEN);
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
	if (dl->secsize == 0 || (dl->secsize % SECTOR_SIZE) != 0)
		return -1;
	if ((uint64_t)dl->label_blkno * SECTOR_SIZE != (uint64_t)off)
		return -1;
	if (dl->checksum_present && !dl->checksum_valid)
		return -1;
	return 0;
}

static int
pick_label_slice(const struct next_disk_label *dl, off_t *slice_base_out,
    off_t *slice_size_out)
{
	size_t i;

	if (dl->rootpartition >= 'a' &&
	    dl->rootpartition < 'a' + LABEL_PART_COUNT) {
		i = (size_t)(dl->rootpartition - 'a');
		if (dl->part[i].present && dl->part[i].size_blocks != 0) {
			*slice_base_out =
			    (off_t)(((uint64_t)dl->front + dl->part[i].base_blocks) *
			    dl->secsize);
			if (slice_size_out != NULL)
				*slice_size_out = (off_t)((uint64_t)dl->part[i].size_blocks *
				    dl->secsize);
			return 0;
		}
	}
	for (i = 0; i < LABEL_PART_COUNT; i++) {
		if (!dl->part[i].present || dl->part[i].size_blocks == 0)
			continue;
		if (strcmp(dl->part[i].type, "4.3BSD") != 0)
			continue;
		*slice_base_out =
		    (off_t)(((uint64_t)dl->front + dl->part[i].base_blocks) *
		    dl->secsize);
		if (slice_size_out != NULL)
			*slice_size_out = (off_t)((uint64_t)dl->part[i].size_blocks *
			    dl->secsize);
		return 0;
	}
	return -1;
}

static off_t
inode_offset_guess(const struct nextufs_image *img, unsigned inode_no)
{
	uint64_t cg;
	uint64_t cgbase;
	uint64_t cgstart;
	uint64_t cgimin;
	uint64_t frag_addr;
	uint64_t inum_in_group;
	uint64_t inode_slot;
	const struct nextufs_superblock *sb = &img->sb;

	if (sb->inodes_per_group == 0 || sb->frags_per_group == 0 ||
	    sb->inodes_per_block == 0)
		return -1;
	cg = inode_no / sb->inodes_per_group;
	inum_in_group = inode_no % sb->inodes_per_group;
	cgbase = (uint64_t)sb->frags_per_group * cg;
	cgstart = cgbase + ((uint64_t)sb->cg_delta *
	    (cg & (uint64_t)(~sb->cg_cyc_mask)));
	cgimin = cgstart + sb->ino_off;
	frag_addr = cgimin + ((inum_in_group / sb->inodes_per_block) << sb->frag_shift);
	inode_slot = inum_in_group % sb->inodes_per_block;
	return img->slice_base + (off_t)(frag_addr * sb->frag_size) +
	    (off_t)(inode_slot * UFS_INODE_SIZE);
}

int
nextufs_read_inode_by_number(const struct nextufs_image *img, unsigned inode_no,
    struct nextufs_inode *ino, off_t *ino_off)
{
	uint8_t ibuf[UFS_INODE_SIZE];
	off_t off;
	int rc;

	off = inode_offset_guess(img, inode_no);
	if (off < 0)
		return -EINVAL;
	rc = read_exact(img->fd, ibuf, sizeof(ibuf), off);
	if (rc < 0)
		return rc;
	decode_inode(ino, ibuf);
	if (ino_off != NULL)
		*ino_off = off;
	return 0;
}

static int
read_indirect_entry(const struct nextufs_image *img, uint32_t block_frag,
    uint64_t entry_index, uint32_t *entry_out)
{
	uint8_t entry_buf[4];
	uint64_t entries_per_block;
	off_t entry_off;

	entries_per_block = img->sb.block_size / sizeof(uint32_t);
	if (block_frag == 0 || entry_index >= entries_per_block)
		return -EINVAL;
	entry_off = img->slice_base + ((off_t)block_frag * img->sb.frag_size) +
	    (off_t)(entry_index * sizeof(uint32_t));
	if (read_exact(img->fd, entry_buf, sizeof(entry_buf), entry_off) < 0)
		return -EIO;
	*entry_out = read_be32(entry_buf);
	return 0;
}

static int
resolve_indirect_block_frag(const struct nextufs_image *img, uint32_t block_frag,
    unsigned level, uint64_t logical_index, uint32_t *data_frag_out)
{
	uint64_t entries_per_block;
	uint64_t span;
	uint64_t entry_index;
	uint64_t remainder;
	uint32_t next_frag;
	unsigned i;

	entries_per_block = img->sb.block_size / sizeof(uint32_t);
	if (block_frag == 0 || level == 0 || entries_per_block == 0)
		return -EINVAL;
	span = 1;
	for (i = 1; i < level; i++)
		span *= entries_per_block;
	entry_index = logical_index / span;
	remainder = logical_index % span;
	if (read_indirect_entry(img, block_frag, entry_index, &next_frag) < 0)
		return -EIO;
	if (level == 1) {
		*data_frag_out = next_frag;
		return 0;
	}
	if (next_frag == 0) {
		*data_frag_out = 0;
		return 0;
	}
	return resolve_indirect_block_frag(img, next_frag, level - 1, remainder, data_frag_out);
}

static int
resolve_file_block_frag(const struct nextufs_image *img,
    const struct nextufs_inode *ino, uint64_t logical_block_index,
    uint32_t *data_frag_out)
{
	uint64_t entries_per_block;
	uint64_t span;
	uint64_t indirect_index;

	if (logical_block_index < 12) {
		*data_frag_out = ino->db[logical_block_index];
		return 0;
	}
	logical_block_index -= 12;
	entries_per_block = img->sb.block_size / sizeof(uint32_t);
	span = entries_per_block;
	for (indirect_index = 0; indirect_index < 3; indirect_index++) {
		if (logical_block_index < span)
			return resolve_indirect_block_frag(img, ino->ib[indirect_index],
			    (unsigned)indirect_index + 1, logical_block_index,
			    data_frag_out);
		logical_block_index -= span;
		span *= entries_per_block;
	}
	return -EINVAL;
}

int
nextufs_read_inode_data(const struct nextufs_image *img,
    const struct nextufs_inode *ino, uint64_t start, uint8_t *buf,
    size_t buf_size, size_t *bytes_read)
{
	uint64_t file_remaining;
	uint64_t logical_block_index;
	size_t done;
	size_t block_offset;

	if (start >= ino->size) {
		if (bytes_read != NULL)
			*bytes_read = 0;
		return 0;
	}
	file_remaining = ino->size - start;
	if (file_remaining > buf_size)
		file_remaining = buf_size;
	done = 0;
	logical_block_index = start / img->sb.block_size;
	block_offset = (size_t)(start % img->sb.block_size);
	while (file_remaining > 0) {
		size_t chunk_size;
		uint32_t data_frag;
		int rc;

		chunk_size = img->sb.block_size - block_offset;
		if (chunk_size > file_remaining)
			chunk_size = (size_t)file_remaining;
		rc = resolve_file_block_frag(img, ino, logical_block_index, &data_frag);
		if (rc < 0)
			return rc;
		if (data_frag == 0) {
			memset(buf + done, 0, chunk_size);
		} else {
			rc = read_exact(img->fd, buf + done, chunk_size,
			    img->slice_base + ((off_t)data_frag * img->sb.frag_size) +
			    (off_t)block_offset);
			if (rc < 0)
				return rc;
		}
		done += chunk_size;
		file_remaining -= chunk_size;
		logical_block_index++;
		block_offset = 0;
	}
	if (bytes_read != NULL)
		*bytes_read = done;
	return 0;
}

static size_t
decode_inline_symlink(const struct nextufs_inode *ino, char *out,
    size_t out_size)
{
	uint8_t raw[60];
	size_t i;
	size_t nbytes;

	if ((ino->mode & NEXTUFS_IFMT) != NEXTUFS_IFLNK ||
	    (ino->flags & IC_FASTLINK) == 0 || ino->size == 0 ||
	    ino->size > sizeof(raw) || out_size == 0)
		return 0;
	for (i = 0; i < 12; i++) {
		raw[(i * 4) + 0] = (uint8_t)(ino->db[i] >> 24);
		raw[(i * 4) + 1] = (uint8_t)(ino->db[i] >> 16);
		raw[(i * 4) + 2] = (uint8_t)(ino->db[i] >> 8);
		raw[(i * 4) + 3] = (uint8_t)ino->db[i];
	}
	for (i = 0; i < 3; i++) {
		raw[48 + (i * 4) + 0] = (uint8_t)(ino->ib[i] >> 24);
		raw[48 + (i * 4) + 1] = (uint8_t)(ino->ib[i] >> 16);
		raw[48 + (i * 4) + 2] = (uint8_t)(ino->ib[i] >> 8);
		raw[48 + (i * 4) + 3] = (uint8_t)ino->ib[i];
	}
	nbytes = (size_t)ino->size;
	if (nbytes >= out_size)
		nbytes = out_size - 1;
	memcpy(out, raw, nbytes);
	out[nbytes] = '\0';
	return nbytes;
}

int
nextufs_read_symlink_target(const struct nextufs_image *img,
    const struct nextufs_inode *ino, char *out, size_t out_size)
{
	size_t got;

	if ((ino->mode & NEXTUFS_IFMT) != NEXTUFS_IFLNK || out_size == 0)
		return -EINVAL;
	if (decode_inline_symlink(ino, out, out_size) != 0)
		return 0;
	if (nextufs_read_inode_data(img, ino, 0, (uint8_t *)out, out_size - 1,
	    &got) < 0)
		return -EIO;
	out[got] = '\0';
	return 0;
}

int
nextufs_open_image(struct nextufs_image *img, const char *path)
{
	struct stat st;
	uint8_t *scanbuf;
	size_t scan_size;
	size_t label_scan_size;
	size_t off;
	int fd;

	memset(img, 0, sizeof(*img));
	img->fd = -1;
	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -errno;
	if (fstat(fd, &st) < 0) {
		close(fd);
		return -errno;
	}
	img->image_size = st.st_size;
	scan_size = DEFAULT_SCAN_LIMIT;
	if ((off_t)scan_size > st.st_size)
		scan_size = (size_t)st.st_size;
	scanbuf = malloc(scan_size);
	if (scanbuf == NULL) {
		close(fd);
		return -ENOMEM;
	}
	if (read_exact(fd, scanbuf, scan_size, 0) < 0) {
		free(scanbuf);
		close(fd);
		return -EIO;
	}
	label_scan_size = scan_size < LABEL_SCAN_LIMIT ? scan_size : LABEL_SCAN_LIMIT;
	for (off = 0; off + LABEL_DECODE_SIZE <= label_scan_size; off += SECTOR_SIZE) {
		struct next_disk_label dl;
		off_t slice_base;
		off_t slice_size;
		size_t magic_off;
		uint8_t sbuf[2048];
		uint32_t version;

		version = read_be32(scanbuf + off);
		if (version != DL_V1 && version != DL_V2 && version != DL_V3)
			continue;
		if (decode_next_disk_label(&dl, scanbuf + off, (off_t)off) < 0)
			continue;
		if (pick_label_slice(&dl, &slice_base, &slice_size) < 0)
			continue;
		if (slice_base < 0 || slice_base >= st.st_size)
			continue;
		if (slice_size <= 0 || slice_base + slice_size > st.st_size)
			continue;
		magic_off = (size_t)(slice_base + UFS_SBLOCK_OFFSET + UFS_SUPER_MAGIC_OFFSET);
		if (magic_off + 4 > scan_size)
			continue;
		if (read_be32(scanbuf + magic_off) != UFS_FS_MAGIC)
			continue;
		if (read_exact(fd, sbuf, sizeof(sbuf), slice_base + UFS_SBLOCK_OFFSET) < 0)
			continue;
		decode_superblock(&img->sb, sbuf);
		if (img->sb.fs_magic != UFS_FS_MAGIC || img->sb.block_size == 0 ||
		    img->sb.frag_size == 0)
			continue;
		img->fd = fd;
		img->slice_base = slice_base;
		img->slice_size = slice_size;
		img->label_off = dl.label_off;
		img->label_version = dl.version;
		img->label_secsize = dl.secsize;
		img->label_front = dl.front;
		img->rootpartition = dl.rootpartition;
		img->used_disk_label = 1;
		free(scanbuf);
		return 0;
	}
	for (off = 0; off + 4 <= scan_size; off += 4) {
		uint8_t sbuf[2048];

		if (read_be32(scanbuf + off) != UFS_FS_MAGIC)
			continue;
		if ((off_t)off < (off_t)UFS_SUPER_MAGIC_OFFSET)
			continue;
		img->slice_base = (off_t)off - (off_t)UFS_SUPER_MAGIC_OFFSET -
		    (off_t)UFS_SBLOCK_OFFSET;
		if (img->slice_base < 0)
			continue;
		if (read_exact(fd, sbuf, sizeof(sbuf),
		    img->slice_base + UFS_SBLOCK_OFFSET) < 0)
			continue;
		decode_superblock(&img->sb, sbuf);
		if (img->sb.fs_magic != UFS_FS_MAGIC || img->sb.block_size == 0 ||
		    img->sb.frag_size == 0)
			continue;
		img->fd = fd;
		img->slice_size = st.st_size - img->slice_base;
		free(scanbuf);
		return 0;
	}
	free(scanbuf);
	close(fd);
	return -EINVAL;
}

void
nextufs_close_image(struct nextufs_image *img)
{
	if (img->fd >= 0)
		close(img->fd);
	img->fd = -1;
}
