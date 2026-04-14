#define FUSE_USE_VERSION 31

#include "nextufs_read.h"

#include <errno.h>
#include <fuse3/fuse.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

static struct nextufs_image g_img = { .fd = -1 };

static int
nextufs_getattr(const char *path, struct stat *st, struct fuse_file_info *fi)
{
	struct nextufs_node node;
	(void)fi;
	int rc;

	rc = nextufs_lookup(&g_img, path, 0, &node);
	if (rc < 0)
		return rc;
	return nextufs_node_stat(&node, st);
}

static int
nextufs_open(const char *path, struct fuse_file_info *fi)
{
	struct nextufs_node node;
	int rc;

	if ((fi->flags & O_ACCMODE) != O_RDONLY)
		return -EROFS;
	rc = nextufs_lookup(&g_img, path, 1, &node);
	if (rc < 0)
		return rc;
	if (!nextufs_node_is_reg(&node))
		return -EISDIR;
	return 0;
}

static int
nextufs_read(const char *path, char *buf, size_t size, off_t offset,
    struct fuse_file_info *fi)
{
	size_t got;
	int rc;
	(void)fi;

	rc = nextufs_read_path(&g_img, path, (uint64_t)offset,
	    (uint8_t *)buf, size, &got);
	if (rc < 0)
		return rc;
	return (int)got;
}

static int
nextufs_readlink(const char *path, char *buf, size_t size)
{
	int rc;

	rc = nextufs_readlink_path(&g_img, path, buf, size);
	if (rc < 0)
		return rc;
	return 0;
}

struct readdir_ctx {
	void *buf;
	fuse_fill_dir_t filler;
};

static int
readdir_cb(const struct nextufs_node *node, const char *name, size_t name_len,
    void *ctx_ptr)
{
	struct readdir_ctx *ctx = ctx_ptr;
	char namebuf[256];
	size_t copy_len;
	struct stat st;

	copy_len = name_len < sizeof(namebuf) - 1 ? name_len : sizeof(namebuf) - 1;
	memcpy(namebuf, name, copy_len);
	namebuf[copy_len] = '\0';
	if (strcmp(namebuf, ".") != 0 && strcmp(namebuf, "..") != 0) {
		if (nextufs_node_stat(node, &st) == 0)
			ctx->filler(ctx->buf, namebuf, &st, 0, FUSE_FILL_DIR_PLUS);
		else
			ctx->filler(ctx->buf, namebuf, NULL, 0, 0);
	}
	return 0;
}

static int
nextufs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
    off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags)
{
	struct readdir_ctx ctx;
	struct nextufs_node node;
	int rc;
	(void)offset;
	(void)fi;
	(void)flags;

	rc = nextufs_lookup(&g_img, path, 1, &node);
	if (rc < 0)
		return rc;
	if (!nextufs_node_is_dir(&node))
		return -ENOTDIR;
	filler(buf, ".", NULL, 0, 0);
	filler(buf, "..", NULL, 0, 0);
	ctx.buf = buf;
	ctx.filler = filler;
	rc = nextufs_iterate_directory_nodes_path(&g_img, path, 1, readdir_cb,
	    &ctx);
	if (rc < 0)
		return rc;
	return 0;
}

static int
nextufs_access(const char *path, int mask)
{
	const struct fuse_context *fctx;
	struct nextufs_node node;
	int rc;

	rc = nextufs_lookup(&g_img, path, 1, &node);
	if (rc < 0)
		return rc;
	fctx = fuse_get_context();
	if (fctx == NULL)
		return 0;
	return nextufs_check_access(&node, fctx->uid, fctx->gid, mask);
}

static int
nextufs_statfs(const char *path, struct statvfs *stvfs)
{
	(void)path;
	return nextufs_statvfs(&g_img, stvfs);
}

static void *
nextufs_init(struct fuse_conn_info *conn, struct fuse_config *cfg)
{
	(void)conn;
	cfg->use_ino = 1;
	return NULL;
}

static const struct fuse_operations nextufs_ops = {
	.init = nextufs_init,
	.getattr = nextufs_getattr,
	.open = nextufs_open,
	.read = nextufs_read,
	.readlink = nextufs_readlink,
	.readdir = nextufs_readdir,
	.access = nextufs_access,
	.statfs = nextufs_statfs,
};

int
main(int argc, char **argv)
{
	struct fuse_args args = FUSE_ARGS_INIT(argc - 1, argv + 1);
	int rc;

	if (argc < 3) {
		fprintf(stderr, "usage: %s <raw-image> <mountpoint> [fuse options]\n",
		    argv[0]);
		return 1;
	}
	rc = nextufs_open_image(&g_img, argv[1]);
	if (rc < 0) {
		fprintf(stderr, "failed to open image %s: %s\n", argv[1],
		    strerror(-rc));
		return 1;
	}
	rc = fuse_main(args.argc, args.argv, &nextufs_ops, NULL);
	fuse_opt_free_args(&args);
	nextufs_close_image(&g_img);
	return rc;
}
