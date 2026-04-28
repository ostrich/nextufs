#ifndef NEXTUFS_INFO_H
#define NEXTUFS_INFO_H

#include "nextufs.h"

#include <stdint.h>

struct nextufs_info {
	uint64_t backing_bytes;
	uint64_t image_bytes;
	uint64_t slice_base;
	uint64_t slice_bytes;
	uint64_t superblock_base;
	uint64_t filesystem_bytes;
	uint64_t trailing_slice_slack;
	uint64_t csum_capacity_groups;
	int source_is_container;
	int used_disk_label;
	uint32_t label_version;
	uint64_t label_off;
	uint32_t label_secsize;
	uint16_t label_front;
	char rootpartition;
	struct nextufs_superblock sb;
};

void nextufs_info_collect(const struct nextufs_image *img,
	uint64_t backing_bytes, struct nextufs_info *info);
const char *nextufs_info_source_kind(const struct nextufs_info *info);

#endif
