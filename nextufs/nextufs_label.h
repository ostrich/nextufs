#ifndef NEXTUFS_LABEL_H
#define NEXTUFS_LABEL_H

#include <stdint.h>
#include <sys/types.h>

#define NEXTUFS_LABEL_FRONT_PORCH_BYTES (160U * 1024U)
#define NEXTUFS_LABEL_SECTOR_SIZE 1024U

int nextufs_label_write_path(const char *target, uint64_t image_bytes,
	uint64_t slice_bytes, const char *label);
int nextufs_label_validate_single_slice_fd(int fd, off_t slice_base);
int nextufs_label_patch_slice_size_fd(int fd, off_t slice_base,
	uint64_t slice_bytes, int *patched_count);

#endif
