#ifndef NEXTUFS_LABEL_H
#define NEXTUFS_LABEL_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define NEXTUFS_LABEL_FRONT_PORCH_BYTES (160U * 1024U)
#define NEXTUFS_LABEL_SECTOR_SIZE 1024U
#define NEXTUFS_LABEL_DEV_SECTOR_SIZE 512U
#define NEXTUFS_LABEL_SCAN_LIMIT (128U * 1024U)
#define NEXTUFS_LABEL_DECODE_SIZE 0x1c5aU
#define NEXTUFS_LABEL_PART_COUNT 8
#define NEXTUFS_LABEL_FSTYPE_LEN 8U

struct nextufs_label_partition {
	int present;
	uint32_t base_blocks;
	uint32_t size_blocks;
	uint16_t block_size;
	uint16_t frag_size;
	char type[NEXTUFS_LABEL_FSTYPE_LEN + 1U];
};

struct nextufs_disk_label {
	off_t label_off;
	uint32_t version;
	uint32_t label_blkno;
	uint32_t secsize;
	uint16_t front;
	char rootpartition;
	struct nextufs_label_partition part[NEXTUFS_LABEL_PART_COUNT];
	uint16_t checksum;
	int checksum_valid;
	int checksum_present;
};

int nextufs_label_write_path(const char *target, uint64_t image_bytes,
	uint64_t slice_bytes, const char *label);
int nextufs_label_decode(struct nextufs_disk_label *dl, const uint8_t *buf,
	size_t size, off_t off);
int nextufs_label_is_version(uint32_t version);
int nextufs_label_pick_slice(const struct nextufs_disk_label *dl,
	off_t *slice_base_out, off_t *slice_size_out);
int nextufs_label_validate_single_slice_fd(int fd, off_t slice_base);
int nextufs_label_patch_slice_size_fd(int fd, off_t slice_base,
	uint64_t slice_bytes, int *patched_count);

#endif
