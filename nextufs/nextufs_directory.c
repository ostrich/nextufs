#include "nextufs_internal.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint16_t
read_be16(const uint8_t *p)
{
	return ((uint16_t)p[0] << 8) | (uint16_t)p[1];
}

static uint32_t
read_be32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) |
	    ((uint32_t)p[1] << 16) |
	    ((uint32_t)p[2] << 8) |
	    (uint32_t)p[3];
}

static int
read_dirent_v1(const uint8_t *buf, size_t size, size_t off, uint32_t *ino,
    uint16_t *reclen, uint16_t *namlen, const char **name)
{
	if (off + 8 > size)
		return -1;
	*ino = read_be32(buf + off);
	*reclen = read_be16(buf + off + 4);
	*namlen = read_be16(buf + off + 6);
	if (*reclen < 8 || off + *reclen > size || off + 8 + *namlen > size)
		return -1;
	*name = (const char *)(buf + off + 8);
	return 0;
}

int
nextufs_directory_iterate(const struct nextufs_image *img,
    const struct nextufs_inode *dirino, nextufs_dir_iter_cb cb, void *ctx)
{
	uint8_t *buf;
	uint64_t remaining;
	size_t block_index;

	buf = malloc(img->sb.block_size);
	if (buf == NULL)
		return -ENOMEM;
	remaining = dirino->size;
	for (block_index = 0; remaining > 0; block_index++) {
		size_t off = 0;
		size_t chunk_size;

		chunk_size = remaining < img->sb.block_size ?
		    (size_t)remaining : img->sb.block_size;
		if (nextufs_inode_read_data(img, dirino,
		    (uint64_t)block_index * img->sb.block_size, buf, chunk_size,
		    NULL) < 0) {
			free(buf);
			return -EIO;
		}
		while (off < chunk_size) {
			uint32_t d_ino;
			uint16_t d_reclen;
			uint16_t d_namlen;
			const char *d_name;

			if (read_dirent_v1(buf, chunk_size, off, &d_ino, &d_reclen,
			    &d_namlen, &d_name) < 0)
				break;
			if (d_ino != 0 && d_namlen != 0) {
				int rc;

				rc = cb(d_ino, d_name, d_namlen, ctx);
				if (rc != 0) {
					free(buf);
					return rc;
				}
			}
			off += d_reclen;
		}
		remaining -= chunk_size;
	}
	free(buf);
	return 0;
}

struct dir_node_ctx {
	const struct nextufs_image *img;
	nextufs_dir_node_iter_cb cb;
	void *ctx;
};

static int
dir_node_cb(uint32_t ino, const char *name, size_t name_len, void *ctx_ptr)
{
	struct dir_node_ctx *ctx = ctx_ptr;
	struct nextufs_node node;
	int rc;

	rc = nextufs_node_get_by_inode(ctx->img, ino, &node);
	if (rc < 0)
		return rc;
	return ctx->cb(&node, name, name_len, ctx->ctx);
}

int
nextufs_directory_iterate_nodes(const struct nextufs_image *img,
    const struct nextufs_inode *dirino, nextufs_dir_node_iter_cb cb, void *ctx)
{
	struct dir_node_ctx node_ctx;

	node_ctx.img = img;
	node_ctx.cb = cb;
	node_ctx.ctx = ctx;
	return nextufs_directory_iterate(img, dirino, dir_node_cb, &node_ctx);
}

struct find_name_ctx {
	const char *target_name;
	size_t target_len;
	unsigned *target_inode;
};

static int
find_name_cb(uint32_t ino, const char *name, size_t name_len, void *ctx_ptr)
{
	struct find_name_ctx *ctx = ctx_ptr;

	if (ctx->target_len == name_len &&
	    memcmp(name, ctx->target_name, name_len) == 0) {
		*ctx->target_inode = ino;
		return 1;
	}
	return 0;
}

int
nextufs__find_name_in_directory(const struct nextufs_image *img,
    const struct nextufs_inode *dirino, const char *target_name,
    unsigned *target_inode)
{
	struct find_name_ctx ctx;
	int rc;

	ctx.target_name = target_name;
	ctx.target_len = strlen(target_name);
	ctx.target_inode = target_inode;
	*target_inode = 0;
	rc = nextufs_directory_iterate(img, dirino, find_name_cb, &ctx);
	if (rc == 1)
		return 0;
	return rc;
}
