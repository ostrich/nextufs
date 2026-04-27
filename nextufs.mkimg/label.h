#ifndef NEXTUFS_MKIMG_LABEL_H
#define NEXTUFS_MKIMG_LABEL_H

#include <stdint.h>

#define NEXTUFS_LABEL_FRONT_PORCH_BYTES (160U * 1024U)
#define NEXTUFS_LABEL_SECTOR_SIZE 1024U

int nextufs_label_write(const char *target, uint64_t image_bytes,
    uint64_t slice_bytes, const char *label);

#endif
