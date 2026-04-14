#include "nextufs_write_internal.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int nextufs_w_block_is_free(const uint8_t *free_map, uint32_t frag_base,
	uint32_t frags);
static void nextufs_w_clear_block_bits(uint8_t *free_map, uint32_t frag_base,
	uint32_t frags);
static int nextufs_w_set_run_bits(const uint8_t *free_map, uint32_t frag_base,
	uint32_t frags);
static void nextufs_w_count_free_runs_in_block(const uint8_t *free_map,
	uint32_t block_frag_base, uint32_t frags_per_block, uint32_t *counts);
static void nextufs_w_apply_cg_frsum_delta(uint8_t *buf, const uint32_t *before,
	const uint32_t *after, uint32_t frags_per_block);
static int nextufs_w_allocate_frags_from_free_block_in_cg(
	const struct nextufs_image *img, unsigned cg, uint32_t frags_needed,
	uint32_t *frag_out);

int
nextufs_w_allocate_inode_in_cg(const struct nextufs_image *img, unsigned cg,
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
	cg_off = nextufs_w_cg_block_offset(img, cg);
	rc = nextufs_w_read_exact(img->fd, buf, img->sb.cg_size, cg_off);
	if (rc < 0) {
		free(buf);
		return rc;
	}
	cg_magic = nextufs_w_read_be32(buf + CG_MAGIC_OFF);
	if (cg_magic != CG_MAGIC) {
		free(buf);
		return -EINVAL;
	}
	if (nextufs_w_read_be32(buf + CG_CS_NIFREE_OFF) == 0) {
		free(buf);
		return -ENOSPC;
	}
	start = nextufs_w_read_be32(buf + CG_IROTOR_OFF);
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
	nextufs_w_write_be32(buf + CG_IROTOR_OFF, ipref);
	nextufs_w_write_be32(buf + CG_CS_NIFREE_OFF,
	    nextufs_w_read_be32(buf + CG_CS_NIFREE_OFF) - 1U);
	now = (uint32_t)time(NULL);
	nextufs_w_write_be32(buf + CG_TIME_OFF, now);
	if ((mode & NEXTUFS_IFMT) == NEXTUFS_IFDIR)
		nextufs_w_write_be32(buf + CG_CS_NDIR_OFF,
		    nextufs_w_read_be32(buf + CG_CS_NDIR_OFF) + 1U);
	rc = nextufs_w_write_exact(img->fd, buf, img->sb.cg_size, cg_off);
	free(buf);
	if (rc < 0)
		return rc;
	rc = nextufs_w_update_summary_counts(img, cg,
	    ((mode & NEXTUFS_IFMT) == NEXTUFS_IFDIR) ? 1 : 0, 0, -1, 0);
	if (rc < 0)
		return rc;
	*inode_no_out = (cg * img->sb.inodes_per_group) + ipref;
	return 0;
}

int
nextufs_w_free_inode_in_cg(const struct nextufs_image *img, unsigned inode_no,
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
	cg_off = nextufs_w_cg_block_offset(img, cg);
	rc = nextufs_w_read_exact(img->fd, buf, img->sb.cg_size, cg_off);
	if (rc < 0) {
		free(buf);
		return rc;
	}
	if (nextufs_w_read_be32(buf + CG_MAGIC_OFF) != CG_MAGIC) {
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
	nextufs_w_write_be32(buf + CG_CS_NIFREE_OFF,
	    nextufs_w_read_be32(buf + CG_CS_NIFREE_OFF) + 1U);
	now = (uint32_t)time(NULL);
	nextufs_w_write_be32(buf + CG_TIME_OFF, now);
	if ((mode & NEXTUFS_IFMT) == NEXTUFS_IFDIR)
		nextufs_w_write_be32(buf + CG_CS_NDIR_OFF,
		    nextufs_w_read_be32(buf + CG_CS_NDIR_OFF) - 1U);
	rc = nextufs_w_write_exact(img->fd, buf, img->sb.cg_size, cg_off);
	free(buf);
	if (rc < 0)
		return rc;
	return nextufs_w_update_summary_counts(img, cg,
	    ((mode & NEXTUFS_IFMT) == NEXTUFS_IFDIR) ? -1 : 0, 0, 1, 0);
}

static int
nextufs_w_block_is_free(const uint8_t *free_map, uint32_t frag_base, uint32_t frags)
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
nextufs_w_clear_block_bits(uint8_t *free_map, uint32_t frag_base, uint32_t frags)
{
	uint32_t i;

	for (i = 0; i < frags; i++)
		free_map[(frag_base + i) / 8] &= (uint8_t)~(1U << ((frag_base + i) % 8));
}

static int
nextufs_w_set_run_bits(const uint8_t *free_map, uint32_t frag_base, uint32_t frags)
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
nextufs_w_count_free_runs_in_block(const uint8_t *free_map, uint32_t block_frag_base,
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
nextufs_w_apply_cg_frsum_delta(uint8_t *buf, const uint32_t *before,
    const uint32_t *after, uint32_t frags_per_block)
{
	uint32_t i;

	for (i = 1; i < frags_per_block; i++) {
		uint32_t frsum_off = 52U + (i * 4U);
		uint32_t v = nextufs_w_read_be32(buf + frsum_off);
		int32_t delta = (int32_t)after[i] - (int32_t)before[i];

		if (delta != 0)
			nextufs_w_write_be32(buf + frsum_off, (uint32_t)(v + delta));
	}
}

static int
nextufs_w_allocate_frags_from_free_block_in_cg(const struct nextufs_image *img,
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
	cg_off = nextufs_w_cg_block_offset(img, cg);
	rc = nextufs_w_read_exact(img->fd, buf, img->sb.cg_size, cg_off);
	if (rc < 0) {
		free(buf);
		return rc;
	}
	cg_magic = nextufs_w_read_be32(buf + CG_MAGIC_OFF);
	if (cg_magic != CG_MAGIC) {
		free(buf);
		return -EINVAL;
	}
	if (nextufs_w_read_be32(buf + CG_CS_NBFREE_OFF) == 0) {
		free(buf);
		return -ENOSPC;
	}
	for (local_frag = img->sb.data_off;
	    local_frag + img->sb.frags_per_block <= img->sb.frags_per_group;
	    local_frag += img->sb.frags_per_block) {
		if (!nextufs_w_block_is_free(buf + CG_FREE_OFF, local_frag,
		    img->sb.frags_per_block))
			continue;
		nextufs_w_clear_block_bits(buf + CG_FREE_OFF, local_frag, frags_needed);
		nextufs_w_write_be32(buf + CG_ROTOR_OFF, local_frag);
		nextufs_w_write_be32(buf + CG_CS_NBFREE_OFF,
		    nextufs_w_read_be32(buf + CG_CS_NBFREE_OFF) - 1U);
		free_frags = img->sb.frags_per_block - frags_needed;
		nextufs_w_write_be32(buf + CG_CS_NFFREE_OFF,
		    nextufs_w_read_be32(buf + CG_CS_NFFREE_OFF) + free_frags);
		if (free_frags > 0) {
			uint32_t frsum_off = 52U + (free_frags * 4U);

			nextufs_w_write_be32(buf + frsum_off,
			    nextufs_w_read_be32(buf + frsum_off) + 1U);
		}
		now = (uint32_t)time(NULL);
		nextufs_w_write_be32(buf + CG_TIME_OFF, now);
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
				btot = nextufs_w_read_be32(btotp);
				nextufs_w_write_be32(btotp, btot - 1U);
				bposp = buf + CG_BPOS_OFF +
				    ((cylno * NRPOS + rotpos) * 2U);
				bpos = nextufs_w_read_be16(bposp);
				nextufs_w_write_be16(bposp, (uint16_t)(bpos - 1U));
			}
		}
		rc = nextufs_w_write_exact(img->fd, buf, img->sb.cg_size, cg_off);
		free(buf);
		if (rc < 0)
			return rc;
		rc = nextufs_w_update_summary_counts(img, cg, 0, -1, 0,
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
nextufs_w_allocate_frags_anycg(const struct nextufs_image *img, unsigned preferred_cg,
    uint32_t frags_needed, uint32_t *frag_out)
{
	uint32_t i;

	for (i = 0; i < img->sb.cg_count; i++) {
		unsigned cg = (preferred_cg + i) % img->sb.cg_count;
		int rc = nextufs_w_allocate_frags_from_free_block_in_cg(img, cg,
		    frags_needed, frag_out);
		if (rc == 0)
			return 0;
		if (rc != -ENOSPC)
			return rc;
	}
	return -ENOSPC;
}

int
nextufs_w_extend_fragment_run(const struct nextufs_image *img, uint32_t frag_base,
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
	cg_off = nextufs_w_cg_block_offset(img, cg);
	rc = nextufs_w_read_exact(img->fd, buf, img->sb.cg_size, cg_off);
	if (rc < 0) {
		free(buf);
		return rc;
	}
	if (nextufs_w_read_be32(buf + CG_MAGIC_OFF) != CG_MAGIC) {
		free(buf);
		return -EINVAL;
	}
	added_frags = new_frags - old_frags;
	block_frag_base = local_frag - (local_frag % img->sb.frags_per_block);
	if (!nextufs_w_set_run_bits(buf + CG_FREE_OFF, local_frag + old_frags,
	    added_frags)) {
		free(buf);
		return -ENOSPC;
	}
	nextufs_w_count_free_runs_in_block(buf + CG_FREE_OFF, block_frag_base,
	    img->sb.frags_per_block, before);
	nextufs_w_clear_block_bits(buf + CG_FREE_OFF, local_frag + old_frags,
	    added_frags);
	nextufs_w_count_free_runs_in_block(buf + CG_FREE_OFF, block_frag_base,
	    img->sb.frags_per_block, after);
	nextufs_w_apply_cg_frsum_delta(buf, before, after, img->sb.frags_per_block);
	nextufs_w_write_be32(buf + CG_CS_NFFREE_OFF,
	    nextufs_w_read_be32(buf + CG_CS_NFFREE_OFF) - added_frags);
	now = (uint32_t)time(NULL);
	nextufs_w_write_be32(buf + CG_TIME_OFF, now);
	rc = nextufs_w_write_exact(img->fd, buf, img->sb.cg_size, cg_off);
	free(buf);
	if (rc < 0)
		return rc;
	return nextufs_w_update_summary_counts(img, cg, 0, 0, 0,
	    -(int32_t)added_frags);
}

int
nextufs_w_free_fragment_run(const struct nextufs_image *img, uint32_t frag_base,
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
	cg_off = nextufs_w_cg_block_offset(img, cg);
	rc = nextufs_w_read_exact(img->fd, buf, img->sb.cg_size, cg_off);
	if (rc < 0) {
		free(buf);
		return rc;
	}
	if (nextufs_w_read_be32(buf + CG_MAGIC_OFF) != CG_MAGIC) {
		free(buf);
		return -EINVAL;
	}
	nextufs_w_count_free_runs_in_block(buf + CG_FREE_OFF, block_frag_base,
	    img->sb.frags_per_block, before);
	for (i = 0; i < frags; i++) {
		uint32_t bit = local_frag + i;

		if ((buf[CG_FREE_OFF + (bit / 8)] & (1U << (bit % 8))) != 0) {
			free(buf);
			return -EINVAL;
		}
		buf[CG_FREE_OFF + (bit / 8)] |= (uint8_t)(1U << (bit % 8));
	}
	nextufs_w_count_free_runs_in_block(buf + CG_FREE_OFF, block_frag_base,
	    img->sb.frags_per_block, after);
	nextufs_w_apply_cg_frsum_delta(buf, before, after, img->sb.frags_per_block);
	d_nffree = (int32_t)frags;
	if (nextufs_w_block_is_free(buf + CG_FREE_OFF, block_frag_base,
	    img->sb.frags_per_block)) {
		uint32_t cylno;
		uint32_t rotpos;
		uint8_t *btotp;
		uint8_t *bposp;

		nextufs_w_write_be32(buf + CG_CS_NFFREE_OFF,
		    nextufs_w_read_be32(buf + CG_CS_NFFREE_OFF) + frags -
		    img->sb.frags_per_block);
		nextufs_w_write_be32(buf + CG_CS_NBFREE_OFF,
		    nextufs_w_read_be32(buf + CG_CS_NBFREE_OFF) + 1U);
		d_nffree -= (int32_t)img->sb.frags_per_block;
		d_nbfree = 1;
		cylno = (block_frag_base * img->sb.sectors_per_frag) /
		    img->sb.sectors_per_cyl;
		rotpos = ((block_frag_base * img->sb.sectors_per_frag) %
		    img->sb.sectors_per_cyl % img->sb.sectors_per_track) * NRPOS /
		    img->sb.sectors_per_track;
		if (cylno < 32 && rotpos < NRPOS) {
			btotp = buf + CG_BTOT_OFF + (cylno * 4U);
			nextufs_w_write_be32(btotp, nextufs_w_read_be32(btotp) + 1U);
			bposp = buf + CG_BPOS_OFF + ((cylno * NRPOS + rotpos) * 2U);
			nextufs_w_write_be16(bposp,
			    (uint16_t)(nextufs_w_read_be16(bposp) + 1U));
		}
	} else {
		nextufs_w_write_be32(buf + CG_CS_NFFREE_OFF,
		    nextufs_w_read_be32(buf + CG_CS_NFFREE_OFF) + frags);
	}
	now = (uint32_t)time(NULL);
	nextufs_w_write_be32(buf + CG_TIME_OFF, now);
	rc = nextufs_w_write_exact(img->fd, buf, img->sb.cg_size, cg_off);
	free(buf);
	if (rc < 0)
		return rc;
	return nextufs_w_update_summary_counts(img, cg, 0, d_nbfree, 0, d_nffree);
}

int
nextufs_w_reallocate_fragment_run(const struct nextufs_image *img, uint32_t old_frag,
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
	rc = nextufs_w_read_exact(img->fd, tmp, old_bytes,
	    img->slice_base + ((off_t)old_frag * img->sb.frag_size));
	if (rc < 0) {
		free(tmp);
		return rc;
	}
	rc = nextufs_w_allocate_frags_anycg(img, preferred_cg, new_frags, &new_frag);
	if (rc < 0) {
		free(tmp);
		return rc;
	}
	rc = nextufs_w_write_exact(img->fd, tmp, new_bytes,
	    img->slice_base + ((off_t)new_frag * img->sb.frag_size));
	free(tmp);
	if (rc < 0)
		return rc;
	rc = nextufs_w_free_fragment_run(img, old_frag, old_frags);
	if (rc < 0)
		return rc;
	*new_frag_out = new_frag;
	return 0;
}

int
nextufs_w_allocate_data_for_inode(const struct nextufs_image *img,
    unsigned preferred_cg, const uint8_t *data, size_t data_len,
    struct nextufs_inode *ino_out)
{
	uint64_t remaining;
	uint32_t logical_block;
	uint32_t total_alloc_frags;

	total_alloc_frags = 0;
	remaining = data_len;
	for (logical_block = 0; remaining > 0; logical_block++) {
		uint32_t frags_needed;
		uint32_t alloc_frag;
		size_t chunk_bytes;
		size_t alloc_bytes;
		uint8_t *block;
		int rc;

		if (logical_block >= 12)
			return -EFBIG;
		chunk_bytes = remaining > img->sb.block_size ?
		    img->sb.block_size : (size_t)remaining;
		frags_needed = (uint32_t)((chunk_bytes + img->sb.frag_size - 1U) /
		    img->sb.frag_size);
		rc = nextufs_w_allocate_frags_anycg(img, preferred_cg, frags_needed,
		    &alloc_frag);
		if (rc < 0)
			return rc;
		ino_out->db[logical_block] = alloc_frag;
		total_alloc_frags += frags_needed;
		alloc_bytes = (size_t)frags_needed * img->sb.frag_size;
		block = calloc(1, alloc_bytes);
		if (block == NULL)
			return -ENOMEM;
		memcpy(block, data + ((size_t)logical_block * img->sb.block_size),
		    chunk_bytes);
		rc = nextufs_w_write_exact(img->fd, block, alloc_bytes,
		    img->slice_base + ((off_t)alloc_frag * img->sb.frag_size));
		free(block);
		if (rc < 0)
			return rc;
		remaining -= chunk_bytes;
	}
	ino_out->size = data_len;
	ino_out->blocks = total_alloc_frags * (img->sb.frag_size / DEV_BSIZE);
	return 0;
}

int
nextufs_w_free_regular_file_storage(const struct nextufs_image *img,
    struct nextufs_inode *ino)
{
	uint64_t remaining;
	uint32_t logical_block;

	if (ino->ib[0] != 0 || ino->ib[1] != 0 || ino->ib[2] != 0)
		return -EOPNOTSUPP;
	remaining = ino->size;
	for (logical_block = 0; logical_block < 12 && remaining > 0; logical_block++) {
		uint32_t frags;
		size_t chunk_bytes;
		int rc;

		chunk_bytes = remaining > img->sb.block_size ?
		    img->sb.block_size : (size_t)remaining;
		frags = (uint32_t)((chunk_bytes + img->sb.frag_size - 1U) /
		    img->sb.frag_size);
		if (ino->db[logical_block] != 0) {
			rc = nextufs_w_free_fragment_run(img, ino->db[logical_block], frags);
			if (rc < 0)
				return rc;
			ino->db[logical_block] = 0;
		}
		remaining -= chunk_bytes;
	}
	if (remaining != 0)
		return -EFBIG;
	ino->size = 0;
	ino->blocks = 0;
	return 0;
}
