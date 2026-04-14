#ifndef NEXTUFS_WRITE_H
#define NEXTUFS_WRITE_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

enum nextufs_write_policy {
	NEXTUFS_WRITE_EDITOR = 0,
	NEXTUFS_WRITE_PERMISSIONS = 1,
};

struct nextufs_write_ctx {
	enum nextufs_write_policy policy;
	uid_t uid;
	gid_t gid;
	const gid_t *groups;
	size_t group_count;
};

int nextufs_create_small_file(const struct nextufs_write_ctx *ctx,
	const char *image_path, const char *path, const void *data,
	size_t data_len);
int nextufs_create_file_from_hostfile(const struct nextufs_write_ctx *ctx,
	const char *image_path, const char *path, const char *host_file);
int nextufs_unlink_path(const struct nextufs_write_ctx *ctx,
	const char *image_path, const char *path);
int nextufs_mkdir_path(const struct nextufs_write_ctx *ctx,
	const char *image_path, const char *path, uint16_t mode);
int nextufs_overwrite_file(const struct nextufs_write_ctx *ctx,
	const char *image_path, const char *path, const void *data, size_t data_len);
int nextufs_append_file(const struct nextufs_write_ctx *ctx,
	const char *image_path, const char *path, const void *data, size_t data_len);
int nextufs_rmdir_path(const struct nextufs_write_ctx *ctx,
	const char *image_path, const char *path);
int nextufs_link_path(const struct nextufs_write_ctx *ctx,
	const char *image_path, const char *source_path, const char *target_path);
int nextufs_rename_path(const struct nextufs_write_ctx *ctx,
	const char *image_path, const char *source_path, const char *target_path);
int nextufs_symlink_path(const struct nextufs_write_ctx *ctx,
	const char *image_path, const char *target, const char *link_path);
int nextufs_truncate_path(const struct nextufs_write_ctx *ctx,
	const char *image_path, const char *path, uint64_t size);
int nextufs_pwrite_path(const struct nextufs_write_ctx *ctx,
	const char *image_path, const char *path, const void *data,
	size_t data_len, uint64_t offset);
int nextufs_mknod_path(const struct nextufs_write_ctx *ctx,
	const char *image_path, const char *path, uint16_t mode, uint32_t rdev);
int nextufs_chmod_path(const struct nextufs_write_ctx *ctx,
	const char *image_path, const char *path, uint16_t mode);
int nextufs_chown_path(const struct nextufs_write_ctx *ctx,
	const char *image_path, const char *path, uid_t uid, gid_t gid);
int nextufs_utimes_path(const struct nextufs_write_ctx *ctx,
	const char *image_path, const char *path, uint32_t atime, uint32_t mtime);

#endif
