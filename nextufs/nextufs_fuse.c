#define FUSE_USE_VERSION 31

#include "nextufs.h"
#include "nextufs_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <fuse3/fuse.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

static struct nextufs_image g_img = { .fd = -1 };
static const char *g_image_path;
static char g_image_path_buf[PATH_MAX];
enum nextufs_mount_mode {
	NEXTUFS_MOUNT_SU = 0,
	NEXTUFS_MOUNT_USER = 1,
};

static enum nextufs_mount_mode g_mount_mode = NEXTUFS_MOUNT_SU;
static int g_mount_readonly;
static int g_mount_request_rw;
static int g_mount_saw_access_opt;
static uid_t g_mount_uid;
static gid_t g_mount_gid;
static int g_mount_uid_set;
static int g_mount_gid_set;

static void
nextufs_effective_ids(uid_t *uid_out, gid_t *gid_out)
{
	const struct fuse_context *fctx;
	uid_t uid;
	gid_t gid;

	uid = getuid();
	gid = getgid();
	fctx = fuse_get_context();
	if (fctx != NULL) {
		uid = fctx->uid;
		gid = fctx->gid;
	}
	if (g_mount_uid_set)
		uid = g_mount_uid;
	if (g_mount_gid_set)
		gid = g_mount_gid;
	*uid_out = uid;
	*gid_out = gid;
}

static void
nextufs_fill_mutation_ctx(struct nextufs_write_ctx *ctx)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->policy = g_mount_mode == NEXTUFS_MOUNT_SU ?
	    NEXTUFS_WRITE_SU : NEXTUFS_WRITE_USER;
	nextufs_effective_ids(&ctx->uid, &ctx->gid);
}

static int
nextufs_parse_mode_option(const char *value)
{
	if (strcmp(value, "su") == 0) {
		g_mount_mode = NEXTUFS_MOUNT_SU;
		return 0;
	}
	if (strcmp(value, "user") == 0) {
		g_mount_mode = NEXTUFS_MOUNT_USER;
		return 0;
	}
	return -1;
}

static int
nextufs_parse_id_option(const char *value, unsigned int *id_out)
{
	char *endptr;
	unsigned long parsed;

	if (value == NULL || *value == '\0')
		return -EINVAL;
	errno = 0;
	parsed = strtoul(value, &endptr, 10);
	if (errno != 0 || *endptr != '\0' || parsed > UINT_MAX)
		return -EINVAL;
	*id_out = (unsigned int)parsed;
	return 0;
}

static int
nextufs_strip_mode_from_optarg(const char *optarg, char **rebuilt_out)
{
	char *copy;
	char *saveptr;
	char *token;
	size_t out_len;
	char *rebuilt;
	int kept_any;

	copy = strdup(optarg);
	if (copy == NULL)
		return -ENOMEM;
	out_len = strlen(optarg) + 1;
	rebuilt = malloc(out_len);
	if (rebuilt == NULL) {
		free(copy);
		return -ENOMEM;
	}
	rebuilt[0] = '\0';
	kept_any = 0;
	for (token = strtok_r(copy, ",", &saveptr);
	    token != NULL;
	    token = strtok_r(NULL, ",", &saveptr)) {
		if (strncmp(token, "mode=", 5) == 0) {
			if (nextufs_parse_mode_option(token + 5) < 0) {
				free(rebuilt);
				free(copy);
				return -EINVAL;
			}
			continue;
		}
		if (strncmp(token, "uid=", 4) == 0) {
			unsigned int parsed;

			if (nextufs_parse_id_option(token + 4, &parsed) < 0) {
				free(rebuilt);
				free(copy);
				return -EINVAL;
			}
			g_mount_uid = (uid_t)parsed;
			g_mount_uid_set = 1;
			continue;
		}
		if (strncmp(token, "gid=", 4) == 0) {
			unsigned int parsed;

			if (nextufs_parse_id_option(token + 4, &parsed) < 0) {
				free(rebuilt);
				free(copy);
				return -EINVAL;
			}
			g_mount_gid = (gid_t)parsed;
			g_mount_gid_set = 1;
			continue;
		}
		if (strcmp(token, "rw") == 0) {
			g_mount_request_rw = 1;
			g_mount_saw_access_opt = 1;
		} else if (strcmp(token, "ro") == 0) {
			g_mount_request_rw = 0;
			g_mount_saw_access_opt = 1;
		}
		if (kept_any)
			strcat(rebuilt, ",");
		strcat(rebuilt, token);
		kept_any = 1;
	}
	free(copy);
	*rebuilt_out = rebuilt;
	return kept_any;
}

static int
nextufs_build_fuse_args(int argc, char **argv, struct fuse_args *args_out)
{
	int i;

	args_out->argc = 0;
	args_out->argv = NULL;
	args_out->allocated = 0;
	if (fuse_opt_add_arg(args_out, argv[0]) != 0)
		return -ENOMEM;
	for (i = 2; i < argc; i++) {
		if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
			char *rebuilt = NULL;
			int rc = nextufs_strip_mode_from_optarg(argv[i + 1], &rebuilt);

			if (rc < 0) {
				fuse_opt_free_args(args_out);
				return rc;
			}
			if (rc > 0) {
				if (fuse_opt_add_arg(args_out, "-o") != 0 ||
				    fuse_opt_add_arg(args_out, rebuilt) != 0) {
					free(rebuilt);
					fuse_opt_free_args(args_out);
					return -ENOMEM;
				}
			}
			free(rebuilt);
			i++;
			continue;
		}
		if (fuse_opt_add_arg(args_out, argv[i]) != 0) {
			fuse_opt_free_args(args_out);
			return -ENOMEM;
		}
	}
	return 0;
}

static int
nextufs_refresh_image(void)
{
	nextufs_image_close(&g_img);
	if (g_mount_readonly)
		return nextufs_image_open(&g_img, g_image_path);
	return nextufs_image_open_rw(&g_img, g_image_path);
}

static int
nextufs_require_writable(void)
{
	return g_mount_readonly ? -EROFS : 0;
}

static int
nextufs_require_access(const struct nextufs_node *node, int mask)
{
	uid_t uid;
	gid_t gid;

	if (g_mount_mode == NEXTUFS_MOUNT_SU)
		return 0;
	nextufs_effective_ids(&uid, &gid);
	return nextufs__node_check_access(node, uid, gid, NULL, 0, mask,
	    !g_mount_readonly);
}

static int
nextufs_getattr(const char *path, struct stat *st, struct fuse_file_info *fi)
{
	struct nextufs_node node;
	int rc;
	(void)fi;

	rc = nextufs_node_lookup(&g_img, path, 0, &node);
	if (rc < 0)
		return rc;
	return nextufs_node_stat(&node, st);
}

static int
nextufs_open(const char *path, struct fuse_file_info *fi)
{
	struct nextufs_node node;
	struct nextufs_write_ctx ctx;
	int rc;

	rc = nextufs_node_lookup(&g_img, path, 1, &node);
	if (rc < 0)
		return rc;
	if (!nextufs_node_is_reg(&node))
		return -EISDIR;
	if ((fi->flags & O_ACCMODE) == O_RDWR)
		rc = nextufs_require_access(&node, R_OK | W_OK);
	else if ((fi->flags & O_ACCMODE) == O_WRONLY)
		rc = nextufs_require_access(&node, W_OK);
	else
		rc = nextufs_require_access(&node, R_OK);
	if (rc < 0)
		return rc;
	if ((fi->flags & (O_WRONLY | O_RDWR)) != 0 && g_mount_readonly)
		return -EROFS;
	if ((fi->flags & O_TRUNC) != 0) {
		rc = nextufs_require_writable();
		if (rc < 0)
			return rc;
		nextufs_fill_mutation_ctx(&ctx);
		rc = nextufs_path_truncate(&ctx, g_image_path, path, 0);
		if (rc < 0)
			return rc;
		rc = nextufs_refresh_image();
		if (rc < 0)
			return rc;
	}
	return 0;
}

static int
nextufs_read(const char *path, char *buf, size_t size, off_t offset,
    struct fuse_file_info *fi)
{
	struct nextufs_node node;
	size_t got;
	int rc;
	(void)fi;

	rc = nextufs_node_lookup(&g_img, path, 1, &node);
	if (rc < 0)
		return rc;
	rc = nextufs_require_access(&node, R_OK);
	if (rc < 0)
		return rc;
	rc = nextufs_path_read(&g_img, path, (uint64_t)offset, (uint8_t *)buf, size,
	    &got);
	if (rc < 0)
		return rc;
	return (int)got;
}

static int
nextufs_write(const char *path, const char *buf, size_t size, off_t offset,
    struct fuse_file_info *fi)
{
	struct nextufs_write_ctx ctx;
	int rc;
	(void)fi;

	rc = nextufs_require_writable();
	if (rc < 0)
		return rc;
	nextufs_fill_mutation_ctx(&ctx);
	rc = nextufs_path_pwrite(&ctx, g_image_path, path, buf, size,
	    (uint64_t)offset);
	if (rc < 0)
		return rc;
	rc = nextufs_refresh_image();
	if (rc < 0)
		return rc;
	return (int)size;
}

static int
nextufs_readlink(const char *path, char *buf, size_t size)
{
	int rc;

	rc = nextufs_path_readlink(&g_img, path, buf, size);
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

	rc = nextufs_node_lookup(&g_img, path, 1, &node);
	if (rc < 0)
		return rc;
	if (!nextufs_node_is_dir(&node))
		return -ENOTDIR;
	rc = nextufs_require_access(&node, R_OK | X_OK);
	if (rc < 0)
		return rc;
	filler(buf, ".", NULL, 0, 0);
	filler(buf, "..", NULL, 0, 0);
	ctx.buf = buf;
	ctx.filler = filler;
	return nextufs_directory_iterate_nodes_path(&g_img, path, 1, readdir_cb,
	    &ctx);
}

static int
nextufs_access(const char *path, int mask)
{
	struct nextufs_node node;
	int rc;

	rc = nextufs_node_lookup(&g_img, path, 1, &node);
	if (rc < 0)
		return rc;
	return nextufs_require_access(&node, mask);
}

static int
nextufs_statfs(const char *path, struct statvfs *stvfs)
{
	(void)path;
	return nextufs_fs_statvfs(&g_img, stvfs);
}

static int
nextufs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	struct nextufs_write_ctx ctx;
	int rc;

	rc = nextufs_require_writable();
	if (rc < 0)
		return rc;
	nextufs_fill_mutation_ctx(&ctx);
	rc = nextufs_path_mknod(&ctx, g_image_path, path,
	    NEXTUFS_IFREG | (mode & 07777), 0);
	if (rc < 0)
		return rc;
	rc = nextufs_refresh_image();
	if (rc < 0)
		return rc;
	return nextufs_open(path, fi);
}

static int
nextufs_mknod(const char *path, mode_t mode, dev_t rdev)
{
	struct nextufs_write_ctx ctx;
	int rc;

	rc = nextufs_require_writable();
	if (rc < 0)
		return rc;
	nextufs_fill_mutation_ctx(&ctx);
	rc = nextufs_path_mknod(&ctx, g_image_path, path, (uint16_t)mode,
	    (uint32_t)rdev);
	if (rc < 0)
		return rc;
	return nextufs_refresh_image();
}

static int
nextufs_unlink(const char *path)
{
	struct nextufs_write_ctx ctx;
	int rc;

	rc = nextufs_require_writable();
	if (rc < 0)
		return rc;
	nextufs_fill_mutation_ctx(&ctx);
	rc = nextufs_path_unlink(&ctx, g_image_path, path);
	if (rc < 0)
		return rc;
	return nextufs_refresh_image();
}

static int
nextufs_mkdir(const char *path, mode_t mode)
{
	struct nextufs_write_ctx ctx;
	int rc;

	rc = nextufs_require_writable();
	if (rc < 0)
		return rc;
	nextufs_fill_mutation_ctx(&ctx);
	rc = nextufs_path_mkdir(&ctx, g_image_path, path, (uint16_t)mode);
	if (rc < 0)
		return rc;
	return nextufs_refresh_image();
}

static int
nextufs_rmdir(const char *path)
{
	struct nextufs_write_ctx ctx;
	int rc;

	rc = nextufs_require_writable();
	if (rc < 0)
		return rc;
	nextufs_fill_mutation_ctx(&ctx);
	rc = nextufs_path_rmdir(&ctx, g_image_path, path);
	if (rc < 0)
		return rc;
	return nextufs_refresh_image();
}

static int
nextufs_rename(const char *from, const char *to, unsigned int flags)
{
	struct nextufs_write_ctx ctx;
	int rc;

	if (flags != 0)
		return -EINVAL;
	rc = nextufs_require_writable();
	if (rc < 0)
		return rc;
	nextufs_fill_mutation_ctx(&ctx);
	rc = nextufs_path_rename(&ctx, g_image_path, from, to);
	if (rc < 0)
		return rc;
	return nextufs_refresh_image();
}

static int
nextufs_link(const char *from, const char *to)
{
	struct nextufs_write_ctx ctx;
	int rc;

	rc = nextufs_require_writable();
	if (rc < 0)
		return rc;
	nextufs_fill_mutation_ctx(&ctx);
	rc = nextufs_path_link(&ctx, g_image_path, from, to);
	if (rc < 0)
		return rc;
	return nextufs_refresh_image();
}

static int
nextufs_symlink(const char *target, const char *linkpath)
{
	struct nextufs_write_ctx ctx;
	int rc;

	rc = nextufs_require_writable();
	if (rc < 0)
		return rc;
	nextufs_fill_mutation_ctx(&ctx);
	rc = nextufs_path_symlink(&ctx, g_image_path, target, linkpath);
	if (rc < 0)
		return rc;
	return nextufs_refresh_image();
}

static int
nextufs_chmod(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	struct nextufs_write_ctx ctx;
	int rc;
	(void)fi;

	rc = nextufs_require_writable();
	if (rc < 0)
		return rc;
	nextufs_fill_mutation_ctx(&ctx);
	rc = nextufs_path_chmod(&ctx, g_image_path, path, (uint16_t)mode);
	if (rc < 0)
		return rc;
	return nextufs_refresh_image();
}

static int
nextufs_chown(const char *path, uid_t uid, gid_t gid, struct fuse_file_info *fi)
{
	struct nextufs_write_ctx ctx;
	int rc;
	(void)fi;

	rc = nextufs_require_writable();
	if (rc < 0)
		return rc;
	nextufs_fill_mutation_ctx(&ctx);
	rc = nextufs_path_chown(&ctx, g_image_path, path, uid, gid);
	if (rc < 0)
		return rc;
	return nextufs_refresh_image();
}

static int
nextufs_truncate(const char *path, off_t size, struct fuse_file_info *fi)
{
	struct nextufs_write_ctx ctx;
	int rc;
	(void)fi;

	if (size < 0)
		return -EINVAL;
	rc = nextufs_require_writable();
	if (rc < 0)
		return rc;
	nextufs_fill_mutation_ctx(&ctx);
	rc = nextufs_path_truncate(&ctx, g_image_path, path, (uint64_t)size);
	if (rc < 0)
		return rc;
	return nextufs_refresh_image();
}

static int
nextufs_utimens(const char *path, const struct timespec ts[2],
    struct fuse_file_info *fi)
{
	struct nextufs_write_ctx ctx;
	int rc;
	(void)fi;

	rc = nextufs_require_writable();
	if (rc < 0)
		return rc;
	nextufs_fill_mutation_ctx(&ctx);
	rc = nextufs_path_utimes(&ctx, g_image_path, path,
	    (uint32_t)ts[0].tv_sec, (uint32_t)ts[1].tv_sec);
	if (rc < 0)
		return rc;
	return nextufs_refresh_image();
}

static int
nextufs_fsync(const char *path, int datasync, struct fuse_file_info *fi)
{
	(void)path;
	(void)datasync;
	(void)fi;
	if (g_mount_readonly)
		return 0;
	return nextufs__image_fsync(&g_img);
}

static void *
nextufs_init(struct fuse_conn_info *conn, struct fuse_config *cfg)
{
	(void)conn;
	cfg->use_ino = 1;
	cfg->kernel_cache = 0;
	return NULL;
}

static const struct fuse_operations nextufs_ops = {
	.init = nextufs_init,
	.getattr = nextufs_getattr,
	.open = nextufs_open,
	.read = nextufs_read,
	.write = nextufs_write,
	.readlink = nextufs_readlink,
	.readdir = nextufs_readdir,
	.access = nextufs_access,
	.statfs = nextufs_statfs,
	.create = nextufs_create,
	.mknod = nextufs_mknod,
	.unlink = nextufs_unlink,
	.mkdir = nextufs_mkdir,
	.rmdir = nextufs_rmdir,
	.rename = nextufs_rename,
	.link = nextufs_link,
	.symlink = nextufs_symlink,
	.chmod = nextufs_chmod,
	.chown = nextufs_chown,
	.truncate = nextufs_truncate,
	.utimens = nextufs_utimens,
	.fsync = nextufs_fsync,
};

int
main(int argc, char **argv)
{
	struct fuse_args args;
	int rc;

	if (argc < 3) {
		fprintf(stderr, "usage: %s <source> <mountpoint> [fuse options]\n",
		    argv[0]);
		return 1;
	}
	g_mount_mode = NEXTUFS_MOUNT_SU;
	g_mount_request_rw = 0;
	g_mount_saw_access_opt = 0;
	g_mount_uid_set = 0;
	g_mount_gid_set = 0;
	rc = nextufs_build_fuse_args(argc, argv, &args);
	if (rc < 0) {
		fprintf(stderr, "failed to parse fuse args: %s\n", strerror(-rc));
		return 1;
	}
	if (realpath(argv[1], g_image_path_buf) == NULL) {
		fprintf(stderr, "failed to resolve image path %s: %s\n", argv[1],
		    strerror(errno));
		fuse_opt_free_args(&args);
		return 1;
	}
	g_image_path = g_image_path_buf;
	g_mount_readonly = !g_mount_request_rw;
	if (!g_mount_request_rw && !g_mount_saw_access_opt) {
		if (fuse_opt_add_arg(&args, "-o") != 0 ||
		    fuse_opt_add_arg(&args, "ro") != 0) {
			fprintf(stderr, "failed to set read-only fuse args\n");
			fuse_opt_free_args(&args);
			return 1;
		}
	}
	rc = nextufs_refresh_image();
	if (rc < 0) {
		fprintf(stderr, "failed to open source %s: %s\n", argv[1],
		    strerror(-rc));
		fuse_opt_free_args(&args);
		return 1;
	}
	rc = fuse_main(args.argc, args.argv, &nextufs_ops, NULL);
	fuse_opt_free_args(&args);
	nextufs_image_close(&g_img);
	return rc;
}
