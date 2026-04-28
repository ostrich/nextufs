#include "nextufs_report.h"
#include "nextufs_size.h"

#include <inttypes.h>
#include <string.h>

static void
report_size_line(FILE *out, const char *label, uint64_t bytes)
{
	fprintf(out, "  %-30s %" PRIu64 " bytes (%" PRIu64 " 1K sectors)\n",
	    label, bytes, bytes / 1024U);
}

void
nextufs_report_error(FILE *out, const char *command, const char *message)
{
	fprintf(out, "nextufs %s: %s\n", command, message);
}

void
nextufs_report_invalid_value(FILE *out, const char *command, const char *field,
    const char *value)
{
	fprintf(out, "nextufs %s: invalid %s '%s'\n", command, field, value);
}

void
nextufs_report_errno(FILE *out, const char *command, const char *action,
    const char *subject, int rc)
{
	int err = rc < 0 ? -rc : rc;

	if (subject != NULL && subject[0] != '\0') {
		fprintf(out, "nextufs %s: %s %s: %s\n", command, action,
		    subject, strerror(err));
		return;
	}
	fprintf(out, "nextufs %s: %s: %s\n", command, action, strerror(err));
}

static void
report_section(FILE *out, const char *name)
{
	fprintf(out, "%s\n", name);
}

static void
report_next_section(FILE *out, const char *name)
{
	fprintf(out, "\n%s\n", name);
}

static void
report_string_line(FILE *out, const char *label, const char *value)
{
	fprintf(out, "  %-30s %s\n", label, value);
}

static void
report_u32_line(FILE *out, const char *label, uint32_t value)
{
	fprintf(out, "  %-30s %" PRIu32 "\n", label, value);
}

static void
report_hex32_line(FILE *out, const char *label, uint32_t value)
{
	fprintf(out, "  %-30s 0x%08" PRIx32 "\n", label, value);
}

static void
report_offset_line(FILE *out, const char *label, uint64_t value)
{
	fprintf(out, "  %-30s 0x%jx (%" PRIu64 ")\n",
	    label, (uintmax_t)value, value);
}

static void
report_superblock_text(FILE *out, const struct nextufs_info *info)
{
	const struct nextufs_superblock *sb = &info->sb;

	report_hex32_line(out, "magic", sb->fs_magic);
	report_u32_line(out, "fsbtodb shift", sb->fsbtodb);
	report_u32_line(out, "indirect entries/block", sb->nindir);
	report_u32_line(out, "inodes/block", sb->inodes_per_block);
	fprintf(out, "  %-30s %" PRIu32 " / %" PRIu32 "\n",
	    "fragments total/data", sb->frag_count, sb->data_frag_count);
	fprintf(out, "  %-30s %" PRIu32 " / %u\n", "optimization/state",
	    sb->optim, (unsigned)sb->state);
}

static void
report_geometry_text(FILE *out, const struct nextufs_info *info)
{
	const struct nextufs_superblock *sb = &info->sb;

	report_u32_line(out, "block size", sb->block_size);
	report_u32_line(out, "fragment size", sb->frag_size);
	report_u32_line(out, "fragments/block", sb->frags_per_block);
	report_u32_line(out, "fragments/group", sb->frags_per_group);
	report_u32_line(out, "inodes/group", sb->inodes_per_group);
	report_u32_line(out, "cylinders/group", sb->cpg);
	report_u32_line(out, "cylinders", sb->ncyl);
	report_u32_line(out, "cylinder groups", sb->cg_count);
	fprintf(out, "  %-30s %" PRIu64 " groups\n",
	    "cylinder-summary capacity", info->csum_capacity_groups);
}

static void
report_free_space_text(FILE *out, const struct nextufs_info *info)
{
	const struct nextufs_superblock *sb = &info->sb;

	report_u32_line(out, "data fragments", sb->data_frag_count);
	report_u32_line(out, "free blocks", sb->free_block_count);
	report_u32_line(out, "free fragments", sb->free_frag_count);
	report_u32_line(out, "free inodes", sb->free_inode_count);
}

void
nextufs_report_info_text(FILE *out, const char *source,
    const struct nextufs_info *info)
{
	report_section(out, "Source");
	report_string_line(out, "path", source);
	report_string_line(out, "kind", nextufs_info_source_kind(info));
	report_size_line(out, "backing file size",
	    info->backing_bytes);
	report_size_line(out, "image size", info->image_bytes);

	if (info->used_disk_label) {
		char root[2];

		root[0] = info->rootpartition ? info->rootpartition : '?';
		root[1] = '\0';
		report_next_section(out, "Disk Label");
		report_hex32_line(out, "version", info->label_version);
		report_offset_line(out, "offset", info->label_off);
		report_u32_line(out, "sector size", info->label_secsize);
		report_u32_line(out, "front porch sectors", info->label_front);
		report_string_line(out, "root partition", root);
	}

	report_next_section(out, "Layout");
	report_offset_line(out, "slice base", info->slice_base);
	report_size_line(out, "slice size", info->slice_bytes);
	report_offset_line(out, "superblock base", info->superblock_base);
	report_size_line(out, "filesystem size",
	    info->filesystem_bytes);
	report_size_line(out, "trailing unused slice space",
	    info->trailing_slice_slack);
	report_size_line(out, "compatibility ceiling",
	    NEXTUFS_COMPAT_MAX_BYTES);

	report_next_section(out, "Geometry");
	report_geometry_text(out, info);

	report_next_section(out, "Free Space");
	report_free_space_text(out, info);

	report_next_section(out, "Superblock");
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
	fprintf(out, "  \"compatibility_ceiling_bytes\": %" PRIu64 ",\n",
	    NEXTUFS_COMPAT_MAX_BYTES);
	fprintf(out, "  \"cylinder_summary_capacity_groups\": %" PRIu64 ",\n",
	    info->csum_capacity_groups);
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
