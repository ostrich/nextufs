#ifndef NEXTUFS_IMAGE_H
#define NEXTUFS_IMAGE_H

#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "nextufs_label.h"

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
	char label_name[NEXTUFS_LABEL_NAME_LEN + 1U];
	char rootpartition;
	int used_disk_label;
	struct nextufs_superblock sb;
};

enum nextufs_open_policy {
	NEXTUFS_OPEN_READ_ONLY = 0,
	NEXTUFS_OPEN_READ_WRITE = 1,
	NEXTUFS_OPEN_CREATE = 2,
	NEXTUFS_OPEN_OVERWRITE = 3,
};

struct nextufs_image_info {
	off_t image_size;
	off_t slice_base;
	off_t slice_size;
	off_t label_off;
	uint32_t label_version;
	uint32_t label_secsize;
	uint16_t label_front;
	char label_name[NEXTUFS_LABEL_NAME_LEN + 1U];
	char rootpartition;
	int used_disk_label;
	int source_is_container;
};

int nextufs_image_open_source(struct nextufs_image *img, const char *path,
	enum nextufs_open_policy policy);
int nextufs_image_open(struct nextufs_image *img, const char *path);
int nextufs_image_open_rw(struct nextufs_image *img, const char *path);
void nextufs_image_close(struct nextufs_image *img);
int nextufs_target_open(const char *path, enum nextufs_open_policy policy,
	mode_t mode, int *fd_out);
int nextufs_target_create_sized(const char *path,
	enum nextufs_open_policy policy, mode_t mode, uint64_t bytes);
int nextufs_image_pread(const struct nextufs_image *img, void *buf, size_t size,
	off_t offset);
int nextufs_image_pwrite(const struct nextufs_image *img, const void *buf,
	size_t size, off_t offset);
int nextufs_image_fsync(const struct nextufs_image *img);
int nextufs_source_extract_slice(const char *source_path, int out_fd);
void nextufs_image_info_get(const struct nextufs_image *img,
	struct nextufs_image_info *info);

#endif
