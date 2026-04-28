#include "nextufs_report.h"
#include "nextufs_size.h"

#include <inttypes.h>

void
nextufs_report_size_line(FILE *out, const char *label, uint64_t bytes)
{
	fprintf(out, "%-30s %" PRIu64 " bytes (%" PRIu64 " 1K sectors)\n",
	    label, bytes, bytes / 1024U);
}

static void
report_superblock_text(FILE *out, const struct nextufs_info *info)
{
	const struct nextufs_superblock *sb = &info->sb;

	fprintf(out, "image size:            %" PRIu64 " bytes\n",
	    info->image_bytes);
	fprintf(out, "slice base:            0x%jx (%jd)\n",
	    (uintmax_t)info->slice_base, (intmax_t)info->slice_base);
	fprintf(out, "superblock base:       0x%jx (%jd)\n",
	    (uintmax_t)info->superblock_base,
	    (intmax_t)info->superblock_base);
	fprintf(out, "superblock magic:      0x%08" PRIx32 "\n",
	    sb->fs_magic);
	fprintf(out, "block size:            %" PRIu32 "\n", sb->block_size);
	fprintf(out, "fragment size:         %" PRIu32 "\n", sb->frag_size);
	fprintf(out, "frags/block:           %" PRIu32 "\n",
	    sb->frags_per_block);
	fprintf(out, "frags total/data:      %" PRIu32 " / %" PRIu32 "\n",
	    sb->frag_count, sb->data_frag_count);
	fprintf(out, "cylinder groups:       %" PRIu32 "\n", sb->cg_count);
	fprintf(out, "cylinders/group:       %" PRIu32 "\n", sb->cpg);
	fprintf(out, "inodes/group:          %" PRIu32 "\n",
	    sb->inodes_per_group);
	fprintf(out, "frags/group:           %" PRIu32 "\n",
	    sb->frags_per_group);
	fprintf(out, "inodes/block:          %" PRIu32 "\n",
	    sb->inodes_per_block);
	fprintf(out, "fsbtodb shift:         %" PRIu32 "\n", sb->fsbtodb);
	fprintf(out, "nindir:                %" PRIu32 "\n", sb->nindir);
	fprintf(out, "optim/state:           %" PRIu32 " / %u\n",
	    sb->optim, (unsigned)sb->state);
}

void
nextufs_report_info_text(FILE *out, const char *source,
    const struct nextufs_info *info)
{
	fprintf(out, "source: %s\n", source);
	fprintf(out, "source kind:                   %s\n",
	    nextufs_info_source_kind(info));
	nextufs_report_size_line(out, "backing file size:",
	    info->backing_bytes);
	if (info->used_disk_label) {
		nextufs_report_size_line(out, "slice base:", info->slice_base);
		nextufs_report_size_line(out, "slice size:", info->slice_bytes);
		fprintf(out, "root partition:               %c\n",
		    info->rootpartition);
	}
	nextufs_report_size_line(out, "filesystem size:",
	    info->filesystem_bytes);
	nextufs_report_size_line(out, "trailing unused slice space:",
	    info->trailing_slice_slack);
	nextufs_report_size_line(out, "compatibility ceiling:",
	    NEXTUFS_COMPAT_MAX_BYTES);
	fprintf(out, "block size:                   %" PRIu32 "\n",
	    info->sb.block_size);
	fprintf(out, "fragment size:                %" PRIu32 "\n",
	    info->sb.frag_size);
	fprintf(out, "fragments/block:              %" PRIu32 "\n",
	    info->sb.frags_per_block);
	fprintf(out, "cylinder groups:              %" PRIu32 "\n",
	    info->sb.cg_count);
	fprintf(out, "cylinder-summary capacity:    %" PRIu64 " groups\n",
	    info->csum_capacity_groups);
	fprintf(out, "fragments/group:              %" PRIu32 "\n",
	    info->sb.frags_per_group);
	fprintf(out, "inodes/group:                 %" PRIu32 "\n",
	    info->sb.inodes_per_group);
	fprintf(out, "cylinders/group:              %" PRIu32 "\n",
	    info->sb.cpg);
	fprintf(out, "cylinders:                    %" PRIu32 "\n",
	    info->sb.ncyl);
	fprintf(out, "data fragments:               %" PRIu32 "\n",
	    info->sb.data_frag_count);
	fprintf(out, "free blocks:                  %" PRIu32 "\n",
	    info->sb.free_block_count);
	fprintf(out, "free fragments:               %" PRIu32 "\n",
	    info->sb.free_frag_count);
	fprintf(out, "free inodes:                  %" PRIu32 "\n",
	    info->sb.free_inode_count);
	if (info->used_disk_label) {
		fprintf(out,
		    "disk label:                    version=0x%08x off=0x%jx secsize=%u front=%u root=%c\n",
		    info->label_version, (uintmax_t)info->label_off,
		    info->label_secsize, info->label_front,
		    info->rootpartition ? info->rootpartition : '?');
	}
	report_superblock_text(out, info);
}

static void
json_string(FILE *out, const char *s)
{
	fputc('"', out);
	for (; *s != '\0'; s++) {
		unsigned char c = (unsigned char)*s;

		if (c == '"' || c == '\\')
			fprintf(out, "\\%c", c);
		else if (c == '\n')
			fputs("\\n", out);
		else if (c == '\r')
			fputs("\\r", out);
		else if (c == '\t')
			fputs("\\t", out);
		else if (c < 32)
			fprintf(out, "\\u%04x", c);
		else
			fputc(c, out);
	}
	fputc('"', out);
}

void
nextufs_report_info_json(FILE *out, const char *source,
    const struct nextufs_info *info)
{
	fprintf(out, "{\n");
	fprintf(out, "  \"source\": ");
	json_string(out, source);
	fprintf(out, ",\n");
	fprintf(out, "  \"source_kind\": ");
	json_string(out, nextufs_info_source_kind(info));
	fprintf(out, ",\n");
	fprintf(out, "  \"backing_bytes\": %" PRIu64 ",\n",
	    info->backing_bytes);
	fprintf(out, "  \"image_bytes\": %" PRIu64 ",\n", info->image_bytes);
	fprintf(out, "  \"slice_base\": %" PRIu64 ",\n", info->slice_base);
	fprintf(out, "  \"slice_bytes\": %" PRIu64 ",\n", info->slice_bytes);
	fprintf(out, "  \"superblock_base\": %" PRIu64 ",\n",
	    info->superblock_base);
	fprintf(out, "  \"filesystem_bytes\": %" PRIu64 ",\n",
	    info->filesystem_bytes);
	fprintf(out, "  \"trailing_slice_slack\": %" PRIu64 ",\n",
	    info->trailing_slice_slack);
	fprintf(out, "  \"used_disk_label\": %s,\n",
	    info->used_disk_label ? "true" : "false");
	fprintf(out, "  \"label\": {\n");
	fprintf(out, "    \"version\": %" PRIu32 ",\n", info->label_version);
	fprintf(out, "    \"offset\": %" PRIu64 ",\n", info->label_off);
	fprintf(out, "    \"sector_size\": %" PRIu32 ",\n",
	    info->label_secsize);
	fprintf(out, "    \"front_porch_sectors\": %" PRIu16 ",\n",
	    info->label_front);
	fprintf(out, "    \"root_partition\": ");
	if (info->rootpartition)
		json_string(out, (char []){ info->rootpartition, '\0' });
	else
		fprintf(out, "null");
	fprintf(out, "\n  },\n");
	fprintf(out, "  \"filesystem\": {\n");
	fprintf(out, "    \"magic\": %" PRIu32 ",\n", info->sb.fs_magic);
	fprintf(out, "    \"block_size\": %" PRIu32 ",\n",
	    info->sb.block_size);
	fprintf(out, "    \"fragment_size\": %" PRIu32 ",\n",
	    info->sb.frag_size);
	fprintf(out, "    \"fragments_per_block\": %" PRIu32 ",\n",
	    info->sb.frags_per_block);
	fprintf(out, "    \"fragment_count\": %" PRIu32 ",\n",
	    info->sb.frag_count);
	fprintf(out, "    \"data_fragment_count\": %" PRIu32 ",\n",
	    info->sb.data_frag_count);
	fprintf(out, "    \"cylinder_groups\": %" PRIu32 ",\n",
	    info->sb.cg_count);
	fprintf(out, "    \"cylinders_per_group\": %" PRIu32 ",\n",
	    info->sb.cpg);
	fprintf(out, "    \"inodes_per_group\": %" PRIu32 ",\n",
	    info->sb.inodes_per_group);
	fprintf(out, "    \"fragments_per_group\": %" PRIu32 ",\n",
	    info->sb.frags_per_group);
	fprintf(out, "    \"free_blocks\": %" PRIu32 ",\n",
	    info->sb.free_block_count);
	fprintf(out, "    \"free_fragments\": %" PRIu32 ",\n",
	    info->sb.free_frag_count);
	fprintf(out, "    \"free_inodes\": %" PRIu32 "\n",
	    info->sb.free_inode_count);
	fprintf(out, "  }\n");
	fprintf(out, "}\n");
}
