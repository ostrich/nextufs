#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "../../../nextufs/nextufs.h"
#ifndef MAXFRAG
#define MAXFRAG 8
#endif
#include "../../../fsck.nextufs/include/ufs/fs.h"
#include "../../../fsck.nextufs/include/ufs/fsdir.h"

#define UFS_DINODE_MODE_OFF 0
#define UFS_DINODE_SIZE_HI_OFF 8
#define UFS_DINODE_SIZE_LO_OFF 12
#define UFS_DINODE_DB_OFF 40
#define UFS_DINODE_BLOCKS_OFF 104
#define UFS_SUPERBLOCK_BYTE_OFF 8192
#ifndef DEV_BSIZE
#define DEV_BSIZE 512
#endif

static const struct nextufs_write_ctx g_editor_ctx = {
	.policy = NEXTUFS_WRITE_EDITOR,
	.uid = 0,
	.gid = 0,
	.groups = NULL,
	.group_count = 0,
};

static uint16_t
be16(uint16_t v)
{
	return (uint16_t)(((v & 0x00ffU) << 8) | ((v & 0xff00U) >> 8));
}

static uint32_t
be32(uint32_t v)
{
	return ((v & 0x000000ffU) << 24) |
	    ((v & 0x0000ff00U) << 8) |
	    ((v & 0x00ff0000U) >> 8) |
	    ((v & 0xff000000U) >> 24);
}

static int
write_be16(int fd, off_t off, uint16_t value)
{
	uint16_t disk = be16(value);
	return pwrite(fd, &disk, sizeof(disk), off) == (ssize_t)sizeof(disk) ? 0 : -1;
}

static int
write_be32(int fd, off_t off, uint32_t value)
{
	uint32_t disk = be32(value);
	return pwrite(fd, &disk, sizeof(disk), off) == (ssize_t)sizeof(disk) ? 0 : -1;
}

static int
write_inode_mode(int fd, off_t inode_off, uint16_t mode)
{
	uint16_t disk = be16(mode);
	return pwrite(fd, &disk, sizeof(disk), inode_off + UFS_DINODE_MODE_OFF) ==
	    (ssize_t)sizeof(disk) ? 0 : -1;
}

static int
write_inode_size32(int fd, off_t inode_off, uint32_t size)
{
	if (write_be32(fd, inode_off + UFS_DINODE_SIZE_HI_OFF, 0) < 0)
		return -1;
	return write_be32(fd, inode_off + UFS_DINODE_SIZE_LO_OFF, size);
}

static int
write_inode_db(int fd, off_t inode_off, int index, uint32_t frag)
{
	return write_be32(fd, inode_off + UFS_DINODE_DB_OFF + (off_t)index * 4, frag);
}

static int
read_exact(int fd, off_t off, void *buf, size_t len)
{
	return pread(fd, buf, len, off) == (ssize_t)len ? 0 : -1;
}

static int
write_exact(int fd, off_t off, const void *buf, size_t len)
{
	return pwrite(fd, buf, len, off) == (ssize_t)len ? 0 : -1;
}

static off_t
frag_to_offset(const struct nextufs_image *img, uint32_t frag)
{
	return img->slice_base + ((off_t)frag * (off_t)img->sb.frag_size);
}

static int
open_rw(const char *path)
{
	int fd = open(path, O_RDWR);
	if (fd < 0)
		fprintf(stderr, "corrupt_raw_case: open(%s): %s\n", path, strerror(errno));
	return fd;
}

static int
open_image(const char *path, struct nextufs_image *img)
{
	int rc = nextufs_image_open(img, path);
	if (rc < 0)
		fprintf(stderr, "corrupt_raw_case: nextufs_image_open(%s): %d\n", path, rc);
	return rc;
}

static int
lookup_path(const struct nextufs_image *img, const char *path,
    struct nextufs_node *node)
{
	int rc = nextufs_node_lookup(img, path, 0, node);
	if (rc < 0)
		fprintf(stderr, "corrupt_raw_case: lookup(%s): %d\n", path, rc);
	return rc;
}

static int
find_dirent_offset(const struct nextufs_image *img, const struct nextufs_node *dir,
    const char *name, off_t *entry_off_out)
{
	uint8_t blk[8192];
	uint64_t remain = dir->inode.size;
	size_t i;

	for (i = 0; i < 12 && remain > 0; i++) {
		uint32_t frag = dir->inode.db[i];
		size_t block_size;
		size_t pos;

		if (frag == 0)
			continue;
		block_size = img->sb.block_size;
		if (remain < block_size)
			block_size = (size_t)remain;
		if (block_size > sizeof(blk)) {
			fprintf(stderr, "corrupt_raw_case: block too large\n");
			return -1;
		}
		if (read_exact(img->fd, frag_to_offset(img, frag), blk, block_size) < 0) {
			fprintf(stderr, "corrupt_raw_case: directory read failed\n");
			return -1;
		}
		pos = 0;
		while (pos + 8 <= block_size) {
			uint32_t ino;
			uint16_t reclen;
			uint16_t namlen;
			char entname[MAXNAMLEN + 1];

			memcpy(&ino, blk + pos, sizeof(ino));
			memcpy(&reclen, blk + pos + 4, sizeof(reclen));
			memcpy(&namlen, blk + pos + 6, sizeof(namlen));
			ino = be32(ino);
			reclen = be16(reclen);
			namlen = be16(namlen);
			if (reclen == 0 || pos + reclen > block_size)
				return -1;
			if (namlen <= MAXNAMLEN && pos + 8 + namlen <= block_size) {
				memcpy(entname, blk + pos + 8, namlen);
				entname[namlen] = '\0';
				if (ino != 0 && strcmp(entname, name) == 0) {
					*entry_off_out = frag_to_offset(img, frag) + (off_t)pos;
					return 0;
				}
			}
			pos += reclen;
		}
		remain -= block_size;
	}
	fprintf(stderr, "corrupt_raw_case: directory entry %s not found\n", name);
	return -1;
}

static int
write_dirent_name(int fd, off_t entry_off, const char *name)
{
	size_t len = strlen(name);
	char buf[MAXNAMLEN + 1];

	if (len > MAXNAMLEN)
		return -1;
	memset(buf, 0, sizeof(buf));
	memcpy(buf, name, len);
	if (write_be16(fd, entry_off + 6, (uint16_t)len) < 0)
		return -1;
	return pwrite(fd, buf, len + 1, entry_off + 8) == (ssize_t)(len + 1) ? 0 : -1;
}

static off_t
super_offset(const struct nextufs_image *img)
{
	return img->slice_base + UFS_SUPERBLOCK_BYTE_OFF;
}

static uint32_t
cg_of_frag(const struct nextufs_image *img, uint32_t frag)
{
	return frag / img->sb.frags_per_group;
}

static uint32_t
cg_base_frag(const struct nextufs_image *img, uint32_t cg)
{
	return cg * img->sb.frags_per_group +
	    (img->sb.cg_delta * (cg & ~img->sb.cg_cyc_mask));
}

static off_t
cg_offset(const struct nextufs_image *img, uint32_t cg)
{
	return img->slice_base +
	    (off_t)((cg_base_frag(img, cg) + img->sb.cg_off) * img->sb.frag_size);
}

static int
mutate_cg_byte(const struct nextufs_image *img, int fd, uint32_t cg,
    size_t byte_off, uint8_t set_mask, uint8_t clear_mask)
{
	uint8_t v;
	off_t off = cg_offset(img, cg) + (off_t)byte_off;

	if (read_exact(fd, off, &v, sizeof(v)) < 0)
		return -1;
	v |= set_mask;
	v &= (uint8_t)~clear_mask;
	return write_exact(fd, off, &v, sizeof(v));
}

static int
mark_frag_free_in_cg(const struct nextufs_image *img, int fd, uint32_t frag)
{
	uint32_t cg = cg_of_frag(img, frag);
	uint32_t rel_frag = frag - cg_base_frag(img, cg);
	size_t byte_off = offsetof(struct cg, cg_free) + (rel_frag / 8U);
	uint8_t bit = (uint8_t)(1U << (rel_frag % 8U));

	return mutate_cg_byte(img, fd, cg, byte_off, bit, 0);
}

static int
corrupt_bad_block_count(const char *path)
{
	struct nextufs_image img;
	struct nextufs_node node;
	int fd;
	off_t blocks_off;
	uint32_t new_blocks;

	memset(&img, 0, sizeof(img));
	if (open_image(path, &img) < 0)
		return 1;
	if (lookup_path(&img, "/alpha", &node) < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	fd = open_rw(path);
	if (fd < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	blocks_off = node.inode_off + UFS_DINODE_BLOCKS_OFF;
	new_blocks = node.inode.blocks + 7;
	if (write_be32(fd, blocks_off, new_blocks) < 0) {
		fprintf(stderr, "corrupt_raw_case: failed writing di_blocks\n");
		close(fd);
		nextufs_image_close(&img);
		return 1;
	}
	close(fd);
	nextufs_image_close(&img);
	return 0;
}

static int
corrupt_missing_dot(const char *path)
{
	struct nextufs_image img;
	struct nextufs_node node;
	int fd;
	off_t dirblk_off;

	memset(&img, 0, sizeof(img));
	if (open_image(path, &img) < 0)
		return 1;
	if (lookup_path(&img, "/dir", &node) < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	fd = open_rw(path);
	if (fd < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	dirblk_off = frag_to_offset(&img, node.inode.db[0]);
	if (write_be32(fd, dirblk_off, 0) < 0) {
		fprintf(stderr, "corrupt_raw_case: failed clearing '.' inode\n");
		close(fd);
		nextufs_image_close(&img);
		return 1;
	}
	close(fd);
	nextufs_image_close(&img);
	return 0;
}

static int
corrupt_missing_dotdot(const char *path)
{
	struct nextufs_image img;
	struct nextufs_node node;
	int fd;
	uint8_t first[8];
	off_t dirblk_off;
	uint16_t first_reclen;

	memset(&img, 0, sizeof(img));
	if (open_image(path, &img) < 0)
		return 1;
	if (lookup_path(&img, "/dir", &node) < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	fd = open_rw(path);
	if (fd < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	dirblk_off = frag_to_offset(&img, node.inode.db[0]);
	if (read_exact(fd, dirblk_off, first, sizeof(first)) < 0) {
		fprintf(stderr, "corrupt_raw_case: failed reading first dirent\n");
		close(fd);
		nextufs_image_close(&img);
		return 1;
	}
	memcpy(&first_reclen, first + 4, sizeof(first_reclen));
	first_reclen = be16(first_reclen);
	if (write_be32(fd, dirblk_off + first_reclen, 0) < 0) {
		fprintf(stderr, "corrupt_raw_case: failed clearing '..' inode\n");
		close(fd);
		nextufs_image_close(&img);
		return 1;
	}
	close(fd);
	nextufs_image_close(&img);
	return 0;
}

static int
corrupt_unreferenced_alpha(const char *path)
{
	struct nextufs_image img;
	struct nextufs_node root;
	int fd;
	off_t entry_off;

	memset(&img, 0, sizeof(img));
	if (open_image(path, &img) < 0)
		return 1;
	if (nextufs_node_get_root(&img, &root) < 0) {
		fprintf(stderr, "corrupt_raw_case: failed to get root\n");
		nextufs_image_close(&img);
		return 1;
	}
	if (find_dirent_offset(&img, &root, "alpha", &entry_off) < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	fd = open_rw(path);
	if (fd < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	if (write_be32(fd, entry_off, 0) < 0) {
		fprintf(stderr, "corrupt_raw_case: failed clearing alpha dirent\n");
		close(fd);
		nextufs_image_close(&img);
		return 1;
	}
	close(fd);
	nextufs_image_close(&img);
	return 0;
}

static int
corrupt_bad_dot_inode(const char *path)
{
	struct nextufs_image img;
	struct nextufs_node node;
	int fd;
	off_t dirblk_off;

	memset(&img, 0, sizeof(img));
	if (open_image(path, &img) < 0)
		return 1;
	if (lookup_path(&img, "/dir", &node) < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	fd = open_rw(path);
	if (fd < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	dirblk_off = frag_to_offset(&img, node.inode.db[0]);
	if (write_be32(fd, dirblk_off, NEXTUFS_ROOT_INODE) < 0) {
		fprintf(stderr, "corrupt_raw_case: failed changing '.' inode\n");
		close(fd);
		nextufs_image_close(&img);
		return 1;
	}
	close(fd);
	nextufs_image_close(&img);
	return 0;
}

static int
corrupt_bad_dotdot_inode(const char *path)
{
	struct nextufs_image img;
	struct nextufs_node node;
	int fd;
	off_t dirblk_off;

	memset(&img, 0, sizeof(img));
	if (open_image(path, &img) < 0)
		return 1;
	if (lookup_path(&img, "/dir", &node) < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	fd = open_rw(path);
	if (fd < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	dirblk_off = frag_to_offset(&img, node.inode.db[0]);
	if (write_be32(fd, dirblk_off + 12, node.inode_no) < 0) {
		fprintf(stderr, "corrupt_raw_case: failed changing '..' inode\n");
		close(fd);
		nextufs_image_close(&img);
		return 1;
	}
	close(fd);
	nextufs_image_close(&img);
	return 0;
}

static int
corrupt_zero_length_dir(const char *path)
{
	struct nextufs_image img;
	struct nextufs_node node;
	int fd;

	memset(&img, 0, sizeof(img));
	if (open_image(path, &img) < 0)
		return 1;
	if (lookup_path(&img, "/dir", &node) < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	fd = open_rw(path);
	if (fd < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	if (write_inode_size32(fd, node.inode_off, 0) < 0) {
		fprintf(stderr, "corrupt_raw_case: failed setting zero dir size\n");
		close(fd);
		nextufs_image_close(&img);
		return 1;
	}
	close(fd);
	nextufs_image_close(&img);
	return 0;
}

static int
corrupt_short_dir(const char *path)
{
	struct nextufs_image img;
	struct nextufs_node node;
	int fd;

	memset(&img, 0, sizeof(img));
	if (open_image(path, &img) < 0)
		return 1;
	if (lookup_path(&img, "/dir", &node) < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	fd = open_rw(path);
	if (fd < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	if (write_inode_size32(fd, node.inode_off, 8) < 0) {
		fprintf(stderr, "corrupt_raw_case: failed setting short dir size\n");
		close(fd);
		nextufs_image_close(&img);
		return 1;
	}
	close(fd);
	nextufs_image_close(&img);
	return 0;
}

static int
corrupt_misaligned_dir_size(const char *path)
{
	struct nextufs_image img;
	struct nextufs_node node;
	int fd;

	memset(&img, 0, sizeof(img));
	if (open_image(path, &img) < 0)
		return 1;
	if (lookup_path(&img, "/dir", &node) < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	fd = open_rw(path);
	if (fd < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	if (write_inode_size32(fd, node.inode_off, 513) < 0) {
		fprintf(stderr, "corrupt_raw_case: failed setting misaligned dir size\n");
		close(fd);
		nextufs_image_close(&img);
		return 1;
	}
	close(fd);
	nextufs_image_close(&img);
	return 0;
}

static int
corrupt_partially_alloc_inode(const char *path)
{
	struct nextufs_image img;
	struct nextufs_node node;
	int fd;

	memset(&img, 0, sizeof(img));
	if (open_image(path, &img) < 0)
		return 1;
	if (lookup_path(&img, "/alpha", &node) < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	fd = open_rw(path);
	if (fd < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	if (write_inode_mode(fd, node.inode_off, 0) < 0) {
		fprintf(stderr, "corrupt_raw_case: failed clearing inode mode\n");
		close(fd);
		nextufs_image_close(&img);
		return 1;
	}
	close(fd);
	nextufs_image_close(&img);
	return 0;
}

static int
corrupt_bad_file_type(const char *path)
{
	struct nextufs_image img;
	struct nextufs_node node;
	int fd;

	memset(&img, 0, sizeof(img));
	if (open_image(path, &img) < 0)
		return 1;
	if (lookup_path(&img, "/alpha", &node) < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	fd = open_rw(path);
	if (fd < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	if (write_inode_mode(fd, node.inode_off, 0160000) < 0) {
		fprintf(stderr, "corrupt_raw_case: failed setting bad file type\n");
		close(fd);
		nextufs_image_close(&img);
		return 1;
	}
	close(fd);
	nextufs_image_close(&img);
	return 0;
}

static int
corrupt_dir_entry_fclear(const char *path)
{
	struct nextufs_image img;
	struct nextufs_node nested;
	int fd;

	memset(&img, 0, sizeof(img));
	if (open_image(path, &img) < 0)
		return 1;
	if (lookup_path(&img, "/dir/file", &nested) < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	fd = open_rw(path);
	if (fd < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	if (write_inode_mode(fd, nested.inode_off, 0) < 0) {
		fprintf(stderr, "corrupt_raw_case: failed clearing nested inode mode\n");
		close(fd);
		nextufs_image_close(&img);
		return 1;
	}
	close(fd);
	nextufs_image_close(&img);
	return 0;
}

static int
corrupt_dir_entry_unallocated(const char *path)
{
	struct nextufs_image img;
	struct nextufs_node nested;
	int fd;
	size_t i;

	memset(&img, 0, sizeof(img));
	if (open_image(path, &img) < 0)
		return 1;
	if (lookup_path(&img, "/dir/file", &nested) < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	fd = open_rw(path);
	if (fd < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	if (write_inode_mode(fd, nested.inode_off, 0) < 0 ||
	    write_inode_size32(fd, nested.inode_off, 0) < 0) {
		fprintf(stderr, "corrupt_raw_case: failed clearing nested inode state\n");
		close(fd);
		nextufs_image_close(&img);
		return 1;
	}
	for (i = 0; i < 12; i++) {
		if (write_inode_db(fd, nested.inode_off, (int)i, 0) < 0) {
			fprintf(stderr, "corrupt_raw_case: failed clearing nested direct blocks\n");
			close(fd);
			nextufs_image_close(&img);
			return 1;
		}
	}
	if (write_be32(fd, nested.inode_off + UFS_DINODE_BLOCKS_OFF, 0) < 0) {
		fprintf(stderr, "corrupt_raw_case: failed clearing nested block count\n");
		close(fd);
		nextufs_image_close(&img);
		return 1;
	}
	close(fd);
	nextufs_image_close(&img);
	return 0;
}

static int
corrupt_dup_block(const char *path)
{
	struct nextufs_image img;
	struct nextufs_node alpha, nested;
	int fd;

	memset(&img, 0, sizeof(img));
	if (open_image(path, &img) < 0)
		return 1;
	if (lookup_path(&img, "/alpha", &alpha) < 0 ||
	    lookup_path(&img, "/dir/file", &nested) < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	fd = open_rw(path);
	if (fd < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	if (write_inode_db(fd, nested.inode_off, 0, alpha.inode.db[0]) < 0) {
		fprintf(stderr, "corrupt_raw_case: failed duplicating data block\n");
		close(fd);
		nextufs_image_close(&img);
		return 1;
	}
	close(fd);
	nextufs_image_close(&img);
	return 0;
}

static int
corrupt_out_of_range_block(const char *path)
{
	struct nextufs_image img;
	struct nextufs_node alpha;
	int fd;

	memset(&img, 0, sizeof(img));
	if (open_image(path, &img) < 0)
		return 1;
	if (lookup_path(&img, "/alpha", &alpha) < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	fd = open_rw(path);
	if (fd < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	if (write_inode_db(fd, alpha.inode_off, 0, img.sb.frag_count + 100) < 0) {
		fprintf(stderr, "corrupt_raw_case: failed writing out-of-range block\n");
		close(fd);
		nextufs_image_close(&img);
		return 1;
	}
	close(fd);
	nextufs_image_close(&img);
	return 0;
}

static int
corrupt_unreferenced_dir(const char *path)
{
	struct nextufs_image img;
	struct nextufs_node root;
	int fd;
	off_t entry_off;

	memset(&img, 0, sizeof(img));
	if (open_image(path, &img) < 0)
		return 1;
	if (nextufs_node_get_root(&img, &root) < 0) {
		fprintf(stderr, "corrupt_raw_case: failed to get root\n");
		nextufs_image_close(&img);
		return 1;
	}
	if (find_dirent_offset(&img, &root, "dir", &entry_off) < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	fd = open_rw(path);
	if (fd < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	if (write_be32(fd, entry_off, 0) < 0) {
		fprintf(stderr, "corrupt_raw_case: failed clearing dir dirent\n");
		close(fd);
		nextufs_image_close(&img);
		return 1;
	}
	close(fd);
	nextufs_image_close(&img);
	return 0;
}

static int
corrupt_invalid_dir_inode(const char *path)
{
	struct nextufs_image img;
	struct nextufs_node dir;
	int fd;
	off_t entry_off;

	memset(&img, 0, sizeof(img));
	if (open_image(path, &img) < 0)
		return 1;
	if (lookup_path(&img, "/dir", &dir) < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	if (find_dirent_offset(&img, &dir, "file", &entry_off) < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	fd = open_rw(path);
	if (fd < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	if (write_be32(fd, entry_off, img.sb.inodes_per_group * img.sb.cg_count + 100) < 0) {
		fprintf(stderr, "corrupt_raw_case: failed writing out-of-range inode\n");
		close(fd);
		nextufs_image_close(&img);
		return 1;
	}
	close(fd);
	nextufs_image_close(&img);
	return 0;
}

static int
corrupt_extra_dot(const char *path)
{
	struct nextufs_image img;
	struct nextufs_node dir;
	int fd;
	off_t entry_off;

	memset(&img, 0, sizeof(img));
	if (open_image(path, &img) < 0)
		return 1;
	if (lookup_path(&img, "/dir", &dir) < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	if (find_dirent_offset(&img, &dir, "file", &entry_off) < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	fd = open_rw(path);
	if (fd < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	if (write_dirent_name(fd, entry_off, ".") < 0) {
		fprintf(stderr, "corrupt_raw_case: failed renaming entry to '.'\n");
		close(fd);
		nextufs_image_close(&img);
		return 1;
	}
	close(fd);
	nextufs_image_close(&img);
	return 0;
}

static int
corrupt_extra_dotdot(const char *path)
{
	struct nextufs_image img;
	struct nextufs_node dir;
	int fd;
	off_t entry_off;

	memset(&img, 0, sizeof(img));
	if (open_image(path, &img) < 0)
		return 1;
	if (lookup_path(&img, "/dir", &dir) < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	if (find_dirent_offset(&img, &dir, "file", &entry_off) < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	fd = open_rw(path);
	if (fd < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	if (write_dirent_name(fd, entry_off, "..") < 0) {
		fprintf(stderr, "corrupt_raw_case: failed renaming entry to '..'\n");
		close(fd);
		nextufs_image_close(&img);
		return 1;
	}
	close(fd);
	nextufs_image_close(&img);
	return 0;
}

static int
corrupt_root_unallocated(const char *path)
{
	struct nextufs_image img;
	struct nextufs_node root;
	int fd;

	memset(&img, 0, sizeof(img));
	if (open_image(path, &img) < 0)
		return 1;
	if (nextufs_node_get_root(&img, &root) < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	fd = open_rw(path);
	if (fd < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	if (write_inode_mode(fd, root.inode_off, 0) < 0 ||
	    write_inode_size32(fd, root.inode_off, 0) < 0 ||
	    write_inode_db(fd, root.inode_off, 0, 0) < 0) {
		fprintf(stderr, "corrupt_raw_case: failed clearing root inode\n");
		close(fd);
		nextufs_image_close(&img);
		return 1;
	}
	close(fd);
	nextufs_image_close(&img);
	return 0;
}

static int
corrupt_root_not_dir(const char *path)
{
	struct nextufs_image img;
	struct nextufs_node root;
	int fd;

	memset(&img, 0, sizeof(img));
	if (open_image(path, &img) < 0)
		return 1;
	if (nextufs_node_get_root(&img, &root) < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	fd = open_rw(path);
	if (fd < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	if (write_inode_mode(fd, root.inode_off, NEXTUFS_IFREG | 0755) < 0) {
		fprintf(stderr, "corrupt_raw_case: failed changing root mode\n");
		close(fd);
		nextufs_image_close(&img);
		return 1;
	}
	close(fd);
	nextufs_image_close(&img);
	return 0;
}

static int
corrupt_super_minfree(const char *path)
{
	struct nextufs_image img;
	int fd;

	memset(&img, 0, sizeof(img));
	if (open_image(path, &img) < 0)
		return 1;
	fd = open_rw(path);
	if (fd < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	if (write_be32(fd, super_offset(&img) + offsetof(struct fs, fs_minfree), 120) < 0) {
		fprintf(stderr, "corrupt_raw_case: failed changing fs_minfree\n");
		close(fd);
		nextufs_image_close(&img);
		return 1;
	}
	close(fd);
	nextufs_image_close(&img);
	return 0;
}

static int
corrupt_super_optim(const char *path)
{
	struct nextufs_image img;
	int fd;

	memset(&img, 0, sizeof(img));
	if (open_image(path, &img) < 0)
		return 1;
	fd = open_rw(path);
	if (fd < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	if (write_be32(fd, super_offset(&img) + offsetof(struct fs, fs_optim), 99) < 0) {
		fprintf(stderr, "corrupt_raw_case: failed changing fs_optim\n");
		close(fd);
		nextufs_image_close(&img);
		return 1;
	}
	close(fd);
	nextufs_image_close(&img);
	return 0;
}

static int
corrupt_super_free_counts(const char *path)
{
	struct nextufs_image img;
	int fd;

	memset(&img, 0, sizeof(img));
	if (open_image(path, &img) < 0)
		return 1;
	fd = open_rw(path);
	if (fd < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	if (write_be32(fd, super_offset(&img) + offsetof(struct fs, fs_cstotal) + offsetof(struct csum, cs_nifree),
	        img.sb.free_inode_count + 3) < 0 ||
	    write_be32(fd, super_offset(&img) + offsetof(struct fs, fs_cstotal) + offsetof(struct csum, cs_nffree),
	        img.sb.free_frag_count + 5) < 0) {
		fprintf(stderr, "corrupt_raw_case: failed changing super counts\n");
		close(fd);
		nextufs_image_close(&img);
		return 1;
	}
	close(fd);
	nextufs_image_close(&img);
	return 0;
}

static int
corrupt_partial_indirect(const char *path)
{
	struct nextufs_image img;
	struct nextufs_node big;
	int fd;
	uint64_t new_size;

	memset(&img, 0, sizeof(img));
	if (open_image(path, &img) < 0)
		return 1;
	if (lookup_path(&img, "/big", &big) < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	if (big.inode.ib[0] == 0) {
		fprintf(stderr, "corrupt_raw_case: /big has no indirect block\n");
		nextufs_image_close(&img);
		return 1;
	}
	fd = open_rw(path);
	if (fd < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	new_size = (uint64_t)img.sb.block_size * 12 + 1;
	if (write_inode_size32(fd, big.inode_off, (uint32_t)new_size) < 0) {
		fprintf(stderr, "corrupt_raw_case: failed shrinking /big size\n");
		close(fd);
		nextufs_image_close(&img);
		return 1;
	}
	close(fd);
	nextufs_image_close(&img);
	return 0;
}

static int
corrupt_extraneous_dir_link(const char *path)
{
	struct nextufs_image img;
	struct nextufs_node root, dir;
	int fd;
	off_t entry_off;

	memset(&img, 0, sizeof(img));
	if (open_image(path, &img) < 0)
		return 1;
	if (nextufs_node_get_root(&img, &root) < 0 ||
	    lookup_path(&img, "/dir", &dir) < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	if (find_dirent_offset(&img, &root, "alpha", &entry_off) < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	fd = open_rw(path);
	if (fd < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	if (write_be32(fd, entry_off, dir.inode_no) < 0 ||
	    write_dirent_name(fd, entry_off, "dirlink") < 0) {
		fprintf(stderr, "corrupt_raw_case: failed creating extraneous dir link\n");
		close(fd);
		nextufs_image_close(&img);
		return 1;
	}
	close(fd);
	nextufs_image_close(&img);
	return 0;
}

static int
corrupt_lostfound_missing(const char *path)
{
	struct nextufs_image img;
	struct nextufs_node root;
	int fd, rc;
	off_t alpha_entry_off;

	rc = nextufs_path_rmdir(&g_editor_ctx, path, "/lost+found");
	if (rc < 0) {
		fprintf(stderr, "corrupt_raw_case: failed removing lost+found: %d\n", rc);
		return 1;
	}
	memset(&img, 0, sizeof(img));
	if (open_image(path, &img) < 0)
		return 1;
	if (nextufs_node_get_root(&img, &root) < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	if (find_dirent_offset(&img, &root, "alpha", &alpha_entry_off) < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	fd = open_rw(path);
	if (fd < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	if (write_be32(fd, alpha_entry_off, 0) < 0) {
		fprintf(stderr, "corrupt_raw_case: failed clearing alpha entry\n");
		close(fd);
		nextufs_image_close(&img);
		return 1;
	}
	close(fd);
	nextufs_image_close(&img);
	return 0;
}

static int
corrupt_lostfound_not_dir(const char *path)
{
	struct nextufs_image img;
	struct nextufs_node root;
	int fd, rc;
	off_t alpha_entry_off;

	rc = nextufs_path_rmdir(&g_editor_ctx, path, "/lost+found");
	if (rc < 0) {
		fprintf(stderr, "corrupt_raw_case: failed removing lost+found: %d\n", rc);
		return 1;
	}
	rc = nextufs_path_create_file(&g_editor_ctx, path, "/lost+found",
	    "not a directory", sizeof("not a directory") - 1);
	if (rc < 0) {
		fprintf(stderr, "corrupt_raw_case: failed creating regular lost+found: %d\n", rc);
		return 1;
	}
	memset(&img, 0, sizeof(img));
	if (open_image(path, &img) < 0)
		return 1;
	if (nextufs_node_get_root(&img, &root) < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	if (find_dirent_offset(&img, &root, "alpha", &alpha_entry_off) < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	fd = open_rw(path);
	if (fd < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	if (write_be32(fd, alpha_entry_off, 0) < 0) {
		fprintf(stderr, "corrupt_raw_case: failed corrupting lost+found\n");
		close(fd);
		nextufs_image_close(&img);
		return 1;
	}
	close(fd);
	nextufs_image_close(&img);
	return 0;
}

static int
corrupt_cg_bitmap_bad(const char *path)
{
	struct nextufs_image img;
	struct nextufs_node alpha;
	int fd;

	memset(&img, 0, sizeof(img));
	if (open_image(path, &img) < 0)
		return 1;
	if (lookup_path(&img, "/alpha", &alpha) < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	fd = open_rw(path);
	if (fd < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	if (mark_frag_free_in_cg(&img, fd, alpha.inode.db[0]) < 0) {
		fprintf(stderr, "corrupt_raw_case: failed corrupting cg bitmap\n");
		close(fd);
		nextufs_image_close(&img);
		return 1;
	}
	close(fd);
	nextufs_image_close(&img);
	return 0;
}

static int
corrupt_cg_summary_bad(const char *path)
{
	struct nextufs_image img;
	int fd;

	memset(&img, 0, sizeof(img));
	if (open_image(path, &img) < 0)
		return 1;
	fd = open_rw(path);
	if (fd < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	if (write_be32(fd, cg_offset(&img, 0) + offsetof(struct cg, cg_cs) + offsetof(struct csum, cs_nbfree),
	        img.sb.free_block_count + 1) < 0) {
		fprintf(stderr, "corrupt_raw_case: failed corrupting cg summary\n");
		close(fd);
		nextufs_image_close(&img);
		return 1;
	}
	close(fd);
	nextufs_image_close(&img);
	return 0;
}

static int
corrupt_super_cstotal_bad(const char *path)
{
	struct nextufs_image img;
	int fd;

	memset(&img, 0, sizeof(img));
	if (open_image(path, &img) < 0)
		return 1;
	fd = open_rw(path);
	if (fd < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	if (write_be32(fd, super_offset(&img) + offsetof(struct fs, fs_cstotal) + offsetof(struct csum, cs_nbfree),
	        img.sb.free_block_count + 1) < 0) {
		fprintf(stderr, "corrupt_raw_case: failed corrupting superblock totals\n");
		close(fd);
		nextufs_image_close(&img);
		return 1;
	}
	close(fd);
	nextufs_image_close(&img);
	return 0;
}

int
main(int argc, char **argv)
{
	if (argc != 3) {
		fprintf(stderr,
		    "usage: %s <case> <raw-image>\n"
		    "cases: bad-block-count bad-dot-inode bad-dotdot-inode bad-file-type "
		    "cg-bitmap-bad cg-summary-bad dir-entry-fclear "
		    "dir-entry-unallocated dup-block extra-dot extra-dotdot "
		    "extraneous-dir-link invalid-dir-inode lostfound-missing "
		    "lostfound-not-dir missing-dot missing-dotdot misaligned-dir-size "
		    "out-of-range-block partial-indirect partially-allocated-inode "
		    "root-not-dir root-unallocated short-dir super-cstotal-bad "
		    "super-free-counts super-minfree super-optim unreferenced-alpha "
		    "unreferenced-dir zero-length-dir\n",
		    argv[0]);
		return 2;
	}
	if (strcmp(argv[1], "bad-block-count") == 0)
		return corrupt_bad_block_count(argv[2]);
	if (strcmp(argv[1], "bad-dot-inode") == 0)
		return corrupt_bad_dot_inode(argv[2]);
	if (strcmp(argv[1], "bad-dotdot-inode") == 0)
		return corrupt_bad_dotdot_inode(argv[2]);
	if (strcmp(argv[1], "bad-file-type") == 0)
		return corrupt_bad_file_type(argv[2]);
	if (strcmp(argv[1], "cg-bitmap-bad") == 0)
		return corrupt_cg_bitmap_bad(argv[2]);
	if (strcmp(argv[1], "cg-summary-bad") == 0)
		return corrupt_cg_summary_bad(argv[2]);
	if (strcmp(argv[1], "dir-entry-fclear") == 0)
		return corrupt_dir_entry_fclear(argv[2]);
	if (strcmp(argv[1], "dir-entry-unallocated") == 0)
		return corrupt_dir_entry_unallocated(argv[2]);
	if (strcmp(argv[1], "dup-block") == 0)
		return corrupt_dup_block(argv[2]);
	if (strcmp(argv[1], "extra-dot") == 0)
		return corrupt_extra_dot(argv[2]);
	if (strcmp(argv[1], "extra-dotdot") == 0)
		return corrupt_extra_dotdot(argv[2]);
	if (strcmp(argv[1], "extraneous-dir-link") == 0)
		return corrupt_extraneous_dir_link(argv[2]);
	if (strcmp(argv[1], "invalid-dir-inode") == 0)
		return corrupt_invalid_dir_inode(argv[2]);
	if (strcmp(argv[1], "lostfound-missing") == 0)
		return corrupt_lostfound_missing(argv[2]);
	if (strcmp(argv[1], "lostfound-not-dir") == 0)
		return corrupt_lostfound_not_dir(argv[2]);
	if (strcmp(argv[1], "missing-dot") == 0)
		return corrupt_missing_dot(argv[2]);
	if (strcmp(argv[1], "missing-dotdot") == 0)
		return corrupt_missing_dotdot(argv[2]);
	if (strcmp(argv[1], "misaligned-dir-size") == 0)
		return corrupt_misaligned_dir_size(argv[2]);
	if (strcmp(argv[1], "out-of-range-block") == 0)
		return corrupt_out_of_range_block(argv[2]);
	if (strcmp(argv[1], "partial-indirect") == 0)
		return corrupt_partial_indirect(argv[2]);
	if (strcmp(argv[1], "partially-allocated-inode") == 0)
		return corrupt_partially_alloc_inode(argv[2]);
	if (strcmp(argv[1], "root-not-dir") == 0)
		return corrupt_root_not_dir(argv[2]);
	if (strcmp(argv[1], "root-unallocated") == 0)
		return corrupt_root_unallocated(argv[2]);
	if (strcmp(argv[1], "short-dir") == 0)
		return corrupt_short_dir(argv[2]);
	if (strcmp(argv[1], "super-cstotal-bad") == 0)
		return corrupt_super_cstotal_bad(argv[2]);
	if (strcmp(argv[1], "super-free-counts") == 0)
		return corrupt_super_free_counts(argv[2]);
	if (strcmp(argv[1], "super-minfree") == 0)
		return corrupt_super_minfree(argv[2]);
	if (strcmp(argv[1], "super-optim") == 0)
		return corrupt_super_optim(argv[2]);
	if (strcmp(argv[1], "unreferenced-alpha") == 0)
		return corrupt_unreferenced_alpha(argv[2]);
	if (strcmp(argv[1], "unreferenced-dir") == 0)
		return corrupt_unreferenced_dir(argv[2]);
	if (strcmp(argv[1], "zero-length-dir") == 0)
		return corrupt_zero_length_dir(argv[2]);
	fprintf(stderr, "corrupt_raw_case: unknown case %s\n", argv[1]);
	return 2;
}
