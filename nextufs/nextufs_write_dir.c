#include "nextufs_write_internal.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int
nextufs_w_write_dirent_raw(uint8_t *p, unsigned ino, uint16_t reclen,
    const char *name)
{
	size_t name_len = strlen(name);

	if (name_len > 255)
		return -ENAMETOOLONG;
	memset(p, 0, reclen);
	nextufs_w_write_be32(p + 0, (uint32_t)ino);
	nextufs_w_write_be16(p + 4, reclen);
	nextufs_w_write_be16(p + 6, (uint16_t)name_len);
	memcpy(p + 8, name, name_len);
	return 0;
}

int
nextufs_w_update_node_times(const struct nextufs_image *img,
    struct nextufs_node *node, int set_size)
{
	int rc;
	uint32_t now = (uint32_t)time(NULL);

	node->inode.mtime = now;
	node->inode.ctime = now;
	node->inode.atime = now;
	(void)set_size;
	rc = nextufs_w_write_inode_raw(img, node->inode_no, &node->inode);
	if (rc < 0)
		return rc;
	return nextufs_get_node_by_inode(img, node->inode_no, node);
}

static int
nextufs_w_grow_directory_by_dirblk(const struct nextufs_image *img,
    struct nextufs_node *dirnode, uint32_t *new_block_frag_out,
    off_t *new_block_off_out)
{
	uint64_t old_size;
	uint64_t new_size;
	uint32_t logical_block;
	uint32_t inblock_old_size;
	uint32_t old_frags;
	uint32_t new_frags;
	uint32_t alloc_frag;
	int rc;

	old_size = dirnode->inode.size;
	if ((old_size % DIRBLKSIZ) != 0)
		return -EINVAL;
	new_size = old_size + DIRBLKSIZ;
	logical_block = (uint32_t)(old_size / img->sb.block_size);
	if (logical_block >= 12)
		return -EFBIG;
	inblock_old_size = (uint32_t)(old_size % img->sb.block_size);
	if (inblock_old_size == 0) {
		rc = nextufs_w_allocate_frags_anycg(img,
		    dirnode->inode_no / img->sb.inodes_per_group, 1, &alloc_frag);
		if (rc < 0)
			return rc;
		dirnode->inode.db[logical_block] = alloc_frag;
		*new_block_frag_out = alloc_frag;
		*new_block_off_out = img->slice_base +
		    ((off_t)alloc_frag * img->sb.frag_size);
	} else {
		old_frags = (uint32_t)((inblock_old_size + img->sb.frag_size - 1U) /
		    img->sb.frag_size);
		new_frags = (uint32_t)(((inblock_old_size + DIRBLKSIZ) +
		    img->sb.frag_size - 1U) / img->sb.frag_size);
		if (new_frags <= old_frags)
			return -EINVAL;
		alloc_frag = dirnode->inode.db[logical_block];
		if (alloc_frag == 0)
			return -EINVAL;
		rc = nextufs_w_extend_fragment_run(img, alloc_frag, old_frags, new_frags);
		if (rc == -ENOSPC) {
			rc = nextufs_w_reallocate_fragment_run(img, alloc_frag, old_frags,
			    new_frags, dirnode->inode_no / img->sb.inodes_per_group,
			    &alloc_frag);
		}
		if (rc < 0)
			return rc;
		dirnode->inode.db[logical_block] = alloc_frag;
		*new_block_frag_out = alloc_frag;
		*new_block_off_out = img->slice_base +
		    ((off_t)alloc_frag * img->sb.frag_size) +
		    (off_t)inblock_old_size;
	}
	dirnode->inode.size = new_size;
	dirnode->inode.blocks += DIRBLKSIZ / DEV_BSIZE;
	return nextufs_w_update_node_times(img, dirnode, 1);
}

int
nextufs_w_insert_dirent_existing_space(const struct nextufs_image *img,
    struct nextufs_node *dirnode, const char *name, unsigned new_inode)
{
	uint8_t *block;
	uint64_t remaining;
	size_t block_index;
	size_t need;

	need = nextufs_w_dirent_size(strlen(name));
	block = malloc(img->sb.block_size);
	if (block == NULL)
		return -ENOMEM;
	remaining = dirnode->inode.size;
	for (block_index = 0; remaining > 0; block_index++) {
		size_t off = 0;
		size_t chunk_size;

		chunk_size = remaining < img->sb.block_size ?
		    (size_t)remaining : img->sb.block_size;
		if (nextufs_read_inode_data(img, &dirnode->inode,
		    (uint64_t)block_index * img->sb.block_size, block, chunk_size,
		    NULL) < 0) {
			free(block);
			return -EIO;
		}
		while (off < chunk_size) {
			struct nextufs_write_dirent ent;
			size_t used;
			size_t slack;
			off_t block_disk_off;

			if (nextufs_w_read_dirent(block, chunk_size, off, &ent) < 0)
				break;
			used = ent.ino == 0 ? 0 : nextufs_w_dirent_size(ent.namlen);
			if (ent.ino == 0 && ent.reclen >= need) {
				if (nextufs_w_write_dirent_raw(block + off, new_inode,
				    ent.reclen, name) < 0) {
					free(block);
					return -EINVAL;
				}
			} else {
				slack = ent.reclen - used;
				if (ent.ino != 0 && slack >= need) {
					nextufs_w_write_be16(block + off + 4, (uint16_t)used);
					if (nextufs_w_write_dirent_raw(block + off + used,
					    new_inode, (uint16_t)slack, name) < 0) {
						free(block);
						return -EINVAL;
					}
				} else {
					off += ent.reclen;
					continue;
				}
			}
			{
				uint32_t data_frag;
				uint64_t logical_block_index = block_index;

				if (logical_block_index < 12) {
					data_frag = dirnode->inode.db[logical_block_index];
				} else {
					free(block);
					return -ENOSPC;
				}
				block_disk_off = img->slice_base +
				    ((off_t)data_frag * img->sb.frag_size);
			}
			if (nextufs_w_write_exact(img->fd, block, chunk_size,
			    block_disk_off) < 0) {
				free(block);
				return -EIO;
			}
			free(block);
			return nextufs_w_update_node_times(img, dirnode, 0);
		}
		remaining -= chunk_size;
	}
	free(block);
	{
		uint8_t new_dirblk[DIRBLKSIZ];
		uint32_t new_block_frag;
		off_t new_block_off;

		if (need > DIRBLKSIZ)
			return -ENOSPC;
		if (nextufs_w_grow_directory_by_dirblk(img, dirnode, &new_block_frag,
		    &new_block_off) < 0)
			return -ENOSPC;
		(void)new_block_frag;
		memset(new_dirblk, 0, sizeof(new_dirblk));
		if (nextufs_w_write_dirent_raw(new_dirblk, new_inode, DIRBLKSIZ,
		    name) < 0)
			return -EINVAL;
		if (nextufs_w_write_exact(img->fd, new_dirblk, sizeof(new_dirblk),
		    new_block_off) < 0)
			return -EIO;
		return 0;
	}
}

int
nextufs_w_remove_dirent(const struct nextufs_image *img,
    struct nextufs_node *dirnode, const char *name, unsigned *removed_inode_out)
{
	uint8_t *block;
	uint64_t remaining;
	size_t block_index;
	size_t name_len;

	name_len = strlen(name);
	block = malloc(img->sb.block_size);
	if (block == NULL)
		return -ENOMEM;
	remaining = dirnode->inode.size;
	for (block_index = 0; remaining > 0; block_index++) {
		size_t chunk_size;
		size_t dirblk_base;
		off_t block_disk_off;
		uint32_t data_frag;

		chunk_size = remaining < img->sb.block_size ?
		    (size_t)remaining : img->sb.block_size;
		if (nextufs_read_inode_data(img, &dirnode->inode,
		    (uint64_t)block_index * img->sb.block_size, block, chunk_size,
		    NULL) < 0) {
			free(block);
			return -EIO;
		}
		if (block_index >= 12 || dirnode->inode.db[block_index] == 0) {
			free(block);
			return -EINVAL;
		}
		data_frag = dirnode->inode.db[block_index];
		block_disk_off = img->slice_base + ((off_t)data_frag * img->sb.frag_size);
		for (dirblk_base = 0; dirblk_base < chunk_size; dirblk_base += DIRBLKSIZ) {
			size_t dirblk_size = chunk_size - dirblk_base;
			size_t off = 0;
			size_t prev_off = (size_t)-1;

			if (dirblk_size > DIRBLKSIZ)
				dirblk_size = DIRBLKSIZ;
			while (off < dirblk_size) {
				struct nextufs_write_dirent ent;
				struct nextufs_write_dirent prev_ent;

				if (nextufs_w_read_dirent(block + dirblk_base, dirblk_size,
				    off, &ent) < 0)
					break;
				if (ent.ino != 0 && ent.namlen == name_len &&
				    memcmp(ent.name, name, name_len) == 0) {
					if (name_len == 1 && ent.name[0] == '.') {
						free(block);
						return -EINVAL;
					}
					if (name_len == 2 && ent.name[0] == '.' &&
					    ent.name[1] == '.') {
						free(block);
						return -EINVAL;
					}
					if (prev_off != (size_t)-1) {
						if (nextufs_w_read_dirent(block + dirblk_base,
						    dirblk_size, prev_off, &prev_ent) < 0) {
							free(block);
							return -EINVAL;
						}
						nextufs_w_write_be16(block + dirblk_base +
						    prev_off + 4,
						    (uint16_t)(prev_ent.reclen + ent.reclen));
					} else {
						nextufs_w_write_be32(block + dirblk_base + off, 0);
					}
					if (nextufs_w_write_exact(img->fd, block, chunk_size,
					    block_disk_off) < 0) {
						free(block);
						return -EIO;
					}
					*removed_inode_out = ent.ino;
					free(block);
					return nextufs_w_update_node_times(img, dirnode, 0);
				}
				if (ent.ino != 0)
					prev_off = off;
				off += ent.reclen;
			}
		}
		remaining -= chunk_size;
	}
	free(block);
	return -ENOENT;
}

int
nextufs_w_write_new_directory_block(const struct nextufs_image *img,
    uint32_t frag_addr, unsigned self_ino, unsigned parent_ino)
{
	uint8_t dirblk[DIRBLKSIZ];

	memset(dirblk, 0, sizeof(dirblk));
	if (nextufs_w_write_dirent_raw(dirblk, self_ino,
	    (uint16_t)nextufs_w_dirent_size(1), ".") < 0)
		return -EINVAL;
	if (nextufs_w_write_dirent_raw(dirblk + nextufs_w_dirent_size(1),
	    parent_ino, (uint16_t)(DIRBLKSIZ - nextufs_w_dirent_size(1)),
	    "..") < 0)
		return -EINVAL;
	return nextufs_w_write_exact(img->fd, dirblk, sizeof(dirblk),
	    img->slice_base + ((off_t)frag_addr * img->sb.frag_size));
}

int
nextufs_w_directory_is_empty(const struct nextufs_image *img,
    const struct nextufs_node *dirnode, unsigned parent_ino)
{
	uint8_t *block;
	uint64_t remaining;
	size_t block_index;
	int seen_dot = 0;
	int seen_dotdot = 0;

	if (!nextufs_node_is_dir(dirnode))
		return 0;
	block = malloc(img->sb.block_size);
	if (block == NULL)
		return 0;
	remaining = dirnode->inode.size;
	for (block_index = 0; remaining > 0; block_index++) {
		size_t chunk_size;
		size_t off;

		chunk_size = remaining < img->sb.block_size ?
		    (size_t)remaining : img->sb.block_size;
		if (nextufs_read_inode_data(img, &dirnode->inode,
		    (uint64_t)block_index * img->sb.block_size, block, chunk_size,
		    NULL) < 0) {
			free(block);
			return 0;
		}
		for (off = 0; off < chunk_size; ) {
			struct nextufs_write_dirent ent;

			if (nextufs_w_read_dirent(block, chunk_size, off, &ent) < 0) {
				free(block);
				return 0;
			}
			if (ent.ino != 0) {
				if (ent.namlen == 1 && ent.name[0] == '.') {
					if (ent.ino != dirnode->inode_no) {
						free(block);
						return 0;
					}
					seen_dot = 1;
				} else if (ent.namlen == 2 &&
				    ent.name[0] == '.' && ent.name[1] == '.') {
					if (ent.ino != parent_ino) {
						free(block);
						return 0;
					}
					seen_dotdot = 1;
				} else {
					free(block);
					return 0;
				}
			}
			off += ent.reclen;
		}
		remaining -= chunk_size;
	}
	free(block);
	return seen_dot && seen_dotdot;
}

void
nextufs_w_store_inline_symlink_bytes(struct nextufs_inode *ino,
    const char *target, size_t len)
{
	size_t i;
	uint8_t raw[60];

	memset(raw, 0, sizeof(raw));
	memcpy(raw, target, len);
	for (i = 0; i < 12; i++) {
		ino->db[i] = ((uint32_t)raw[(i * 4) + 0] << 24) |
		    ((uint32_t)raw[(i * 4) + 1] << 16) |
		    ((uint32_t)raw[(i * 4) + 2] << 8) |
		    (uint32_t)raw[(i * 4) + 3];
	}
	for (i = 0; i < 3; i++) {
		ino->ib[i] = ((uint32_t)raw[48 + (i * 4) + 0] << 24) |
		    ((uint32_t)raw[48 + (i * 4) + 1] << 16) |
		    ((uint32_t)raw[48 + (i * 4) + 2] << 8) |
		    (uint32_t)raw[48 + (i * 4) + 3];
	}
}
