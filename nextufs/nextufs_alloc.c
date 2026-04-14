#include "nextufs_internal.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int nextufs__block_is_free(const uint8_t *free_map, uint32_t frag_base,
	uint32_t frags);
static void nextufs__clear_block_bits(uint8_t *free_map, uint32_t frag_base,
	uint32_t frags);
static int nextufs__set_run_bits(const uint8_t *free_map, uint32_t frag_base,
	uint32_t frags);
static void nextufs__count_free_runs_in_block(const uint8_t *free_map,
	uint32_t block_frag_base, uint32_t frags_per_block, uint32_t *counts);
static void nextufs__apply_cg_frsum_delta(uint8_t *buf, const uint32_t *before,
	const uint32_t *after, uint32_t frags_per_block);
static int nextufs__allocate_frags_from_free_block_in_group(
	const struct nextufs_image *img, unsigned cg, uint32_t frags_needed,
	uint32_t *frag_out);

int
nextufs__allocate_inode_in_group(const struct nextufs_image *img, unsigned cg,
    uint16_t mode, unsigned *inode_no_out)
{
	uint8_t *buf;
	off_t cg_off;
	uint32_t cg_magic;
	uint32_t now;
	uint32_t ipref = 0;
	uint32_t start;
	uint32_t i;
	int rc;

	buf = malloc(img->sb.cg_size);
	if (buf == NULL)
		return -ENOMEM;
	cg_off = nextufs__cg_block_offset(img, cg);
	rc = nextufs__read_exact(img->fd, buf, img->sb.cg_size, cg_off);
	if (rc < 0) {
		free(buf);
		return rc;
	}
	cg_magic = nextufs__read_be32(buf + CG_MAGIC_OFF);
	if (cg_magic != CG_MAGIC) {
		free(buf);
		return -EINVAL;
	}
	if (nextufs__read_be32(buf + CG_CS_NIFREE_OFF) == 0) {
		free(buf);
		return -ENOSPC;
	}
	start = nextufs__read_be32(buf + CG_IROTOR_OFF);
	if (start >= img->sb.inodes_per_group)
		start = 0;
	for (i = 0; i < img->sb.inodes_per_group; i++) {
		uint32_t idx = (start + i) % img->sb.inodes_per_group;
		uint8_t *bytep = &buf[CG_IUSED_OFF + (idx / 8)];
		uint8_t bit = (uint8_t)(1U << (idx % 8));

		if ((*bytep & bit) == 0) {
			ipref = idx;
			*bytep |= bit;
			break;
		}
	}
	if (i == img->sb.inodes_per_group) {
		free(buf);
		return -ENOSPC;
	}
	nextufs__write_be32(buf + CG_IROTOR_OFF, ipref);
	nextufs__write_be32(buf + CG_CS_NIFREE_OFF,
	    nextufs__read_be32(buf + CG_CS_NIFREE_OFF) - 1U);
	now = (uint32_t)time(NULL);
	nextufs__write_be32(buf + CG_TIME_OFF, now);
	if ((mode & NEXTUFS_IFMT) == NEXTUFS_IFDIR)
		nextufs__write_be32(buf + CG_CS_NDIR_OFF,
		    nextufs__read_be32(buf + CG_CS_NDIR_OFF) + 1U);
	rc = nextufs__write_exact(img->fd, buf, img->sb.cg_size, cg_off);
	free(buf);
	if (rc < 0)
		return rc;
	rc = nextufs__update_summary_counts(img, cg,
	    ((mode & NEXTUFS_IFMT) == NEXTUFS_IFDIR) ? 1 : 0, 0, -1, 0);
	if (rc < 0)
		return rc;
	*inode_no_out = (cg * img->sb.inodes_per_group) + ipref;
	return 0;
}

int
nextufs__free_inode_in_group(const struct nextufs_image *img, unsigned inode_no,
    uint16_t mode)
{
	uint8_t *buf;
	off_t cg_off;
	unsigned cg;
	uint32_t local_ino;
	uint32_t now;
	int rc;
	uint8_t *bytep;
	uint8_t bit;

	cg = inode_no / img->sb.inodes_per_group;
	local_ino = inode_no % img->sb.inodes_per_group;
	buf = malloc(img->sb.cg_size);
	if (buf == NULL)
		return -ENOMEM;
	cg_off = nextufs__cg_block_offset(img, cg);
	rc = nextufs__read_exact(img->fd, buf, img->sb.cg_size, cg_off);
	if (rc < 0) {
		free(buf);
		return rc;
	}
	if (nextufs__read_be32(buf + CG_MAGIC_OFF) != CG_MAGIC) {
		free(buf);
		return -EINVAL;
	}
	bytep = &buf[CG_IUSED_OFF + (local_ino / 8)];
	bit = (uint8_t)(1U << (local_ino % 8));
	if ((*bytep & bit) == 0) {
		free(buf);
		return -EINVAL;
	}
	*bytep &= (uint8_t)~bit;
	nextufs__write_be32(buf + CG_CS_NIFREE_OFF,
	    nextufs__read_be32(buf + CG_CS_NIFREE_OFF) + 1U);
	now = (uint32_t)time(NULL);
	nextufs__write_be32(buf + CG_TIME_OFF, now);
	if ((mode & NEXTUFS_IFMT) == NEXTUFS_IFDIR)
		nextufs__write_be32(buf + CG_CS_NDIR_OFF,
		    nextufs__read_be32(buf + CG_CS_NDIR_OFF) - 1U);
	rc = nextufs__write_exact(img->fd, buf, img->sb.cg_size, cg_off);
	free(buf);
	if (rc < 0)
		return rc;
	return nextufs__update_summary_counts(img, cg,
	    ((mode & NEXTUFS_IFMT) == NEXTUFS_IFDIR) ? -1 : 0, 0, 1, 0);
}

static int
nextufs__block_is_free(const uint8_t *free_map, uint32_t frag_base, uint32_t frags)
{
	uint32_t i;

	for (i = 0; i < frags; i++) {
		if ((free_map[(frag_base + i) / 8] &
		    (1U << ((frag_base + i) % 8))) == 0)
			return 0;
	}
	return 1;
}

static void
nextufs__clear_block_bits(uint8_t *free_map, uint32_t frag_base, uint32_t frags)
{
	uint32_t i;

	for (i = 0; i < frags; i++)
		free_map[(frag_base + i) / 8] &= (uint8_t)~(1U << ((frag_base + i) % 8));
}

static int
nextufs__set_run_bits(const uint8_t *free_map, uint32_t frag_base, uint32_t frags)
{
	uint32_t i;

	for (i = 0; i < frags; i++) {
		if ((free_map[(frag_base + i) / 8] &
		    (1U << ((frag_base + i) % 8))) == 0)
			return 0;
	}
	return 1;
}

static void
nextufs__count_free_runs_in_block(const uint8_t *free_map, uint32_t block_frag_base,
    uint32_t frags_per_block, uint32_t *counts)
{
	uint32_t i;

	memset(counts, 0, sizeof(uint32_t) * (frags_per_block + 1U));
	i = 0;
	while (i < frags_per_block) {
		uint32_t run = 0;

		while (i + run < frags_per_block &&
		    (free_map[(block_frag_base + i + run) / 8] &
		    (1U << ((block_frag_base + i + run) % 8))) != 0) {
			run++;
		}
		if (run > 0 && run < frags_per_block)
			counts[run]++;
		i += run > 0 ? run : 1;
	}
}

static void
nextufs__apply_cg_frsum_delta(uint8_t *buf, const uint32_t *before,
    const uint32_t *after, uint32_t frags_per_block)
{
	uint32_t i;

	for (i = 1; i < frags_per_block; i++) {
		uint32_t frsum_off = 52U + (i * 4U);
		uint32_t v = nextufs__read_be32(buf + frsum_off);
		int32_t delta = (int32_t)after[i] - (int32_t)before[i];

		if (delta != 0)
			nextufs__write_be32(buf + frsum_off, (uint32_t)(v + delta));
	}
}

static int
nextufs__allocate_frags_from_free_block_in_group(const struct nextufs_image *img,
    unsigned cg, uint32_t frags_needed, uint32_t *frag_out)
{
	uint8_t *buf;
	off_t cg_off;
	uint32_t cg_magic;
	uint32_t local_frag;
	uint32_t now;
	uint32_t free_frags;
	int rc;

	if (frags_needed == 0 || frags_needed > img->sb.frags_per_block)
		return -EINVAL;
	buf = malloc(img->sb.cg_size);
	if (buf == NULL)
		return -ENOMEM;
	cg_off = nextufs__cg_block_offset(img, cg);
	rc = nextufs__read_exact(img->fd, buf, img->sb.cg_size, cg_off);
	if (rc < 0) {
		free(buf);
		return rc;
	}
	cg_magic = nextufs__read_be32(buf + CG_MAGIC_OFF);
	if (cg_magic != CG_MAGIC) {
		free(buf);
		return -EINVAL;
	}
	if (nextufs__read_be32(buf + CG_CS_NBFREE_OFF) == 0) {
		free(buf);
		return -ENOSPC;
	}
	for (local_frag = img->sb.data_off;
	    local_frag + img->sb.frags_per_block <= img->sb.frags_per_group;
	    local_frag += img->sb.frags_per_block) {
		if (!nextufs__block_is_free(buf + CG_FREE_OFF, local_frag,
		    img->sb.frags_per_block))
			continue;
		nextufs__clear_block_bits(buf + CG_FREE_OFF, local_frag, frags_needed);
		nextufs__write_be32(buf + CG_ROTOR_OFF, local_frag);
		nextufs__write_be32(buf + CG_CS_NBFREE_OFF,
		    nextufs__read_be32(buf + CG_CS_NBFREE_OFF) - 1U);
		free_frags = img->sb.frags_per_block - frags_needed;
		nextufs__write_be32(buf + CG_CS_NFFREE_OFF,
		    nextufs__read_be32(buf + CG_CS_NFFREE_OFF) + free_frags);
		if (free_frags > 0) {
			uint32_t frsum_off = 52U + (free_frags * 4U);

			nextufs__write_be32(buf + frsum_off,
			    nextufs__read_be32(buf + frsum_off) + 1U);
		}
		now = (uint32_t)time(NULL);
		nextufs__write_be32(buf + CG_TIME_OFF, now);
		{
			uint32_t cylno;
			uint32_t rotpos;
			uint32_t btot;
			uint16_t bpos;
			uint8_t *btotp;
			uint8_t *bposp;

			cylno = (local_frag * img->sb.sectors_per_frag) /
			    img->sb.sectors_per_cyl;
			rotpos = ((local_frag * img->sb.sectors_per_frag) %
			    img->sb.sectors_per_cyl %
			    img->sb.sectors_per_track) * NRPOS /
			    img->sb.sectors_per_track;
			if (cylno < 32 && rotpos < NRPOS) {
				btotp = buf + CG_BTOT_OFF + (cylno * 4U);
				btot = nextufs__read_be32(btotp);
				nextufs__write_be32(btotp, btot - 1U);
				bposp = buf + CG_BPOS_OFF +
				    ((cylno * NRPOS + rotpos) * 2U);
				bpos = nextufs__read_be16(bposp);
				nextufs__write_be16(bposp, (uint16_t)(bpos - 1U));
			}
		}
		rc = nextufs__write_exact(img->fd, buf, img->sb.cg_size, cg_off);
		free(buf);
		if (rc < 0)
			return rc;
		rc = nextufs__update_summary_counts(img, cg, 0, -1, 0,
		    (int32_t)free_frags);
		if (rc < 0)
			return rc;
		*frag_out = cg * img->sb.frags_per_group + local_frag;
		return 0;
	}
	free(buf);
	return -ENOSPC;
}

int
nextufs__allocate_frags_anycg(const struct nextufs_image *img, unsigned preferred_cg,
    uint32_t frags_needed, uint32_t *frag_out)
{
	uint32_t i;

	for (i = 0; i < img->sb.cg_count; i++) {
		unsigned cg = (preferred_cg + i) % img->sb.cg_count;
		int rc = nextufs__allocate_frags_from_free_block_in_group(img, cg,
		    frags_needed, frag_out);
		if (rc == 0)
			return 0;
		if (rc != -ENOSPC)
			return rc;
	}
	return -ENOSPC;
}

int
nextufs__allocate_full_block_anycg(const struct nextufs_image *img,
    unsigned preferred_cg, uint32_t *frag_out)
{
	return nextufs__allocate_frags_anycg(img, preferred_cg,
	    img->sb.frags_per_block, frag_out);
}

int
nextufs__extend_fragment_run(const struct nextufs_image *img, uint32_t frag_base,
    uint32_t old_frags, uint32_t new_frags)
{
	uint8_t *buf;
	off_t cg_off;
	unsigned cg;
	uint32_t local_frag;
	uint32_t block_frag_base;
	uint32_t added_frags;
	uint32_t before[9];
	uint32_t after[9];
	uint32_t now;
	int rc;

	if (new_frags <= old_frags || new_frags > img->sb.frags_per_block)
		return -EINVAL;
	cg = frag_base / img->sb.frags_per_group;
	local_frag = frag_base % img->sb.frags_per_group;
	if ((local_frag % img->sb.frags_per_block) + new_frags >
	    img->sb.frags_per_block)
		return -ENOSPC;
	buf = malloc(img->sb.cg_size);
	if (buf == NULL)
		return -ENOMEM;
	cg_off = nextufs__cg_block_offset(img, cg);
	rc = nextufs__read_exact(img->fd, buf, img->sb.cg_size, cg_off);
	if (rc < 0) {
		free(buf);
		return rc;
	}
	if (nextufs__read_be32(buf + CG_MAGIC_OFF) != CG_MAGIC) {
		free(buf);
		return -EINVAL;
	}
	added_frags = new_frags - old_frags;
	block_frag_base = local_frag - (local_frag % img->sb.frags_per_block);
	if (!nextufs__set_run_bits(buf + CG_FREE_OFF, local_frag + old_frags,
	    added_frags)) {
		free(buf);
		return -ENOSPC;
	}
	nextufs__count_free_runs_in_block(buf + CG_FREE_OFF, block_frag_base,
	    img->sb.frags_per_block, before);
	nextufs__clear_block_bits(buf + CG_FREE_OFF, local_frag + old_frags,
	    added_frags);
	nextufs__count_free_runs_in_block(buf + CG_FREE_OFF, block_frag_base,
	    img->sb.frags_per_block, after);
	nextufs__apply_cg_frsum_delta(buf, before, after, img->sb.frags_per_block);
	nextufs__write_be32(buf + CG_CS_NFFREE_OFF,
	    nextufs__read_be32(buf + CG_CS_NFFREE_OFF) - added_frags);
	now = (uint32_t)time(NULL);
	nextufs__write_be32(buf + CG_TIME_OFF, now);
	rc = nextufs__write_exact(img->fd, buf, img->sb.cg_size, cg_off);
	free(buf);
	if (rc < 0)
		return rc;
	return nextufs__update_summary_counts(img, cg, 0, 0, 0,
	    -(int32_t)added_frags);
}

int
nextufs__free_fragment_run(const struct nextufs_image *img, uint32_t frag_base,
    uint32_t frags)
{
	uint8_t *buf;
	off_t cg_off;
	unsigned cg;
	uint32_t local_frag;
	uint32_t block_frag_base;
	uint32_t before[9];
	uint32_t after[9];
	uint32_t i;
	uint32_t now;
	int32_t d_nbfree = 0;
	int32_t d_nffree;
	int rc;

	if (frags == 0 || frags > img->sb.frags_per_block)
		return -EINVAL;
	cg = frag_base / img->sb.frags_per_group;
	local_frag = frag_base % img->sb.frags_per_group;
	block_frag_base = local_frag - (local_frag % img->sb.frags_per_block);
	buf = malloc(img->sb.cg_size);
	if (buf == NULL)
		return -ENOMEM;
	cg_off = nextufs__cg_block_offset(img, cg);
	rc = nextufs__read_exact(img->fd, buf, img->sb.cg_size, cg_off);
	if (rc < 0) {
		free(buf);
		return rc;
	}
	if (nextufs__read_be32(buf + CG_MAGIC_OFF) != CG_MAGIC) {
		free(buf);
		return -EINVAL;
	}
	nextufs__count_free_runs_in_block(buf + CG_FREE_OFF, block_frag_base,
	    img->sb.frags_per_block, before);
	for (i = 0; i < frags; i++) {
		uint32_t bit = local_frag + i;

		if ((buf[CG_FREE_OFF + (bit / 8)] & (1U << (bit % 8))) != 0) {
			free(buf);
			return -EINVAL;
		}
		buf[CG_FREE_OFF + (bit / 8)] |= (uint8_t)(1U << (bit % 8));
	}
	nextufs__count_free_runs_in_block(buf + CG_FREE_OFF, block_frag_base,
	    img->sb.frags_per_block, after);
	nextufs__apply_cg_frsum_delta(buf, before, after, img->sb.frags_per_block);
	d_nffree = (int32_t)frags;
	if (nextufs__block_is_free(buf + CG_FREE_OFF, block_frag_base,
	    img->sb.frags_per_block)) {
		uint32_t cylno;
		uint32_t rotpos;
		uint8_t *btotp;
		uint8_t *bposp;

		nextufs__write_be32(buf + CG_CS_NFFREE_OFF,
		    nextufs__read_be32(buf + CG_CS_NFFREE_OFF) + frags -
		    img->sb.frags_per_block);
		nextufs__write_be32(buf + CG_CS_NBFREE_OFF,
		    nextufs__read_be32(buf + CG_CS_NBFREE_OFF) + 1U);
		d_nffree -= (int32_t)img->sb.frags_per_block;
		d_nbfree = 1;
		cylno = (block_frag_base * img->sb.sectors_per_frag) /
		    img->sb.sectors_per_cyl;
		rotpos = ((block_frag_base * img->sb.sectors_per_frag) %
		    img->sb.sectors_per_cyl % img->sb.sectors_per_track) * NRPOS /
		    img->sb.sectors_per_track;
		if (cylno < 32 && rotpos < NRPOS) {
			btotp = buf + CG_BTOT_OFF + (cylno * 4U);
			nextufs__write_be32(btotp, nextufs__read_be32(btotp) + 1U);
			bposp = buf + CG_BPOS_OFF + ((cylno * NRPOS + rotpos) * 2U);
			nextufs__write_be16(bposp,
			    (uint16_t)(nextufs__read_be16(bposp) + 1U));
		}
	} else {
		nextufs__write_be32(buf + CG_CS_NFFREE_OFF,
		    nextufs__read_be32(buf + CG_CS_NFFREE_OFF) + frags);
	}
	now = (uint32_t)time(NULL);
	nextufs__write_be32(buf + CG_TIME_OFF, now);
	rc = nextufs__write_exact(img->fd, buf, img->sb.cg_size, cg_off);
	free(buf);
	if (rc < 0)
		return rc;
	return nextufs__update_summary_counts(img, cg, 0, d_nbfree, 0, d_nffree);
}

int
nextufs__reallocate_fragment_run(const struct nextufs_image *img, uint32_t old_frag,
    uint32_t old_frags, uint32_t new_frags, uint32_t preferred_cg,
    uint32_t *new_frag_out)
{
	uint8_t *tmp;
	uint32_t new_frag;
	size_t old_bytes;
	size_t new_bytes;
	int rc;

	old_bytes = (size_t)old_frags * img->sb.frag_size;
	new_bytes = (size_t)new_frags * img->sb.frag_size;
	tmp = calloc(1, new_bytes);
	if (tmp == NULL)
		return -ENOMEM;
	rc = nextufs__read_exact(img->fd, tmp, old_bytes,
	    img->slice_base + ((off_t)old_frag * img->sb.frag_size));
	if (rc < 0) {
		free(tmp);
		return rc;
	}
	rc = nextufs__allocate_frags_anycg(img, preferred_cg, new_frags, &new_frag);
	if (rc < 0) {
		free(tmp);
		return rc;
	}
	rc = nextufs__write_exact(img->fd, tmp, new_bytes,
	    img->slice_base + ((off_t)new_frag * img->sb.frag_size));
	free(tmp);
	if (rc < 0)
		return rc;
	rc = nextufs__free_fragment_run(img, old_frag, old_frags);
	if (rc < 0)
		return rc;
	*new_frag_out = new_frag;
	return 0;
}

int
nextufs__read_indirect_entry(const struct nextufs_image *img, uint32_t block_frag,
    uint64_t entry_index, uint32_t *entry_out)
{
	uint8_t raw[sizeof(uint32_t)];
	uint64_t max_entries;
	int rc;

	max_entries = img->sb.block_size / sizeof(uint32_t);
	if (entry_index >= max_entries)
		return -EINVAL;
	rc = nextufs__read_exact(img->fd, raw, sizeof(raw),
	    img->slice_base + ((off_t)block_frag * img->sb.frag_size) +
	    (off_t)(entry_index * sizeof(uint32_t)));
	if (rc < 0)
		return rc;
	*entry_out = nextufs__read_be32(raw);
	return 0;
}

int
nextufs__write_indirect_entry(const struct nextufs_image *img, uint32_t block_frag,
    uint64_t entry_index, uint32_t entry_value)
{
	uint8_t raw[sizeof(uint32_t)];
	uint64_t max_entries;

	max_entries = img->sb.block_size / sizeof(uint32_t);
	if (entry_index >= max_entries)
		return -EINVAL;
	nextufs__write_be32(raw, entry_value);
	return nextufs__write_exact(img->fd, raw, sizeof(raw),
	    img->slice_base + ((off_t)block_frag * img->sb.frag_size) +
	    (off_t)(entry_index * sizeof(uint32_t)));
}

static int
nextufs__allocate_zeroed_full_block(const struct nextufs_image *img,
    unsigned preferred_cg, uint32_t *frag_out)
{
	uint8_t *zero_block;
	uint32_t frag;
	int rc;

	rc = nextufs__allocate_full_block_anycg(img, preferred_cg, &frag);
	if (rc < 0)
		return rc;
	zero_block = calloc(1, img->sb.block_size);
	if (zero_block == NULL) {
		nextufs__free_fragment_run(img, frag, img->sb.frags_per_block);
		return -ENOMEM;
	}
	rc = nextufs__write_exact(img->fd, zero_block, img->sb.block_size,
	    img->slice_base + ((off_t)frag * img->sb.frag_size));
	free(zero_block);
	if (rc < 0) {
		nextufs__free_fragment_run(img, frag, img->sb.frags_per_block);
		return rc;
	}
	*frag_out = frag;
	return 0;
}

static uint64_t
nextufs__entries_per_block(const struct nextufs_image *img)
{
	return img->sb.block_size / sizeof(uint32_t);
}

static int
nextufs__set_indirect_data_block(const struct nextufs_image *img, uint32_t block_frag,
    unsigned level, uint64_t logical_index, uint32_t data_frag,
    unsigned preferred_cg, uint32_t *metadata_frags_out)
{
	uint64_t entries_per_block;
	uint64_t span;
	uint64_t entry_index;
	uint64_t remainder;
	uint32_t next_frag;
	int rc;

	entries_per_block = nextufs__entries_per_block(img);
	if (level == 0)
		return -EINVAL;
	if (level == 1)
		return nextufs__write_indirect_entry(img, block_frag, logical_index,
		    data_frag);
	span = 1;
	for (unsigned i = 1; i < level; i++)
		span *= entries_per_block;
	entry_index = logical_index / span;
	remainder = logical_index % span;
	rc = nextufs__read_indirect_entry(img, block_frag, entry_index, &next_frag);
	if (rc < 0)
		return rc;
	if (next_frag == 0) {
		rc = nextufs__allocate_zeroed_full_block(img, preferred_cg, &next_frag);
		if (rc < 0)
			return rc;
		rc = nextufs__write_indirect_entry(img, block_frag, entry_index, next_frag);
		if (rc < 0) {
			nextufs__free_fragment_run(img, next_frag, img->sb.frags_per_block);
			return rc;
		}
		*metadata_frags_out += img->sb.frags_per_block;
	}
	return nextufs__set_indirect_data_block(img, next_frag, level - 1,
	    remainder, data_frag, preferred_cg, metadata_frags_out);
}

static int
nextufs__set_inode_data_block(const struct nextufs_image *img,
    struct nextufs_inode *ino_out, uint32_t logical_block, uint32_t data_frag,
    unsigned preferred_cg, uint32_t *metadata_frags_out)
{
	uint64_t local_index;
	uint64_t span;
	uint64_t entries_per_block;
	uint32_t indirect_slot;
	uint32_t root_frag;
	int rc;

	if (logical_block < 12) {
		ino_out->db[logical_block] = data_frag;
		return 0;
	}
	local_index = logical_block - 12U;
	entries_per_block = nextufs__entries_per_block(img);
	span = entries_per_block;
	for (indirect_slot = 0; indirect_slot < 3; indirect_slot++) {
		if (local_index < span) {
			root_frag = ino_out->ib[indirect_slot];
			if (root_frag == 0) {
				rc = nextufs__allocate_zeroed_full_block(img,
				    preferred_cg, &root_frag);
				if (rc < 0)
					return rc;
				ino_out->ib[indirect_slot] = root_frag;
				*metadata_frags_out += img->sb.frags_per_block;
			}
			return nextufs__set_indirect_data_block(img, root_frag,
			    indirect_slot + 1U, local_index, data_frag, preferred_cg,
			    metadata_frags_out);
		}
		local_index -= span;
		span *= entries_per_block;
	}
	return -EFBIG;
}

static int
nextufs__resolve_indirect_data_block(const struct nextufs_image *img,
    uint32_t block_frag, unsigned level, uint64_t logical_index,
    uint32_t *data_frag_out)
{
	uint64_t entries_per_block;
	uint64_t span;
	uint64_t entry_index;
	uint64_t remainder;
	uint32_t next_frag;
	int rc;

	if (block_frag == 0) {
		*data_frag_out = 0;
		return 0;
	}
	entries_per_block = nextufs__entries_per_block(img);
	if (level == 1)
		return nextufs__read_indirect_entry(img, block_frag, logical_index,
		    data_frag_out);
	span = 1;
	for (unsigned i = 1; i < level; i++)
		span *= entries_per_block;
	entry_index = logical_index / span;
	remainder = logical_index % span;
	rc = nextufs__read_indirect_entry(img, block_frag, entry_index, &next_frag);
	if (rc < 0)
		return rc;
	return nextufs__resolve_indirect_data_block(img, next_frag, level - 1,
	    remainder, data_frag_out);
}

static int
nextufs__resolve_inode_data_block(const struct nextufs_image *img,
    const struct nextufs_inode *ino, uint32_t logical_block, uint32_t *data_frag_out)
{
	uint64_t local_index;
	uint64_t span;
	uint64_t entries_per_block;
	uint32_t indirect_slot;

	if (logical_block < 12) {
		*data_frag_out = ino->db[logical_block];
		return 0;
	}
	local_index = logical_block - 12U;
	entries_per_block = nextufs__entries_per_block(img);
	span = entries_per_block;
	for (indirect_slot = 0; indirect_slot < 3; indirect_slot++) {
		if (local_index < span)
			return nextufs__resolve_indirect_data_block(img,
			    ino->ib[indirect_slot], indirect_slot + 1U, local_index,
			    data_frag_out);
		local_index -= span;
		span *= entries_per_block;
	}
	return -EFBIG;
}

static int
nextufs__free_indirect_metadata_tree(const struct nextufs_image *img,
    uint32_t block_frag, unsigned level)
{
	uint64_t entries_per_block;
	uint64_t i;
	int rc;

	if (block_frag == 0)
		return 0;
	if (level > 1) {
		uint32_t child_frag;

		entries_per_block = nextufs__entries_per_block(img);
		for (i = 0; i < entries_per_block; i++) {
			rc = nextufs__read_indirect_entry(img, block_frag, i, &child_frag);
			if (rc < 0)
				return rc;
			if (child_frag == 0)
				continue;
			rc = nextufs__free_indirect_metadata_tree(img, child_frag, level - 1);
			if (rc < 0)
				return rc;
		}
	}
	return nextufs__free_fragment_run(img, block_frag, img->sb.frags_per_block);
}

int
nextufs__allocate_data_for_inode(const struct nextufs_image *img,
    unsigned preferred_cg, const uint8_t *data, size_t data_len,
    struct nextufs_inode *ino_out)
{
	uint64_t remaining;
	uint32_t logical_block;
	uint32_t total_alloc_frags;
	int rc;

	total_alloc_frags = 0;
	remaining = data_len;
	for (logical_block = 0; remaining > 0; logical_block++) {
		uint32_t frags_needed;
		uint32_t alloc_frag;
		size_t chunk_bytes;
		size_t alloc_bytes;
		uint8_t *block;

		chunk_bytes = remaining > img->sb.block_size ?
		    img->sb.block_size : (size_t)remaining;
		frags_needed = (uint32_t)((chunk_bytes + img->sb.frag_size - 1U) /
		    img->sb.frag_size);
		rc = nextufs__allocate_frags_anycg(img, preferred_cg, frags_needed,
		    &alloc_frag);
		if (rc < 0)
			goto fail;
		rc = nextufs__set_inode_data_block(img, ino_out, logical_block,
		    alloc_frag, preferred_cg, &total_alloc_frags);
		if (rc < 0) {
			nextufs__free_fragment_run(img, alloc_frag, frags_needed);
			goto fail;
		}
		total_alloc_frags += frags_needed;
		ino_out->size = ((uint64_t)logical_block * img->sb.block_size) +
		    chunk_bytes;
		ino_out->blocks = total_alloc_frags * (img->sb.frag_size / DEV_BSIZE);
		alloc_bytes = (size_t)frags_needed * img->sb.frag_size;
		block = calloc(1, alloc_bytes);
		if (block == NULL) {
			rc = -ENOMEM;
			goto fail;
		}
		memcpy(block, data + ((size_t)logical_block * img->sb.block_size),
		    chunk_bytes);
		rc = nextufs__write_exact(img->fd, block, alloc_bytes,
		    img->slice_base + ((off_t)alloc_frag * img->sb.frag_size));
		free(block);
		if (rc < 0)
			goto fail;
		remaining -= chunk_bytes;
	}
	ino_out->size = data_len;
	ino_out->blocks = total_alloc_frags * (img->sb.frag_size / DEV_BSIZE);
	return 0;

fail:
	(void)nextufs__free_file_storage(img, ino_out);
	return rc;
}

int
nextufs__free_file_storage(const struct nextufs_image *img,
    struct nextufs_inode *ino)
{
	uint64_t remaining;
	uint32_t logical_block;

	remaining = ino->size;
	for (logical_block = 0; remaining > 0; logical_block++) {
		uint32_t frags;
		uint32_t data_frag;
		size_t chunk_bytes;
		int rc;

		chunk_bytes = remaining > img->sb.block_size ?
		    img->sb.block_size : (size_t)remaining;
		frags = (uint32_t)((chunk_bytes + img->sb.frag_size - 1U) /
		    img->sb.frag_size);
		rc = nextufs__resolve_inode_data_block(img, ino, logical_block, &data_frag);
		if (rc < 0)
			return rc;
		if (data_frag != 0) {
			rc = nextufs__free_fragment_run(img, data_frag, frags);
			if (rc < 0)
				return rc;
		}
		remaining -= chunk_bytes;
	}
	for (logical_block = 0; logical_block < 12; logical_block++)
		ino->db[logical_block] = 0;
	for (logical_block = 0; logical_block < 3; logical_block++) {
		int rc = nextufs__free_indirect_metadata_tree(img, ino->ib[logical_block],
		    logical_block + 1U);
		if (rc < 0)
			return rc;
		ino->ib[logical_block] = 0;
	}
	ino->size = 0;
	ino->blocks = 0;
	return 0;
}
