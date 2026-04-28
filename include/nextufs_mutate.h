#ifndef NEXTUFS_MUTATE_H
#define NEXTUFS_MUTATE_H

#include "nextufs_node.h"

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

enum nextufs_write_policy {
	NEXTUFS_WRITE_SU = 0,
	NEXTUFS_WRITE_USER = 1,
};

struct nextufs_write_ctx {
	enum nextufs_write_policy policy;
	uid_t uid;
	gid_t gid;
	const gid_t *groups;
	size_t group_count;
};

int nextufs_path_create_file(const struct nextufs_write_ctx *ctx,
	const char *image_path, const char *path, const void *data,
	size_t data_len);
int nextufs_path_create_file_from_hostfile(const struct nextufs_write_ctx *ctx,
	const char *image_path, const char *path, const char *host_file);
int nextufs_path_unlink(const struct nextufs_write_ctx *ctx,
	const char *image_path, const char *path);
int nextufs_path_mkdir(const struct nextufs_write_ctx *ctx,
	const char *image_path, const char *path, uint16_t mode);
int nextufs_path_overwrite_file(const struct nextufs_write_ctx *ctx,
	const char *image_path, const char *path, const void *data, size_t data_len);
int nextufs_path_append_file(const struct nextufs_write_ctx *ctx,
	const char *image_path, const char *path, const void *data, size_t data_len);
int nextufs_path_rmdir(const struct nextufs_write_ctx *ctx,
	const char *image_path, const char *path);
int nextufs_path_link(const struct nextufs_write_ctx *ctx,
	const char *image_path, const char *source_path, const char *target_path);
int nextufs_path_rename(const struct nextufs_write_ctx *ctx,
	const char *image_path, const char *source_path, const char *target_path);
int nextufs_path_symlink(const struct nextufs_write_ctx *ctx,
	const char *image_path, const char *target, const char *link_path);
int nextufs_path_truncate(const struct nextufs_write_ctx *ctx,
	const char *image_path, const char *path, uint64_t size);
int nextufs_path_pwrite(const struct nextufs_write_ctx *ctx,
	const char *image_path, const char *path, const void *data,
	size_t data_len, uint64_t offset);
int nextufs_path_mknod(const struct nextufs_write_ctx *ctx,
	const char *image_path, const char *path, uint16_t mode, uint32_t rdev);
int nextufs_path_chmod(const struct nextufs_write_ctx *ctx,
	const char *image_path, const char *path, uint16_t mode);
int nextufs_path_chown(const struct nextufs_write_ctx *ctx,
	const char *image_path, const char *path, uid_t uid, gid_t gid);
int nextufs_path_utimes(const struct nextufs_write_ctx *ctx,
	const char *image_path, const char *path, uint32_t atime, uint32_t mtime);

#endif
