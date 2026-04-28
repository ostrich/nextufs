#ifndef NEXTUFS_NODE_H
#define NEXTUFS_NODE_H

#include "nextufs_image.h"

#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>

#define NEXTUFS_ROOT_INODE 2U
#define NEXTUFS_IFMT 0170000U
#define NEXTUFS_IFIFO 0010000U
#define NEXTUFS_IFCHR 0020000U
#define NEXTUFS_IFDIR 0040000U
#define NEXTUFS_IFBLK 0060000U
#define NEXTUFS_IFREG 0100000U
#define NEXTUFS_IFLNK 0120000U
#define NEXTUFS_IFSOCK 0140000U

struct nextufs_inode {
	uint16_t mode;
	uint16_t nlink;
	uint16_t uid;
	uint16_t gid;
	uint64_t size;
	uint32_t atime;
	uint32_t mtime;
	uint32_t ctime;
	uint32_t db[12];
	uint32_t ib[3];
	uint32_t flags;
	uint32_t blocks;
	uint32_t gen;
};

struct nextufs_node {
	unsigned inode_no;
	off_t inode_off;
	struct nextufs_inode inode;
};

typedef int (*nextufs_dir_iter_cb)(uint32_t ino, const char *name,
	size_t name_len, void *ctx);
typedef int (*nextufs_dir_node_iter_cb)(const struct nextufs_node *node,
	const char *name, size_t name_len, void *ctx);

int nextufs_node_get_root(const struct nextufs_image *img,
	struct nextufs_node *node);
int nextufs_node_get_by_inode(const struct nextufs_image *img, unsigned inode_no,
	struct nextufs_node *node);
int nextufs_node_lookup(const struct nextufs_image *img, const char *path,
	int follow_final_symlink, struct nextufs_node *node);
int nextufs_node_stat(const struct nextufs_node *node, struct stat *st);
int nextufs_node_is_dir(const struct nextufs_node *node);
int nextufs_node_is_reg(const struct nextufs_node *node);
int nextufs_node_is_lnk(const struct nextufs_node *node);
int nextufs_node_check_access(const struct nextufs_node *node, uid_t uid,
	gid_t gid, int mask);
int nextufs_fs_statvfs(const struct nextufs_image *img, struct statvfs *stvfs);
int nextufs_inode_read(const struct nextufs_image *img,
	unsigned inode_no, struct nextufs_inode *ino, off_t *ino_off);
int nextufs_path_lookup(const struct nextufs_image *img, const char *path,
	unsigned *inode_no_out, struct nextufs_inode *inode_out,
	off_t *inode_off_out);
int nextufs_path_lookup_nofollow(const struct nextufs_image *img,
	const char *path, unsigned *inode_no_out, struct nextufs_inode *inode_out,
	off_t *inode_off_out);
int nextufs_inode_read_data(const struct nextufs_image *img,
	const struct nextufs_inode *ino, uint64_t start, uint8_t *buf,
	size_t buf_size, size_t *bytes_read);
int nextufs_inode_readlink(const struct nextufs_image *img,
	const struct nextufs_inode *ino, char *out, size_t out_size);
int nextufs_path_readlink(const struct nextufs_image *img, const char *path,
	char *out, size_t out_size);
int nextufs_directory_iterate(const struct nextufs_image *img,
	const struct nextufs_inode *dirino, nextufs_dir_iter_cb cb, void *ctx);
int nextufs_directory_iterate_nodes(const struct nextufs_image *img,
	const struct nextufs_inode *dirino, nextufs_dir_node_iter_cb cb, void *ctx);
int nextufs_directory_iterate_path(const struct nextufs_image *img,
	const char *path, int follow_final_symlink, nextufs_dir_iter_cb cb,
	void *ctx);
int nextufs_directory_iterate_nodes_path(const struct nextufs_image *img,
	const char *path, int follow_final_symlink, nextufs_dir_node_iter_cb cb,
	void *ctx);
int nextufs_path_read(const struct nextufs_image *img, const char *path,
	uint64_t start, uint8_t *buf, size_t buf_size, size_t *bytes_read);

#endif
