#include "nextufs_internal.h"

#include <errno.h>
#include <dirent.h>
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
#define DL_V3_CKSUM_OFF 0x022eU
#define DL_CKSUM_OFF 0x1c58U
#define DL_V3_CKSUM_SIZE 0x0230U
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
#define VDI_SIGNATURE 0xbeda107fU
#define VDI_TYPE_DYNAMIC 1U
#define VDI_TYPE_DIFF 4U
#define VDI_MAP_UNALLOCATED 0xffffffffU
#define VDI_MAP_ZERO 0xfffffffeU
#define VDI_DESC_SIZE 64U
#define VDI_SIG_OFF 0x40U
#define VDI_VERSION_OFF 0x44U
#define VDI_HEADER_SIZE_OFF 0x48U
#define VDI_TYPE_OFF 0x4cU
#define VDI_BLOCKS_OFFSET_OFF 0x154U
#define VDI_DATA_OFFSET_OFF 0x158U
#define VDI_SECTOR_SIZE_OFF 0x168U
#define VDI_DISK_SIZE_OFF 0x170U
#define VDI_BLOCK_SIZE_OFF 0x178U
#define VDI_BLOCK_EXTRA_OFF 0x17cU
#define VDI_BLOCK_COUNT_OFF 0x180U
#define VDI_BLOCKS_ALLOC_OFF 0x184U
#define VDI_UUID_IMAGE_OFF 0x188U
#define VDI_UUID_PARENT_OFF 0x1a8U

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

struct nextufs_raw_backend {
	int fd;
};

struct nextufs_vdi_backend {
	int fd;
	uint32_t image_type;
	uint32_t map_offset;
	uint32_t data_offset;
	uint32_t sector_size;
	uint32_t block_size;
	uint32_t block_extra;
	uint32_t block_count;
	uint32_t blocks_allocated;
	uint64_t disk_size;
	uint32_t *block_map;
	uint8_t image_uuid[16];
	uint8_t parent_uuid[16];
	int has_parent;
	struct nextufs_image parent;
};

static uint32_t read_be24(const uint8_t *p);
static uint64_t read_be64(const uint8_t *p);
static uint32_t read_le32(const uint8_t *p);
static uint64_t read_le64(const uint8_t *p);
static void copy_cstr_field(char *dst, size_t dst_size, const uint8_t *src, size_t src_size);
static uint16_t checksum_be16(const uint8_t *buf, size_t size);
static void decode_superblock(struct nextufs_superblock *sb, const uint8_t *buf);
static void decode_inode(struct nextufs_inode *ino, const uint8_t *buf);
static int decode_next_disk_label(struct next_disk_label *dl, const uint8_t *buf, off_t off);
static int pick_label_slice(const struct next_disk_label *dl, off_t *slice_base_out,
	off_t *slice_size_out);
static int resolve_indirect_block_frag(const struct nextufs_image *img, uint32_t block_frag, unsigned level, uint64_t logical_index, uint32_t *data_frag_out);
static int resolve_file_block_frag(const struct nextufs_image *img, const struct nextufs_inode *ino, uint64_t logical_block_index, uint32_t *data_frag_out);
static size_t decode_inline_symlink(const struct nextufs_inode *ino, char *out, size_t out_size);
static int nextufs_raw_backend_read(void *ctx, void *buf, size_t size,
	off_t offset);
static int nextufs_raw_backend_write(void *ctx, const void *buf, size_t size,
	off_t offset);
static int nextufs_raw_backend_fsync(void *ctx);
static void nextufs_raw_backend_close(void *ctx);
static int nextufs_vdi_backend_read(void *ctx, void *buf, size_t size,
	off_t offset);
static int nextufs_vdi_backend_write(void *ctx, const void *buf, size_t size,
	off_t offset);
static int nextufs_vdi_backend_fsync(void *ctx);
static void nextufs_vdi_backend_close(void *ctx);
static int nextufs_path_dirname(char *out, size_t out_size, const char *path);
static int nextufs_vdi_find_parent(char *out, size_t out_size,
	const char *path, const uint8_t parent_uuid[16]);
static int nextufs_init_backend(struct nextufs_image *img, const char *path,
	int writable, off_t *source_size_out);
static int nextufs_open_with_mode(struct nextufs_image *img, const char *path,
	int writable);
static int nextufs_write_out_fd(int fd, const void *buf, size_t size);

static const struct nextufs_image_backend_ops nextufs_raw_backend_ops = {
	.read = nextufs_raw_backend_read,
	.write = nextufs_raw_backend_write,
	.fsync = nextufs_raw_backend_fsync,
	.close = nextufs_raw_backend_close,
};

static const struct nextufs_image_backend_ops nextufs_vdi_backend_ops = {
	.read = nextufs_vdi_backend_read,
	.write = nextufs_vdi_backend_write,
	.fsync = nextufs_vdi_backend_fsync,
	.close = nextufs_vdi_backend_close,
};

static int
nextufs_write_out_fd(int fd, const void *buf, size_t size)
{
	const uint8_t *in = buf;
	size_t done = 0;

	while (done < size) {
		ssize_t n = write(fd, in + done, size - done);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -errno;
		}
		if (n == 0)
			return -EIO;
		done += (size_t)n;
	}
	return 0;
}

static uint32_t
read_be24(const uint8_t *p)
{
	return ((uint32_t)p[0] << 16) |
	    ((uint32_t)p[1] << 8) |
	    (uint32_t)p[2];
}

static uint64_t
read_be64(const uint8_t *p)
{
	return ((uint64_t)nextufs__read_be32(p) << 32) |
	    nextufs__read_be32(p + 4);
}

static uint32_t
read_le32(const uint8_t *p)
{
	return (uint32_t)p[0] |
	    ((uint32_t)p[1] << 8) |
	    ((uint32_t)p[2] << 16) |
	    ((uint32_t)p[3] << 24);
}

static uint64_t
read_le64(const uint8_t *p)
{
	return (uint64_t)read_le32(p) |
	    ((uint64_t)read_le32(p + 4) << 32);
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
		sum += nextufs__read_be16(buf + i);
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
	sb->sb_off = nextufs__read_be32(buf + 0x08);
	sb->cg_off = nextufs__read_be32(buf + 0x0c);
	sb->ino_off = nextufs__read_be32(buf + 0x10);
	sb->data_off = nextufs__read_be32(buf + 0x14);
	sb->cg_delta = nextufs__read_be32(buf + 0x18);
	sb->cg_cyc_mask = nextufs__read_be32(buf + 0x1c);
	sb->write_time = nextufs__read_be32(buf + 0x20);
	sb->frag_count = nextufs__read_be32(buf + 0x24);
	sb->data_frag_count = nextufs__read_be32(buf + 0x28);
	sb->cg_count = nextufs__read_be32(buf + 0x2c);
	sb->block_size = nextufs__read_be32(buf + 0x30);
	sb->frag_size = nextufs__read_be32(buf + 0x34);
	sb->frags_per_block = nextufs__read_be32(buf + 0x38);
	sb->minfree = nextufs__read_be32(buf + 0x3c);
	sb->rotdelay = nextufs__read_be32(buf + 0x40);
	sb->rps = nextufs__read_be32(buf + 0x44);
	sb->block_mask = nextufs__read_be32(buf + 0x48);
	sb->frag_mask = nextufs__read_be32(buf + 0x4c);
	sb->block_shift = nextufs__read_be32(buf + 0x50);
	sb->frag_shift_calc = nextufs__read_be32(buf + 0x54);
	sb->maxcontig = nextufs__read_be32(buf + 0x58);
	sb->maxbpg = nextufs__read_be32(buf + 0x5c);
	sb->frag_shift = nextufs__read_be32(buf + 0x60);
	sb->fsbtodb = nextufs__read_be32(buf + 0x64);
	sb->super_size = nextufs__read_be32(buf + 0x68);
	sb->csum_mask = nextufs__read_be32(buf + 0x6c);
	sb->csum_shift = nextufs__read_be32(buf + 0x70);
	sb->nindir = nextufs__read_be32(buf + 0x74);
	sb->inodes_per_block = nextufs__read_be32(buf + 0x78);
	sb->sectors_per_frag = nextufs__read_be32(buf + 0x7c);
	sb->optim = nextufs__read_be32(buf + 0x80);
	sb->cyl_summary_addr = nextufs__read_be32(buf + 0x98);
	sb->csum_size = nextufs__read_be32(buf + 0x9c);
	sb->cg_size = nextufs__read_be32(buf + 0x0a0);
	sb->tracks_per_cyl = nextufs__read_be32(buf + 0x0a4);
	sb->sectors_per_track = nextufs__read_be32(buf + 0x0a8);
	sb->sectors_per_cyl = nextufs__read_be32(buf + 0x0ac);
	sb->ncyl = nextufs__read_be32(buf + 0x0b0);
	sb->cpg = nextufs__read_be32(buf + 0x0b4);
	sb->inodes_per_group = nextufs__read_be32(buf + 0x0b8);
	sb->frags_per_group = nextufs__read_be32(buf + 0x0bc);
	sb->dir_count = nextufs__read_be32(buf + 0x0c0);
	sb->free_block_count = nextufs__read_be32(buf + 0x0c4);
	sb->free_inode_count = nextufs__read_be32(buf + 0x0c8);
	sb->free_frag_count = nextufs__read_be32(buf + 0x0cc);
	sb->state = buf[0x0d1];
	sb->fs_magic = nextufs__read_be32(buf + UFS_SUPER_MAGIC_OFFSET);
}

static void
decode_inode(struct nextufs_inode *ino, const uint8_t *buf)
{
	size_t i;

	ino->mode = nextufs__read_be16(buf + 0x00);
	ino->nlink = nextufs__read_be16(buf + 0x02);
	ino->uid = nextufs__read_be16(buf + 0x04);
	ino->gid = nextufs__read_be16(buf + 0x06);
	ino->size = read_be64(buf + 0x08);
	ino->atime = nextufs__read_be32(buf + 0x10);
	ino->mtime = nextufs__read_be32(buf + 0x18);
	ino->ctime = nextufs__read_be32(buf + 0x20);
	for (i = 0; i < 12; i++)
		ino->db[i] = nextufs__read_be32(buf + 0x28 + (i * 4));
	for (i = 0; i < 3; i++)
		ino->ib[i] = nextufs__read_be32(buf + 0x58 + (i * 4));
	ino->flags = nextufs__read_be32(buf + 0x64);
	ino->blocks = nextufs__read_be32(buf + 0x68);
	ino->gen = nextufs__read_be32(buf + 0x6c);
}

static int
decode_next_disk_label(struct next_disk_label *dl, const uint8_t *buf, off_t off)
{
	size_t i;
	uint8_t tmp[DL_V3_CKSUM_SIZE];
	const uint8_t *dt;

	memset(dl, 0, sizeof(*dl));
	dl->version = nextufs__read_be32(buf + 0x00);
	if (dl->version != DL_V1 && dl->version != DL_V2 && dl->version != DL_V3)
		return -1;
	dl->label_off = off;
	dl->label_blkno = nextufs__read_be32(buf + 0x04);
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
		dl->checksum_valid = checksum_be16(tmp, sizeof(tmp)) == dl->checksum;
	} else {
		dl->checksum = nextufs__read_be16(buf + DL_CKSUM_OFF);
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
		part->block_size = nextufs__read_be16(p + PART_BSIZE_OFF);
		part->frag_size = nextufs__read_be16(p + PART_FSIZE_OFF);
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
nextufs_raw_backend_read(void *ctx, void *buf, size_t size, off_t offset)
{
	struct nextufs_raw_backend *raw = ctx;

	return nextufs__read_exact_fd(raw->fd, buf, size, offset);
}

static int
nextufs_raw_backend_write(void *ctx, const void *buf, size_t size, off_t offset)
{
	struct nextufs_raw_backend *raw = ctx;

	return nextufs__write_exact_fd(raw->fd, buf, size, offset);
}

static int
nextufs_raw_backend_fsync(void *ctx)
{
	struct nextufs_raw_backend *raw = ctx;

	return fsync(raw->fd) < 0 ? -errno : 0;
}

static void
nextufs_raw_backend_close(void *ctx)
{
	struct nextufs_raw_backend *raw = ctx;

	if (raw == NULL)
		return;
	if (raw->fd >= 0)
		close(raw->fd);
	free(raw);
}

static int
nextufs_vdi_backend_read(void *ctx, void *buf, size_t size, off_t offset)
{
	struct nextufs_vdi_backend *vdi = ctx;
	uint8_t *out = buf;
	size_t done = 0;

	if (offset < 0 || (uint64_t)offset + size > vdi->disk_size)
		return -EIO;
	while (done < size) {
		uint64_t guest_off = (uint64_t)offset + done;
		uint32_t block_index;
		uint32_t map_entry;
		size_t block_off;
		size_t chunk;

		block_index = (uint32_t)(guest_off / vdi->block_size);
		if (block_index >= vdi->block_count)
			return -EIO;
		block_off = (size_t)(guest_off % vdi->block_size);
		chunk = vdi->block_size - block_off;
		if (chunk > size - done)
			chunk = size - done;
		map_entry = vdi->block_map[block_index];
		if (map_entry == VDI_MAP_UNALLOCATED) {
			if (vdi->has_parent) {
				size_t parent_chunk = chunk;

				if ((uint64_t)guest_off >= (uint64_t)vdi->parent.image_size) {
					memset(out + done, 0, chunk);
				} else {
					uint64_t parent_remaining =
					    (uint64_t)vdi->parent.image_size -
					    (uint64_t)guest_off;

					if (parent_chunk > parent_remaining)
						parent_chunk = (size_t)parent_remaining;
					if (nextufs__read_exact(&vdi->parent, out + done,
					    parent_chunk, (off_t)guest_off) < 0)
						return -EIO;
					if (parent_chunk < chunk) {
						memset(out + done + parent_chunk, 0,
						    chunk - parent_chunk);
					}
				}
			} else {
				memset(out + done, 0, chunk);
			}
		} else if (map_entry == VDI_MAP_ZERO) {
			memset(out + done, 0, chunk);
		} else {
			off_t file_off;
			uint64_t stride;

			stride = (uint64_t)vdi->block_size + vdi->block_extra;
			file_off = (off_t)vdi->data_offset +
			    (off_t)((uint64_t)map_entry * stride) +
			    (off_t)vdi->block_extra + (off_t)block_off;
			if (nextufs__read_exact_fd(vdi->fd, out + done, chunk,
			    file_off) < 0)
				return -EIO;
		}
		done += chunk;
	}
	return 0;
}

static int
nextufs_vdi_write_le32(int fd, off_t off, uint32_t value)
{
	uint8_t raw[4];

	raw[0] = (uint8_t)value;
	raw[1] = (uint8_t)(value >> 8);
	raw[2] = (uint8_t)(value >> 16);
	raw[3] = (uint8_t)(value >> 24);
	return nextufs__write_exact_fd(fd, raw, sizeof(raw), off);
}

static int
nextufs_vdi_materialize_block(struct nextufs_vdi_backend *vdi, uint32_t block_index)
{
	uint32_t map_entry;
	uint32_t new_entry;
	uint64_t guest_off;
	uint64_t stride;
	off_t block_file_off;
	uint8_t *block_buf;
	int rc;

	map_entry = vdi->block_map[block_index];
	if (map_entry != VDI_MAP_UNALLOCATED && map_entry != VDI_MAP_ZERO)
		return 0;
	if (vdi->blocks_allocated >= vdi->block_count)
		return -ENOSPC;
	block_buf = malloc(vdi->block_size == 0 ? 1 : vdi->block_size);
	if (block_buf == NULL)
		return -ENOMEM;
	guest_off = (uint64_t)block_index * vdi->block_size;
	if (map_entry == VDI_MAP_UNALLOCATED && vdi->has_parent) {
		rc = nextufs__read_exact(&vdi->parent, block_buf, vdi->block_size,
		    (off_t)guest_off);
		if (rc < 0) {
			free(block_buf);
			return rc;
		}
	} else {
		memset(block_buf, 0, vdi->block_size);
	}
	new_entry = vdi->blocks_allocated;
	stride = (uint64_t)vdi->block_size + vdi->block_extra;
	block_file_off = (off_t)vdi->data_offset + (off_t)((uint64_t)new_entry * stride);
	if (vdi->block_extra != 0) {
		uint8_t *extra;

		extra = calloc(1, vdi->block_extra);
		if (extra == NULL) {
			free(block_buf);
			return -ENOMEM;
		}
		rc = nextufs__write_exact_fd(vdi->fd, extra, vdi->block_extra,
		    block_file_off);
		free(extra);
		if (rc < 0) {
			free(block_buf);
			return rc;
		}
	}
	rc = nextufs__write_exact_fd(vdi->fd, block_buf, vdi->block_size,
	    block_file_off + (off_t)vdi->block_extra);
	free(block_buf);
	if (rc < 0)
		return rc;
	rc = nextufs_vdi_write_le32(vdi->fd,
	    (off_t)vdi->map_offset + (off_t)block_index * 4, new_entry);
	if (rc < 0)
		return rc;
	vdi->block_map[block_index] = new_entry;
	vdi->blocks_allocated++;
	rc = nextufs_vdi_write_le32(vdi->fd, VDI_BLOCKS_ALLOC_OFF,
	    vdi->blocks_allocated);
	if (rc < 0)
		return rc;
	return 0;
}

static int
nextufs_vdi_backend_write(void *ctx, const void *buf, size_t size, off_t offset)
{
	struct nextufs_vdi_backend *vdi = ctx;
	const uint8_t *in = buf;
	size_t done = 0;

	if (offset < 0 || (uint64_t)offset + size > vdi->disk_size)
		return -EIO;
	while (done < size) {
		uint64_t guest_off = (uint64_t)offset + done;
		uint32_t block_index;
		uint32_t map_entry;
		size_t block_off;
		size_t chunk;
		uint64_t stride;
		off_t file_off;
		int rc;

		block_index = (uint32_t)(guest_off / vdi->block_size);
		if (block_index >= vdi->block_count)
			return -EIO;
		rc = nextufs_vdi_materialize_block(vdi, block_index);
		if (rc < 0)
			return rc;
		map_entry = vdi->block_map[block_index];
		if (map_entry == VDI_MAP_UNALLOCATED || map_entry == VDI_MAP_ZERO)
			return -EIO;
		block_off = (size_t)(guest_off % vdi->block_size);
		chunk = vdi->block_size - block_off;
		if (chunk > size - done)
			chunk = size - done;
		stride = (uint64_t)vdi->block_size + vdi->block_extra;
		file_off = (off_t)vdi->data_offset +
		    (off_t)((uint64_t)map_entry * stride) +
		    (off_t)vdi->block_extra + (off_t)block_off;
		rc = nextufs__write_exact_fd(vdi->fd, in + done, chunk, file_off);
		if (rc < 0)
			return rc;
		done += chunk;
	}
	return 0;
}

static int
nextufs_vdi_backend_fsync(void *ctx)
{
	struct nextufs_vdi_backend *vdi = ctx;

	return fsync(vdi->fd) < 0 ? -errno : 0;
}

static void
nextufs_vdi_backend_close(void *ctx)
{
	struct nextufs_vdi_backend *vdi = ctx;

	if (vdi == NULL)
		return;
	if (vdi->has_parent)
		nextufs_image_close(&vdi->parent);
	if (vdi->fd >= 0)
		close(vdi->fd);
	free(vdi->block_map);
	free(vdi);
}

static int
nextufs_path_dirname(char *out, size_t out_size, const char *path)
{
	const char *slash;
	size_t len;

	slash = strrchr(path, '/');
	if (slash == NULL) {
		if (out_size < 2)
			return -ENAMETOOLONG;
		strcpy(out, ".");
		return 0;
	}
	len = slash == path ? 1 : (size_t)(slash - path);
	if (len + 1 > out_size)
		return -ENAMETOOLONG;
	memcpy(out, path, len);
	out[len] = '\0';
	return 0;
}

static int
nextufs_vdi_uuid_matches(const char *path, const uint8_t target_uuid[16])
{
	uint8_t hdr[VDI_UUID_PARENT_OFF + 16];
	int fd;
	int rc = 0;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return 0;
	if (nextufs__read_exact_fd(fd, hdr, sizeof(hdr), 0) < 0)
		goto out;
	if (read_le32(hdr + VDI_SIG_OFF) != VDI_SIGNATURE)
		goto out;
	rc = memcmp(hdr + VDI_UUID_IMAGE_OFF, target_uuid, 16) == 0;
out:
	close(fd);
	return rc;
}

static int
nextufs_vdi_search_dir(char *out, size_t out_size, const char *dir,
    const char *self_path, const uint8_t parent_uuid[16])
{
	DIR *dp;
	struct dirent *de;

	dp = opendir(dir);
	if (dp == NULL)
		return -errno;
	while ((de = readdir(dp)) != NULL) {
		char candidate[NEXTUFS_MAX_PATH_LEN];
		size_t dir_len;
		size_t name_len;

		if (de->d_name[0] == '.')
			continue;
		name_len = strlen(de->d_name);
		if (name_len < 4 || strcmp(de->d_name + name_len - 4, ".vdi") != 0)
			continue;
		dir_len = strlen(dir);
		if (dir_len + 1 + name_len + 1 > sizeof(candidate))
			continue;
		memcpy(candidate, dir, dir_len);
		candidate[dir_len] = '/';
		memcpy(candidate + dir_len + 1, de->d_name, name_len + 1);
		if (strcmp(candidate, self_path) == 0)
			continue;
		if (!nextufs_vdi_uuid_matches(candidate, parent_uuid))
			continue;
		if (strlen(candidate) + 1 > out_size) {
			closedir(dp);
			return -ENAMETOOLONG;
		}
		memcpy(out, candidate, strlen(candidate) + 1);
		closedir(dp);
		return 0;
	}
	closedir(dp);
	return -ENOENT;
}

static int
nextufs_vdi_find_parent(char *out, size_t out_size, const char *path,
    const uint8_t parent_uuid[16])
{
	char dir[NEXTUFS_MAX_PATH_LEN];
	char parent_dir[NEXTUFS_MAX_PATH_LEN];
	int rc;

	rc = nextufs_path_dirname(dir, sizeof(dir), path);
	if (rc < 0)
		return rc;
	rc = nextufs_vdi_search_dir(out, out_size, dir, path, parent_uuid);
	if (rc == 0)
		return 0;
	rc = nextufs_path_dirname(parent_dir, sizeof(parent_dir), dir);
	if (rc < 0)
		return rc;
	if (strcmp(parent_dir, dir) == 0)
		return -ENOENT;
	return nextufs_vdi_search_dir(out, out_size, parent_dir, path, parent_uuid);
}

static int
nextufs_init_backend(struct nextufs_image *img, const char *path, int writable,
    off_t *source_size_out)
{
	struct stat st;
	uint8_t hdr[512];
	int fd;
	int rc;

	fd = open(path, writable ? O_RDWR : O_RDONLY);
	if (fd < 0)
		return -errno;
	if (fstat(fd, &st) < 0) {
		close(fd);
		return -errno;
	}
	if (nextufs__read_exact_fd(fd, hdr, sizeof(hdr), 0) < 0) {
		close(fd);
		return -EIO;
	}
	if (sizeof(hdr) >= VDI_UUID_PARENT_OFF + 16 &&
	    read_le32(hdr + VDI_SIG_OFF) == VDI_SIGNATURE) {
		struct nextufs_vdi_backend *vdi;
		uint32_t header_size;
		size_t map_bytes;

		header_size = read_le32(hdr + VDI_HEADER_SIZE_OFF);
		if ((uint64_t)VDI_SIG_OFF + header_size < VDI_UUID_PARENT_OFF + 16 ||
		    read_le32(hdr + VDI_VERSION_OFF) != 0x00010001U) {
			close(fd);
			return -EINVAL;
		}
		vdi = calloc(1, sizeof(*vdi));
		if (vdi == NULL) {
			close(fd);
			return -ENOMEM;
		}
		vdi->fd = fd;
		vdi->image_type = read_le32(hdr + VDI_TYPE_OFF);
		vdi->map_offset = read_le32(hdr + VDI_BLOCKS_OFFSET_OFF);
		vdi->data_offset = read_le32(hdr + VDI_DATA_OFFSET_OFF);
		vdi->sector_size = read_le32(hdr + VDI_SECTOR_SIZE_OFF);
		vdi->disk_size = read_le64(hdr + VDI_DISK_SIZE_OFF);
		vdi->block_size = read_le32(hdr + VDI_BLOCK_SIZE_OFF);
		vdi->block_extra = read_le32(hdr + VDI_BLOCK_EXTRA_OFF);
		vdi->block_count = read_le32(hdr + VDI_BLOCK_COUNT_OFF);
		vdi->blocks_allocated = read_le32(hdr + VDI_BLOCKS_ALLOC_OFF);
		memcpy(vdi->image_uuid, hdr + VDI_UUID_IMAGE_OFF, 16);
		memcpy(vdi->parent_uuid, hdr + VDI_UUID_PARENT_OFF, 16);
		if ((vdi->image_type != VDI_TYPE_DYNAMIC &&
		    vdi->image_type != VDI_TYPE_DIFF) ||
		    vdi->sector_size == 0 || vdi->block_size == 0 ||
		    vdi->block_count == 0 || vdi->map_offset < header_size ||
		    vdi->data_offset < vdi->map_offset) {
			nextufs_vdi_backend_close(vdi);
			return -EINVAL;
		}
		map_bytes = (size_t)vdi->block_count * sizeof(uint32_t);
		vdi->block_map = malloc(map_bytes);
		if (vdi->block_map == NULL) {
			nextufs_vdi_backend_close(vdi);
			return -ENOMEM;
		}
		if (nextufs__read_exact_fd(fd, vdi->block_map, map_bytes,
		    (off_t)vdi->map_offset) < 0) {
			nextufs_vdi_backend_close(vdi);
			return -EIO;
		}
		if (vdi->image_type == VDI_TYPE_DIFF) {
			char parent_path[NEXTUFS_MAX_PATH_LEN];

			rc = nextufs_vdi_find_parent(parent_path, sizeof(parent_path),
			    path, vdi->parent_uuid);
			if (rc < 0) {
				nextufs_vdi_backend_close(vdi);
				return rc;
			}
			memset(&vdi->parent, 0, sizeof(vdi->parent));
			vdi->parent.fd = -1;
			rc = nextufs_init_backend(&vdi->parent, parent_path, 0, NULL);
			if (rc < 0) {
				nextufs_vdi_backend_close(vdi);
				return rc;
			}
			vdi->has_parent = 1;
		}
		img->backend_ops = &nextufs_vdi_backend_ops;
		img->backend_ctx = vdi;
		img->writable = writable;
		img->source_is_container = 1;
		img->fd = fd;
		img->image_size = (off_t)vdi->disk_size;
		if (source_size_out != NULL)
			*source_size_out = st.st_size;
		return 0;
	}
	{
		struct nextufs_raw_backend *raw;

		raw = calloc(1, sizeof(*raw));
		if (raw == NULL) {
			close(fd);
			return -ENOMEM;
		}
		raw->fd = fd;
		img->backend_ops = &nextufs_raw_backend_ops;
		img->backend_ctx = raw;
		img->writable = writable;
		img->source_is_container = 0;
		img->fd = fd;
		img->image_size = st.st_size;
		if (source_size_out != NULL)
			*source_size_out = st.st_size;
		return 0;
	}
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

int
nextufs_inode_read(const struct nextufs_image *img, unsigned inode_no,
    struct nextufs_inode *ino, off_t *ino_off)
{
	uint8_t ibuf[UFS_INODE_SIZE];
	off_t off;
	int rc;

	off = nextufs__inode_offset(img, inode_no);
	if (off < 0)
		return -EINVAL;
	rc = nextufs__read_exact(img, ibuf, sizeof(ibuf), off);
	if (rc < 0)
		return rc;
	decode_inode(ino, ibuf);
	if (ino_off != NULL)
		*ino_off = off;
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
	if (nextufs__read_indirect_entry(img, block_frag, entry_index, &next_frag) < 0)
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
nextufs_inode_read_data(const struct nextufs_image *img,
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
			rc = nextufs__read_exact(img, buf + done, chunk_size,
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
	    (ino->flags & NEXTUFS_IC_FASTLINK) == 0 || ino->size == 0 ||
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
nextufs_inode_readlink(const struct nextufs_image *img,
    const struct nextufs_inode *ino, char *out, size_t out_size)
{
	size_t got;

	if ((ino->mode & NEXTUFS_IFMT) != NEXTUFS_IFLNK || out_size == 0)
		return -EINVAL;
	if (decode_inline_symlink(ino, out, out_size) != 0)
		return 0;
	if (nextufs_inode_read_data(img, ino, 0, (uint8_t *)out, out_size - 1,
	    &got) < 0)
		return -EIO;
	out[got] = '\0';
	return 0;
}

int
nextufs_image_open(struct nextufs_image *img, const char *path)
{
	return nextufs_open_with_mode(img, path, 0);
}

int
nextufs_image_open_rw(struct nextufs_image *img, const char *path)
{
	return nextufs_open_with_mode(img, path, 1);
}

static int
nextufs_open_with_mode(struct nextufs_image *img, const char *path, int writable)
{
	uint8_t *scanbuf;
	size_t scan_size;
	size_t label_scan_size;
	size_t off;
	off_t source_size = 0;
	int rc;

	memset(img, 0, sizeof(*img));
	img->fd = -1;
	rc = nextufs_init_backend(img, path, writable, &source_size);
	if (rc < 0)
		return rc;
	scan_size = DEFAULT_SCAN_LIMIT;
	if ((uint64_t)scan_size > (uint64_t)img->image_size)
		scan_size = (size_t)img->image_size;
	scanbuf = malloc(scan_size);
	if (scanbuf == NULL) {
		nextufs_image_close(img);
		return -ENOMEM;
	}
	if (nextufs__read_exact(img, scanbuf, scan_size, 0) < 0) {
		free(scanbuf);
		nextufs_image_close(img);
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

		version = nextufs__read_be32(scanbuf + off);
		if (version != DL_V1 && version != DL_V2 && version != DL_V3)
			continue;
		if (decode_next_disk_label(&dl, scanbuf + off, (off_t)off) < 0)
			continue;
		if (pick_label_slice(&dl, &slice_base, &slice_size) < 0)
			continue;
		if (slice_base < 0 || slice_base >= img->image_size)
			continue;
		if (slice_size <= 0 || slice_base + slice_size > img->image_size)
			continue;
		magic_off = (size_t)(slice_base + UFS_SBLOCK_OFFSET + UFS_SUPER_MAGIC_OFFSET);
		if (magic_off + 4 > scan_size)
			continue;
		if (nextufs__read_be32(scanbuf + magic_off) != UFS_FS_MAGIC)
			continue;
		if (nextufs__read_exact(img, sbuf, sizeof(sbuf),
		    slice_base + UFS_SBLOCK_OFFSET) < 0)
			continue;
		decode_superblock(&img->sb, sbuf);
		if (img->sb.fs_magic != UFS_FS_MAGIC || img->sb.block_size == 0 ||
		    img->sb.frag_size == 0)
			continue;
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

		if (nextufs__read_be32(scanbuf + off) != UFS_FS_MAGIC)
			continue;
		if ((off_t)off < (off_t)UFS_SUPER_MAGIC_OFFSET)
			continue;
		img->slice_base = (off_t)off - (off_t)UFS_SUPER_MAGIC_OFFSET -
		    (off_t)UFS_SBLOCK_OFFSET;
		if (img->slice_base < 0)
			continue;
		if (nextufs__read_exact(img, sbuf, sizeof(sbuf),
		    img->slice_base + UFS_SBLOCK_OFFSET) < 0)
			continue;
		decode_superblock(&img->sb, sbuf);
		if (img->sb.fs_magic != UFS_FS_MAGIC || img->sb.block_size == 0 ||
		    img->sb.frag_size == 0)
			continue;
		img->slice_size = img->image_size - img->slice_base;
		free(scanbuf);
		return 0;
	}
	free(scanbuf);
	nextufs_image_close(img);
	return -EINVAL;
}

void
nextufs_image_close(struct nextufs_image *img)
{
	const struct nextufs_image_backend_ops *ops;

	ops = img->backend_ops;
	if (ops != NULL && ops->close != NULL)
		ops->close(img->backend_ctx);
	img->fd = -1;
	img->backend_ops = NULL;
	img->backend_ctx = NULL;
	img->writable = 0;
	img->source_is_container = 0;
}

int
nextufs_source_extract_slice(const char *source_path, int out_fd)
{
	struct nextufs_image img;
	uint8_t *buf;
	uint64_t copied;
	size_t chunk_size;
	int rc;

	rc = nextufs_image_open(&img, source_path);
	if (rc < 0)
		return rc;
	chunk_size = 1024U * 1024U;
	if ((uint64_t)chunk_size > (uint64_t)img.slice_size &&
	    img.slice_size > 0)
		chunk_size = (size_t)img.slice_size;
	if (chunk_size == 0)
		chunk_size = 4096;
	buf = malloc(chunk_size);
	if (buf == NULL) {
		nextufs_image_close(&img);
		return -ENOMEM;
	}
	rc = 0;
	for (copied = 0; copied < (uint64_t)img.slice_size; copied += chunk_size) {
		size_t chunk = chunk_size;

		if ((uint64_t)chunk > (uint64_t)img.slice_size - copied)
			chunk = (size_t)((uint64_t)img.slice_size - copied);
		rc = nextufs__read_exact(&img, buf, chunk,
		    img.slice_base + (off_t)copied);
		if (rc < 0)
			break;
		rc = nextufs_write_out_fd(out_fd, buf, chunk);
		if (rc < 0)
			break;
	}
	free(buf);
	nextufs_image_close(&img);
	return rc;
}
