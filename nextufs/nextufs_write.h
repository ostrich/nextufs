#ifndef NEXTUFS_WRITE_H
#define NEXTUFS_WRITE_H

#include <stddef.h>
#include <stdint.h>

int nextufs_create_small_file_rw(const char *image_path, const char *path,
	const void *data, size_t data_len);
int nextufs_create_file_from_hostfile_rw(const char *image_path,
	const char *path, const char *host_file);
int nextufs_unlink_path_rw(const char *image_path, const char *path);
int nextufs_mkdir_path_rw(const char *image_path, const char *path,
	uint16_t mode);
int nextufs_overwrite_file_rw(const char *image_path, const char *path,
	const void *data, size_t data_len);
int nextufs_append_file_rw(const char *image_path, const char *path,
	const void *data, size_t data_len);
int nextufs_rmdir_path_rw(const char *image_path, const char *path);
int nextufs_link_path_rw(const char *image_path, const char *source_path,
	const char *target_path);
int nextufs_symlink_path_rw(const char *image_path, const char *target,
	const char *link_path);
int nextufs_chmod_path_rw(const char *image_path, const char *path, uint16_t mode);
int nextufs_chown_path_rw(const char *image_path, const char *path,
	uint16_t uid, uint16_t gid);
int nextufs_utimes_path_rw(const char *image_path, const char *path,
	uint32_t atime, uint32_t mtime);

#endif
