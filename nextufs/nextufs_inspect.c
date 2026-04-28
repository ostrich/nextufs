#include "nextufs_inspect.h"
#include "nextufs_size.h"

#include <string.h>

#define CSUM_SIZE 16U
#define UFS_SBLOCK_OFFSET 0x2000U

void
nextufs_inspect_collect(const struct nextufs_image *img, uint64_t backing_bytes,
    struct nextufs_inspect_info *info)
{
	memset(info, 0, sizeof(*info));
	info->backing_bytes = backing_bytes;
	info->image_bytes = (uint64_t)img->image_size;
	info->slice_base = (uint64_t)img->slice_base;
	info->slice_bytes = (uint64_t)img->slice_size;
	info->superblock_base = (uint64_t)img->slice_base + UFS_SBLOCK_OFFSET;
	info->source_is_container = img->source_is_container;
	info->used_disk_label = img->used_disk_label;
	info->label_version = img->label_version;
	info->label_off = (uint64_t)img->label_off;
	info->label_secsize = img->label_secsize;
	info->label_front = img->label_front;
	info->rootpartition = img->rootpartition;
	info->sb = img->sb;
	info->filesystem_bytes = (uint64_t)img->sb.frag_count *
	    img->sb.frag_size;
	info->trailing_slice_slack =
	    info->slice_bytes > info->filesystem_bytes ?
	    info->slice_bytes - info->filesystem_bytes : 0;
	info->csum_capacity_groups = img->sb.csum_size / CSUM_SIZE;
	if (info->backing_bytes == 0)
		info->backing_bytes = info->image_bytes;
}

const char *
nextufs_inspect_source_kind(const struct nextufs_inspect_info *info)
{
	if (info->source_is_container)
		return "container image";
	if (info->used_disk_label)
		return "labeled disk image";
	return "raw filesystem image";
}
