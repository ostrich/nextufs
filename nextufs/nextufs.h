#ifndef NEXTUFS_H
#define NEXTUFS_H

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

struct nextufs_superblock {
	uint32_t sb_off;
	uint32_t cg_off;
	uint32_t ino_off;
	uint32_t data_off;
	uint32_t cg_delta;
	uint32_t cg_cyc_mask;
	uint32_t write_time;
	uint32_t frag_count;
	uint32_t data_frag_count;
	uint32_t cg_count;
	uint32_t block_size;
	uint32_t frag_size;
	uint32_t frags_per_block;
	uint32_t minfree;
	uint32_t rotdelay;
	uint32_t rps;
	uint32_t block_mask;
	uint32_t frag_mask;
	uint32_t block_shift;
	uint32_t frag_shift_calc;
	uint32_t maxcontig;
	uint32_t maxbpg;
	uint32_t frag_shift;
	uint32_t fsbtodb;
	uint32_t super_size;
	uint32_t csum_mask;
	uint32_t csum_shift;
	uint32_t nindir;
	uint32_t sectors_per_frag;
	uint32_t optim;
	uint32_t inodes_per_block;
	uint32_t cyl_summary_addr;
	uint32_t csum_size;
	uint32_t cg_size;
	uint32_t tracks_per_cyl;
	uint32_t sectors_per_track;
	uint32_t sectors_per_cyl;
	uint32_t ncyl;
	uint32_t cpg;
	uint32_t inodes_per_group;
	uint32_t frags_per_group;
	uint32_t dir_count;
	uint32_t free_block_count;
	uint32_t free_inode_count;
	uint32_t free_frag_count;
	uint8_t state;
	uint32_t fs_magic;
};

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

struct nextufs_image {
	int fd;
	const void *backend_ops;
	void *backend_ctx;
	int writable;
	int source_is_container;
	off_t image_size;
	off_t slice_base;
	off_t slice_size;
	off_t label_off;
	uint32_t label_version;
	uint32_t label_secsize;
	uint16_t label_front;
	char rootpartition;
	int used_disk_label;
	struct nextufs_superblock sb;
};

struct nextufs_node {
	unsigned inode_no;
	off_t inode_off;
	struct nextufs_inode inode;
};

struct nextufs_probe_info {
	off_t image_size;
	off_t slice_base;
	off_t slice_size;
	off_t label_off;
	uint32_t label_version;
	uint32_t label_secsize;
	uint16_t label_front;
	char rootpartition;
	int used_disk_label;
	int source_is_container;
};

typedef int (*nextufs_dir_iter_cb)(uint32_t ino, const char *name,
	size_t name_len, void *ctx);
typedef int (*nextufs_dir_node_iter_cb)(const struct nextufs_node *node,
	const char *name, size_t name_len, void *ctx);

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

int nextufs_image_open(struct nextufs_image *img, const char *path);
int nextufs_image_open_rw(struct nextufs_image *img, const char *path);
void nextufs_image_close(struct nextufs_image *img);
int nextufs_source_extract_slice(const char *source_path, int out_fd);
void nextufs_probe_info_get(const struct nextufs_image *img,
	struct nextufs_probe_info *info);
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
