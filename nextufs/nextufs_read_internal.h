#ifndef NEXTUFS_INTERNAL_H
#define NEXTUFS_INTERNAL_H

#include "nextufs_read.h"

#define NEXTUFS_MAX_LOOKUP_DEPTH 16
#define NEXTUFS_MAX_PATH_LEN 4096

int nextufs_find_name_in_directory(const struct nextufs_image *img,
	const struct nextufs_inode *dirino, const char *target_name,
	unsigned *target_inode);

#endif
