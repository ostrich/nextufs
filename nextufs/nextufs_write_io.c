#include "nextufs_write_internal.h"

#include <errno.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

uint16_t
nextufs_w_read_be16(const uint8_t *p)
{
	return ((uint16_t)p[0] << 8) | (uint16_t)p[1];
}

uint32_t
nextufs_w_read_be32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) |
	    ((uint32_t)p[1] << 16) |
	    ((uint32_t)p[2] << 8) |
	    (uint32_t)p[3];
}

void
nextufs_w_write_be16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)(v >> 8);
	p[1] = (uint8_t)v;
}

void
nextufs_w_write_be32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

void
nextufs_w_write_be64(uint8_t *p, uint64_t v)
{
	nextufs_w_write_be32(p, (uint32_t)(v >> 32));
	nextufs_w_write_be32(p + 4, (uint32_t)v);
}

int
nextufs_w_read_exact(int fd, void *buf, size_t size, off_t offset)
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

int
nextufs_w_write_exact(int fd, const void *buf, size_t size, off_t offset)
{
	const uint8_t *in = buf;
	size_t done = 0;

	while (done < size) {
		ssize_t n;

		n = pwrite(fd, in + done, size - done, offset + (off_t)done);
		if (n < 0)
			return -errno;
		if (n == 0)
			return -EIO;
		done += (size_t)n;
	}
	return 0;
}

uint64_t
nextufs_w_cgstart(const struct nextufs_image *img, unsigned cg)
{
	uint64_t cgbase;
	uint64_t cyc_mask_bits;

	cgbase = (uint64_t)img->sb.frags_per_group * cg;
	cyc_mask_bits = ~img->sb.cg_cyc_mask;
	return cgbase + ((uint64_t)img->sb.cg_delta * (cg & cyc_mask_bits));
}

off_t
nextufs_w_cg_block_offset(const struct nextufs_image *img, unsigned cg)
{
	return img->slice_base +
	    (off_t)((nextufs_w_cgstart(img, cg) + img->sb.cg_off) *
	    img->sb.frag_size);
}

off_t
nextufs_w_inode_offset(const struct nextufs_image *img, unsigned inode_no)
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
	frag_addr = cgimin + ((inum_in_group / sb->inodes_per_block) <<
	    sb->frag_shift);
	inode_slot = inum_in_group % sb->inodes_per_block;
	return img->slice_base + (off_t)(frag_addr * sb->frag_size) +
	    (off_t)(inode_slot * UFS_INODE_SIZE);
}

size_t
nextufs_w_dirent_size(size_t name_len)
{
	return 8U + ((name_len + 1U + 3U) & ~3U);
}

int
nextufs_w_read_dirent(const uint8_t *buf, size_t size, size_t off,
    struct nextufs_write_dirent *ent)
{
	if (off + 8 > size)
		return -1;
	ent->ino = nextufs_w_read_be32(buf + off);
	ent->reclen = nextufs_w_read_be16(buf + off + 4);
	ent->namlen = nextufs_w_read_be16(buf + off + 6);
	if (ent->reclen < 8 || off + ent->reclen > size ||
	    off + 8 + ent->namlen > size)
		return -1;
	ent->name = (const char *)(buf + off + 8);
	return 0;
}

int
nextufs_w_write_inode_raw(const struct nextufs_image *img, unsigned inode_no,
    const struct nextufs_inode *ino)
{
	uint8_t raw[UFS_INODE_SIZE];
	size_t i;
	off_t off;

	memset(raw, 0, sizeof(raw));
	nextufs_w_write_be16(raw + 0x00, ino->mode);
	nextufs_w_write_be16(raw + 0x02, ino->nlink);
	nextufs_w_write_be16(raw + 0x04, ino->uid);
	nextufs_w_write_be16(raw + 0x06, ino->gid);
	nextufs_w_write_be64(raw + 0x08, ino->size);
	nextufs_w_write_be32(raw + 0x10, ino->atime);
	nextufs_w_write_be32(raw + 0x18, ino->mtime);
	nextufs_w_write_be32(raw + 0x20, ino->ctime);
	for (i = 0; i < 12; i++)
		nextufs_w_write_be32(raw + 0x28 + (i * 4), ino->db[i]);
	for (i = 0; i < 3; i++)
		nextufs_w_write_be32(raw + 0x58 + (i * 4), ino->ib[i]);
	nextufs_w_write_be32(raw + 0x64, ino->flags);
	nextufs_w_write_be32(raw + 0x68, ino->blocks);
	nextufs_w_write_be32(raw + 0x6c, ino->gen);
	off = nextufs_w_inode_offset(img, inode_no);
	if (off < 0)
		return -EINVAL;
	return nextufs_w_write_exact(img->fd, raw, sizeof(raw), off);
}

int
nextufs_w_update_summary_counts(const struct nextufs_image *img, unsigned cg,
    int32_t d_ndir, int32_t d_nbfree, int32_t d_nifree, int32_t d_nffree)
{
	uint8_t sbuf[2048];
	uint8_t csum_buf[16];
	off_t sb_off;
	off_t cs_off;
	uint32_t v;
	int rc;
	uint32_t now;

	sb_off = img->slice_base + 0x2000;
	rc = nextufs_w_read_exact(img->fd, sbuf, sizeof(sbuf), sb_off);
	if (rc < 0)
		return rc;
	v = nextufs_w_read_be32(sbuf + SB_NDIR_OFF);
	nextufs_w_write_be32(sbuf + SB_NDIR_OFF, (uint32_t)(v + d_ndir));
	v = nextufs_w_read_be32(sbuf + SB_NBFREE_OFF);
	nextufs_w_write_be32(sbuf + SB_NBFREE_OFF, (uint32_t)(v + d_nbfree));
	v = nextufs_w_read_be32(sbuf + SB_NIFREE_OFF);
	nextufs_w_write_be32(sbuf + SB_NIFREE_OFF, (uint32_t)(v + d_nifree));
	v = nextufs_w_read_be32(sbuf + SB_NFFREE_OFF);
	nextufs_w_write_be32(sbuf + SB_NFFREE_OFF, (uint32_t)(v + d_nffree));
	now = (uint32_t)time(NULL);
	nextufs_w_write_be32(sbuf + SB_TIME_OFF, now);
	rc = nextufs_w_write_exact(img->fd, sbuf, sizeof(sbuf), sb_off);
	if (rc < 0)
		return rc;

	cs_off = img->slice_base +
	    ((off_t)img->sb.cyl_summary_addr * img->sb.frag_size) +
	    ((off_t)cg * (off_t)sizeof(csum_buf));
	rc = nextufs_w_read_exact(img->fd, csum_buf, sizeof(csum_buf), cs_off);
	if (rc < 0)
		return rc;
	v = nextufs_w_read_be32(csum_buf + 0);
	nextufs_w_write_be32(csum_buf + 0, (uint32_t)(v + d_ndir));
	v = nextufs_w_read_be32(csum_buf + 4);
	nextufs_w_write_be32(csum_buf + 4, (uint32_t)(v + d_nbfree));
	v = nextufs_w_read_be32(csum_buf + 8);
	nextufs_w_write_be32(csum_buf + 8, (uint32_t)(v + d_nifree));
	v = nextufs_w_read_be32(csum_buf + 12);
	nextufs_w_write_be32(csum_buf + 12, (uint32_t)(v + d_nffree));
	return nextufs_w_write_exact(img->fd, csum_buf, sizeof(csum_buf), cs_off);
}

int
nextufs_w_path_dirname_basename(const char *path, char *parent,
    size_t parent_size, char *name, size_t name_size)
{
	const char *slash;
	size_t parent_len;
	size_t name_len;

	if (path == NULL || path[0] != '/')
		return -EINVAL;
	slash = strrchr(path, '/');
	if (slash == NULL || slash[1] == '\0')
		return -EINVAL;
	parent_len = (size_t)(slash - path);
	name_len = strlen(slash + 1);
	if (name_len == 0 || name_len >= name_size)
		return -ENAMETOOLONG;
	if (parent_len == 0) {
		if (parent_size < 2)
			return -ENAMETOOLONG;
		parent[0] = '/';
		parent[1] = '\0';
	} else {
		if (parent_len + 1 > parent_size)
			return -ENAMETOOLONG;
		memcpy(parent, path, parent_len);
		parent[parent_len] = '\0';
	}
	memcpy(name, slash + 1, name_len + 1);
	return 0;
}
