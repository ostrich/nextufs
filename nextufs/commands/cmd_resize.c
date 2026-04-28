#include "nextufs.h"
#include "nextufs_internal.h"
#include "nextufs_label.h"
#include "nextufs_size.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define UFS_DISK_SECTOR_SIZE 512U
#define CSUM_SIZE 16U
#define SB_SIZE_OFF 0x24U
#define SB_DSIZE_OFF 0x28U
#define SB_NCG_OFF 0x2cU
#define SB_CSADDR_OFF 0x98U
#define SB_CSSIZE_OFF 0x9cU
#define SB_TIME_OFF_LOCAL 0x20U
#define SB_NCYL_OFF 0x0b0U
#define SB_TOTAL_NDIR_OFF 0x0c0U
#define SB_TOTAL_NBFREE_OFF 0x0c4U
#define SB_TOTAL_NIFREE_OFF 0x0c8U
#define SB_TOTAL_NFFREE_OFF 0x0ccU

#define CG_NCYL_OFF 16U
#define CG_NIBLK_OFF 18U
#define CG_NDBLK_OFF 20U
#define CG_FROTOR_OFF 44U

struct image_open_result {
	struct nextufs_image img;
	int labeled;
	uint64_t backing_size;
};

static int write_csum_entry(struct nextufs_image *img, uint32_t cg,
    int32_t ndir, int32_t nbfree, int32_t nifree, int32_t nffree);

static void
usage(FILE *out, const char *argv0)
{
	fprintf(out, "usage: %s grow [--force-size] <source> <size-1k-sectors>\n", argv0);
	fprintf(out, "\n");
	fprintf(out, "For labeled disk images, grow size is the final image size.\n");
	fprintf(out, "For raw filesystem images, grow size is the filesystem size.\n");
}

static int
file_size_bytes(const char *path, uint64_t *size_out)
{
	struct stat st;

	if (stat(path, &st) < 0)
		return -errno;
	if (!S_ISREG(st.st_mode))
		return -EINVAL;
	if (st.st_size < 0)
		return -EINVAL;
	*size_out = (uint64_t)st.st_size;
	return 0;
}

static int
open_supported_image(struct image_open_result *out, const char *path, int writable)
{
	int rc;

	memset(out, 0, sizeof(*out));
	rc = file_size_bytes(path, &out->backing_size);
	if (rc < 0)
		return rc;
	memset(&out->img, 0, sizeof(out->img));
	rc = nextufs_image_open_source(&out->img, path,
	    writable ? NEXTUFS_SOURCE_READ_WRITE : NEXTUFS_SOURCE_READ_ONLY);
	if (rc < 0)
		return rc;
	if (out->img.source_is_container) {
		nextufs_image_close(&out->img);
		return -ENOTSUP;
	}
	out->labeled = out->img.used_disk_label != 0;
	return 0;
}

static int
validate_labeled_grow_layout(const struct nextufs_image *img)
{
	if (!img->used_disk_label)
		return 0;
	return nextufs_label_validate_single_slice_fd(img->fd, img->slice_base);
}

static uint64_t
cgstart(const struct nextufs_image *img, uint32_t cg)
{
	return nextufs__cgstart(img, cg);
}

static uint64_t
cgsblock(const struct nextufs_image *img, uint32_t cg)
{
	return cgstart(img, cg) + img->sb.sb_off;
}

static off_t
superblock_offset(const struct nextufs_image *img, uint32_t cg)
{
	return (off_t)(cgstart(img, cg) * img->sb.frag_size) +
	    (off_t)((uint64_t)img->sb.sb_off * UFS_DISK_SECTOR_SIZE);
}

static uint64_t
cgtod(const struct nextufs_image *img, uint32_t cg)
{
	return cgstart(img, cg) + img->sb.cg_off;
}

static uint64_t
cgimin(const struct nextufs_image *img, uint32_t cg)
{
	return cgstart(img, cg) + img->sb.ino_off;
}

static uint64_t
cgdmin(const struct nextufs_image *img, uint32_t cg)
{
	return cgstart(img, cg) + img->sb.data_off;
}

static uint32_t
cbtocylno(const struct nextufs_image *img, uint32_t cg_local_frag)
{
	return (cg_local_frag * img->sb.sectors_per_frag) / img->sb.sectors_per_cyl;
}

static uint32_t
cbtorpos(const struct nextufs_image *img, uint32_t cg_local_frag)
{
	return ((cg_local_frag * img->sb.sectors_per_frag) %
	    img->sb.sectors_per_cyl % img->sb.sectors_per_track) *
	    NRPOS / img->sb.sectors_per_track;
}

static void
set_frag_bit(uint8_t *free_map, uint32_t frag)
{
	free_map[frag / 8U] |= (uint8_t)(1U << (frag % 8U));
}

static int
frag_bit_is_set(const uint8_t *free_map, uint32_t frag)
{
	return (free_map[frag / 8U] & (1U << (frag % 8U))) != 0;
}

static void
clear_frag_bit(uint8_t *free_map, uint32_t frag)
{
	free_map[frag / 8U] &= (uint8_t)~(1U << (frag % 8U));
}

static void
count_free_runs_in_block(const uint8_t *free_map, uint32_t block_frag_base,
    uint32_t frags_per_block, uint32_t *counts)
{
	uint32_t i;

	memset(counts, 0, sizeof(uint32_t) * (frags_per_block + 1U));
	i = 0;
	while (i < frags_per_block) {
		uint32_t run = 0;

		while (i + run < frags_per_block &&
		    frag_bit_is_set(free_map, block_frag_base + i + run))
			run++;
		if (run > 0 && run < frags_per_block)
			counts[run]++;
		i += run > 0 ? run : 1;
	}
}

static void
apply_frsum_delta(uint8_t *cg_buf, const uint32_t *before,
    const uint32_t *after, uint32_t frags_per_block)
{
	uint32_t i;

	for (i = 1; i < frags_per_block; i++) {
		uint32_t frsum_off = 52U + (i * 4U);
		uint32_t v = nextufs__read_be32(cg_buf + frsum_off);
		int32_t delta = (int32_t)after[i] - (int32_t)before[i];

		if (delta != 0)
			nextufs__write_be32(cg_buf + frsum_off, (uint32_t)(v + delta));
	}
}

static uint32_t
count_free_bits(const uint8_t *free_map, uint32_t start, uint32_t count)
{
	uint32_t i;
	uint32_t total = 0;

	for (i = 0; i < count; i++) {
		if (frag_bit_is_set(free_map, start + i))
			total++;
	}
	return total;
}

static void
set_block_free(struct nextufs_image *img, uint8_t *cg_buf, uint32_t local_frag,
    int32_t *nbfree)
{
	uint32_t bit;
	uint32_t cylno;
	uint32_t rotpos;
	uint8_t *btotp;
	uint8_t *bposp;

	for (bit = 0; bit < img->sb.frags_per_block; bit++)
		set_frag_bit(cg_buf + CG_FREE_OFF, local_frag + bit);
	(*nbfree)++;
	cylno = cbtocylno(img, local_frag);
	rotpos = cbtorpos(img, local_frag);
	if (cylno < 32U && rotpos < NRPOS) {
		btotp = cg_buf + CG_BTOT_OFF + (cylno * 4U);
		bposp = cg_buf + CG_BPOS_OFF + ((cylno * NRPOS + rotpos) * 2U);
		nextufs__write_be32(btotp, nextufs__read_be32(btotp) + 1U);
		nextufs__write_be16(bposp,
		    (uint16_t)(nextufs__read_be16(bposp) + 1U));
	}
}

static int
ranges_overlap(uint32_t a_start, uint32_t a_len, uint32_t b_start, uint32_t b_len)
{
	uint32_t a_end = a_start + a_len;
	uint32_t b_end = b_start + b_len;

	return a_start < b_end && b_start < a_end;
}

static int
write_zeroes(const struct nextufs_image *img, off_t offset, uint64_t size)
{
	uint8_t zero[8192];
	uint64_t done;

	memset(zero, 0, sizeof(zero));
	for (done = 0; done < size; ) {
		size_t chunk = size - done > sizeof(zero) ? sizeof(zero) :
		    (size_t)(size - done);
		int rc = nextufs_image_pwrite(img, zero, chunk, offset + (off_t)done);

		if (rc < 0)
			return rc;
		done += chunk;
	}
	return 0;
}

static int
reserve_summary_extension(struct nextufs_image *img, uint32_t new_cssize,
    int32_t *d_nbfree_out, int32_t *d_nffree_out)
{
	uint8_t *cg_buf;
	uint8_t *free_map;
	uint32_t old_frags;
	uint32_t new_frags;
	uint32_t pos;
	uint32_t end;
	int32_t d_nbfree = 0;
	int32_t d_nffree = 0;
	int rc;

	*d_nbfree_out = 0;
	*d_nffree_out = 0;
	if (new_cssize <= img->sb.csum_size)
		return 0;
	old_frags = (img->sb.csum_size + img->sb.frag_size - 1U) /
	    img->sb.frag_size;
	new_frags = (new_cssize + img->sb.frag_size - 1U) / img->sb.frag_size;
	pos = img->sb.cyl_summary_addr + old_frags;
	end = img->sb.cyl_summary_addr + new_frags;
	if (end > img->sb.frags_per_group)
		return -ENOTSUP;

	cg_buf = malloc(img->sb.cg_size);
	if (cg_buf == NULL)
		return -ENOMEM;
	rc = nextufs_image_pread(img, cg_buf, img->sb.cg_size,
	    (off_t)(cgtod(img, 0) * img->sb.frag_size));
	if (rc < 0) {
		free(cg_buf);
		return rc;
	}
	free_map = cg_buf + CG_FREE_OFF;
	while (pos < end) {
		uint32_t block_base = pos - (pos % img->sb.frags_per_block);
		uint32_t block_end = block_base + img->sb.frags_per_block;
		uint32_t clear_end = end < block_end ? end : block_end;
		uint32_t before_runs[9];
		uint32_t after_runs[9];
		uint32_t before_free;
		uint32_t after_free;
		int before_whole;
		int after_whole;
		uint32_t i;

		if (img->sb.frags_per_block > 8U) {
			free(cg_buf);
			return -ENOTSUP;
		}
		for (i = pos; i < clear_end; i++) {
			if (!frag_bit_is_set(free_map, i)) {
				free(cg_buf);
				return -ENOSPC;
			}
		}
		before_free = count_free_bits(free_map, block_base,
		    img->sb.frags_per_block);
		before_whole = before_free == img->sb.frags_per_block;
		count_free_runs_in_block(free_map, block_base,
		    img->sb.frags_per_block, before_runs);
		for (i = pos; i < clear_end; i++)
			clear_frag_bit(free_map, i);
		after_free = count_free_bits(free_map, block_base,
		    img->sb.frags_per_block);
		after_whole = after_free == img->sb.frags_per_block;
		count_free_runs_in_block(free_map, block_base,
		    img->sb.frags_per_block, after_runs);
		apply_frsum_delta(cg_buf, before_runs, after_runs,
		    img->sb.frags_per_block);
		if (before_whole && !after_whole) {
			uint32_t cylno = cbtocylno(img, block_base);
			uint32_t rotpos = cbtorpos(img, block_base);

			d_nbfree--;
			d_nffree += (int32_t)after_free;
			if (cylno < 32U && rotpos < NRPOS) {
				uint8_t *btotp = cg_buf + CG_BTOT_OFF + (cylno * 4U);
				uint8_t *bposp = cg_buf + CG_BPOS_OFF +
				    ((cylno * NRPOS + rotpos) * 2U);

				nextufs__write_be32(btotp,
				    nextufs__read_be32(btotp) - 1U);
				nextufs__write_be16(bposp,
				    (uint16_t)(nextufs__read_be16(bposp) - 1U));
			}
		} else {
			d_nffree += (int32_t)after_free - (int32_t)before_free;
		}
		pos = clear_end;
	}
	nextufs__write_be32(cg_buf + CG_CS_NBFREE_OFF,
	    nextufs__read_be32(cg_buf + CG_CS_NBFREE_OFF) + (uint32_t)d_nbfree);
	nextufs__write_be32(cg_buf + CG_CS_NFFREE_OFF,
	    nextufs__read_be32(cg_buf + CG_CS_NFFREE_OFF) + (uint32_t)d_nffree);
	nextufs__write_be32(cg_buf + CG_TIME_OFF, (uint32_t)time(NULL));
	rc = nextufs_image_pwrite(img, cg_buf, img->sb.cg_size,
	    (off_t)(cgtod(img, 0) * img->sb.frag_size));
	if (rc == 0)
		rc = write_csum_entry(img, 0, (int32_t)nextufs__read_be32(cg_buf +
		    CG_CS_NDIR_OFF), (int32_t)nextufs__read_be32(cg_buf +
		    CG_CS_NBFREE_OFF), (int32_t)nextufs__read_be32(cg_buf +
		    CG_CS_NIFREE_OFF), (int32_t)nextufs__read_be32(cg_buf +
		    CG_CS_NFFREE_OFF));
	free(cg_buf);
	if (rc < 0)
		return rc;
	*d_nbfree_out = d_nbfree;
	*d_nffree_out = d_nffree;
	return 0;
}

static uint32_t
inode_direct_frags(const struct nextufs_image *img, const struct nextufs_inode *ino,
    uint32_t slot)
{
	uint64_t off;
	uint64_t remaining;
	uint64_t bytes;

	off = (uint64_t)slot * img->sb.block_size;
	if (off >= ino->size)
		return 0;
	remaining = ino->size - off;
	bytes = remaining < img->sb.block_size ? remaining : img->sb.block_size;
	return (uint32_t)((bytes + img->sb.frag_size - 1U) / img->sb.frag_size);
}

static int
frag_ranges_overlap(uint32_t a_start, uint32_t a_frags, uint32_t b_start,
    uint32_t b_frags)
{
	return a_start < b_start + b_frags && b_start < a_start + a_frags;
}

static int
move_direct_fragment_run(struct nextufs_image *img, unsigned inode_no,
    struct nextufs_inode *ino, uint32_t slot, uint32_t frags,
    uint32_t protect_start, uint32_t protect_frags, int *moved_out)
{
	uint8_t *buf;
	uint32_t old_frag = ino->db[slot];
	uint32_t new_frag;
	uint32_t preferred_cg;
	size_t bytes;
	int rc;

	*moved_out = 0;
	if (frags == 0 || old_frag == 0 ||
	    !frag_ranges_overlap(old_frag, frags, protect_start, protect_frags))
		return 0;
	bytes = (size_t)frags * img->sb.frag_size;
	buf = malloc(bytes);
	if (buf == NULL)
		return -ENOMEM;
	rc = nextufs_image_pread(img, buf, bytes,
	    (off_t)((uint64_t)old_frag * img->sb.frag_size));
	if (rc < 0) {
		free(buf);
		return rc;
	}
	preferred_cg = old_frag / img->sb.frags_per_group;
	if (img->sb.cg_count > 1U)
		preferred_cg = (preferred_cg + 1U) % img->sb.cg_count;
	rc = nextufs__allocate_frags_anycg(img, preferred_cg, frags, &new_frag);
	if (rc < 0) {
		free(buf);
		return rc;
	}
	if (frag_ranges_overlap(new_frag, frags, protect_start, protect_frags)) {
		(void)nextufs__free_fragment_run(img, new_frag, frags);
		free(buf);
		return -ENOSPC;
	}
	rc = nextufs_image_pwrite(img, buf, bytes,
	    (off_t)((uint64_t)new_frag * img->sb.frag_size));
	free(buf);
	if (rc < 0) {
		(void)nextufs__free_fragment_run(img, new_frag, frags);
		return rc;
	}
	ino->db[slot] = new_frag;
	ino->ctime = (uint32_t)time(NULL);
	rc = nextufs__write_inode_raw(img, inode_no, ino);
	if (rc < 0)
		return rc;
	rc = nextufs__free_fragment_run(img, old_frag, frags);
	if (rc < 0)
		return rc;
	printf("nextufs.resize: moved inode %u direct[%u] %" PRIu32
	    "+%" PRIu32 " -> %" PRIu32 "\n",
	    inode_no, slot, old_frag, frags, new_frag);
	*moved_out = 1;
	return 0;
}

static int
evacuate_summary_extension_range(struct nextufs_image *img, uint32_t new_cssize,
    uint32_t *moves_out)
{
	uint32_t old_summary_frags;
	uint32_t new_summary_frags;
	uint32_t protect_start;
	uint32_t protect_frags;
	unsigned inode_no;
	unsigned inode_count;
	uint32_t moves = 0;

	*moves_out = 0;
	if (new_cssize <= img->sb.csum_size)
		return 0;
	old_summary_frags = (img->sb.csum_size + img->sb.frag_size - 1U) /
	    img->sb.frag_size;
	new_summary_frags = (new_cssize + img->sb.frag_size - 1U) /
	    img->sb.frag_size;
	protect_start = img->sb.cyl_summary_addr + old_summary_frags;
	protect_frags = new_summary_frags - old_summary_frags;
	if (protect_start + protect_frags > img->sb.frags_per_group)
		return -ENOTSUP;
	inode_count = img->sb.cg_count * img->sb.inodes_per_group;
	for (inode_no = 0; inode_no < inode_count; inode_no++) {
		struct nextufs_inode ino;
		off_t ino_off;
		uint32_t slot;
		int changed = 0;
		int rc;

		rc = nextufs_inode_read(img, inode_no, &ino, &ino_off);
		if (rc < 0)
			return rc;
		(void)ino_off;
		if (ino.mode == 0 || ino.nlink == 0)
			continue;
		for (slot = 0; slot < 3U; slot++) {
			if (ino.ib[slot] != 0 && frag_ranges_overlap(ino.ib[slot],
			    img->sb.frags_per_block, protect_start, protect_frags))
				return -ENOTSUP;
		}
		for (slot = 0; slot < 12U; slot++) {
			uint32_t frags = inode_direct_frags(img, &ino, slot);
			int moved;

			rc = move_direct_fragment_run(img, inode_no, &ino, slot, frags,
			    protect_start, protect_frags, &moved);
			if (rc < 0)
				return rc;
			if (moved) {
				moves++;
				changed = 1;
			}
		}
		(void)changed;
	}
	*moves_out = moves;
	return 0;
}

static int
patch_label_partition_size(const struct nextufs_image *img, uint64_t slice_bytes)
{
	int patched = 0;
	int rc;

	if (!img->used_disk_label)
		return 0;
	rc = nextufs_label_patch_slice_size_fd(img->fd, img->slice_base,
	    slice_bytes, &patched);
	if (rc < 0)
		return rc;
	printf("nextufs.resize: updated %d disk label copies\n", patched);
	return 0;
}

static int
build_new_cg(struct nextufs_image *img, uint32_t cg, uint16_t ncyl,
    uint32_t reserved_start, uint32_t reserved_frags, uint8_t *cg_buf,
    int32_t *nbfree_out, int32_t *nifree_out, int32_t *nffree_out)
{
	uint64_t cbase;
	uint64_t dmax;
	uint32_t dlower;
	uint32_t dupper;
	uint32_t d;
	int32_t nbfree;
	int32_t nffree;

	memset(cg_buf, 0, img->sb.cg_size);
	cbase = (uint64_t)img->sb.frags_per_group * cg;
	dmax = cbase + img->sb.frags_per_group;
	dlower = (uint32_t)(cgsblock(img, cg) - cbase);
	dupper = (uint32_t)(cgdmin(img, cg) - cbase);
	nbfree = 0;
	nffree = 0;

	nextufs__write_be32(cg_buf + CG_TIME_OFF, (uint32_t)time(NULL));
	nextufs__write_be32(cg_buf + CG_CGX_OFF, cg);
	nextufs__write_be16(cg_buf + CG_NCYL_OFF, ncyl);
	nextufs__write_be16(cg_buf + CG_NIBLK_OFF,
	    (uint16_t)img->sb.inodes_per_group);
	nextufs__write_be32(cg_buf + CG_NDBLK_OFF, (uint32_t)(dmax - cbase));
	nextufs__write_be32(cg_buf + CG_IROTOR_OFF, 0);
	nextufs__write_be32(cg_buf + CG_MAGIC_OFF, CG_MAGIC);

	for (d = 0; d < dlower; d += img->sb.frags_per_block)
		set_block_free(img, cg_buf, d, &nbfree);
	for (d = dupper; d + img->sb.frags_per_block <= img->sb.frags_per_group;
	    d += img->sb.frags_per_block) {
		if (reserved_frags != 0 && ranges_overlap(d,
		    img->sb.frags_per_block, reserved_start, reserved_frags))
			continue;
		set_block_free(img, cg_buf, d, &nbfree);
	}
	if (d < img->sb.frags_per_group) {
		uint32_t run = img->sb.frags_per_group - d;
		uint32_t bit;

		if (reserved_frags != 0 && ranges_overlap(d, run, reserved_start,
		    reserved_frags))
			return -ENOTSUP;
		for (bit = 0; bit < run; bit++)
			set_frag_bit(cg_buf + CG_FREE_OFF, d + bit);
		nffree += (int32_t)run;
		nextufs__write_be32(cg_buf + 52U + (run * 4U), 1U);
	}

	nextufs__write_be32(cg_buf + CG_CS_NBFREE_OFF, (uint32_t)nbfree);
	nextufs__write_be32(cg_buf + CG_CS_NIFREE_OFF, img->sb.inodes_per_group);
	nextufs__write_be32(cg_buf + CG_CS_NFFREE_OFF, (uint32_t)nffree);
	*nbfree_out = nbfree;
	*nifree_out = (int32_t)img->sb.inodes_per_group;
	*nffree_out = nffree;
	return 0;
}

static int
patch_cg_ncyl(struct nextufs_image *img, uint32_t cg, uint16_t ncyl)
{
	uint8_t *cg_buf;
	int rc;

	cg_buf = malloc(img->sb.cg_size);
	if (cg_buf == NULL)
		return -ENOMEM;
	rc = nextufs_image_pread(img, cg_buf, img->sb.cg_size,
	    (off_t)(cgtod(img, cg) * img->sb.frag_size));
	if (rc < 0) {
		free(cg_buf);
		return rc;
	}
	nextufs__write_be16(cg_buf + CG_NCYL_OFF, ncyl);
	rc = nextufs_image_pwrite(img, cg_buf, img->sb.cg_size,
	    (off_t)(cgtod(img, cg) * img->sb.frag_size));
	free(cg_buf);
	return rc;
}

static int
expand_old_last_cg(struct nextufs_image *img, uint32_t cg, uint64_t old_frags,
    int32_t *nbfree_out, uint32_t *dsize_out)
{
	uint8_t *cg_buf;
	uint64_t cbase;
	uint32_t start;
	uint32_t d;
	int32_t nbfree;
	int rc;

	cbase = (uint64_t)img->sb.frags_per_group * cg;
	if (old_frags <= cbase || old_frags >= cbase + img->sb.frags_per_group) {
		*nbfree_out = 0;
		*dsize_out = 0;
		return patch_cg_ncyl(img, cg, (uint16_t)img->sb.cpg);
	}
	start = (uint32_t)(old_frags - cbase);
	if ((start % img->sb.frags_per_block) != 0)
		return -ENOTSUP;
	cg_buf = malloc(img->sb.cg_size);
	if (cg_buf == NULL)
		return -ENOMEM;
	rc = nextufs_image_pread(img, cg_buf, img->sb.cg_size,
	    (off_t)(cgtod(img, cg) * img->sb.frag_size));
	if (rc < 0) {
		free(cg_buf);
		return rc;
	}
	nbfree = 0;
	nextufs__write_be16(cg_buf + CG_NCYL_OFF, (uint16_t)img->sb.cpg);
	nextufs__write_be32(cg_buf + CG_NDBLK_OFF, img->sb.frags_per_group);
	for (d = start; d + img->sb.frags_per_block <= img->sb.frags_per_group;
	    d += img->sb.frags_per_block)
		set_block_free(img, cg_buf, d, &nbfree);
	nextufs__write_be32(cg_buf + CG_CS_NBFREE_OFF,
	    nextufs__read_be32(cg_buf + CG_CS_NBFREE_OFF) + (uint32_t)nbfree);
	nextufs__write_be32(cg_buf + CG_TIME_OFF, (uint32_t)time(NULL));
	rc = nextufs_image_pwrite(img, cg_buf, img->sb.cg_size,
	    (off_t)(cgtod(img, cg) * img->sb.frag_size));
	if (rc == 0)
		rc = write_csum_entry(img, cg, (int32_t)nextufs__read_be32(cg_buf +
		    CG_CS_NDIR_OFF), (int32_t)nextufs__read_be32(cg_buf +
		    CG_CS_NBFREE_OFF), (int32_t)nextufs__read_be32(cg_buf +
		    CG_CS_NIFREE_OFF), (int32_t)nextufs__read_be32(cg_buf +
		    CG_CS_NFFREE_OFF));
	free(cg_buf);
	if (rc < 0)
		return rc;
	*nbfree_out = nbfree;
	*dsize_out = img->sb.frags_per_group - start;
	return 0;
}

static int
write_csum_entry_at(struct nextufs_image *img, uint32_t csum_addr, uint32_t cg,
    int32_t ndir, int32_t nbfree, int32_t nifree, int32_t nffree)
{
	uint8_t buf[CSUM_SIZE];
	off_t off;

	memset(buf, 0, sizeof(buf));
	nextufs__write_be32(buf + 0, (uint32_t)ndir);
	nextufs__write_be32(buf + 4, (uint32_t)nbfree);
	nextufs__write_be32(buf + 8, (uint32_t)nifree);
	nextufs__write_be32(buf + 12, (uint32_t)nffree);
	off = (off_t)((uint64_t)csum_addr * img->sb.frag_size) +
	    (off_t)((uint64_t)cg * CSUM_SIZE);
	return nextufs_image_pwrite(img, buf, sizeof(buf), off);
}

static int
write_csum_entry(struct nextufs_image *img, uint32_t cg, int32_t ndir,
    int32_t nbfree, int32_t nifree, int32_t nffree)
{
	return write_csum_entry_at(img, img->sb.cyl_summary_addr, cg, ndir,
	    nbfree, nifree, nffree);
}

static int
patch_and_write_superblocks(struct nextufs_image *img, const uint8_t *old_sb,
    uint32_t new_ncg, uint32_t new_ncyl, uint32_t new_frag_count,
    uint32_t new_dsize, uint32_t new_csum_addr, uint32_t new_cssize,
    int32_t add_nbfree, int32_t add_nifree, int32_t add_nffree)
{
	uint8_t *sb;
	uint32_t cg;
	int rc;

	sb = malloc(img->sb.super_size);
	if (sb == NULL)
		return -ENOMEM;
	memcpy(sb, old_sb, img->sb.super_size);
	nextufs__write_be32(sb + SB_TIME_OFF_LOCAL, (uint32_t)time(NULL));
	nextufs__write_be32(sb + SB_SIZE_OFF, new_frag_count);
	nextufs__write_be32(sb + SB_DSIZE_OFF, new_dsize);
	nextufs__write_be32(sb + SB_NCG_OFF, new_ncg);
	nextufs__write_be32(sb + SB_CSADDR_OFF, new_csum_addr);
	nextufs__write_be32(sb + SB_CSSIZE_OFF, new_cssize);
	nextufs__write_be32(sb + SB_NCYL_OFF, new_ncyl);
	nextufs__write_be32(sb + SB_TOTAL_NBFREE_OFF,
	    img->sb.free_block_count + (uint32_t)add_nbfree);
	nextufs__write_be32(sb + SB_TOTAL_NIFREE_OFF,
	    img->sb.free_inode_count + (uint32_t)add_nifree);
	nextufs__write_be32(sb + SB_TOTAL_NFFREE_OFF,
	    img->sb.free_frag_count + (uint32_t)add_nffree);
	nextufs__write_be32(sb + SB_TOTAL_NDIR_OFF, img->sb.dir_count);

	rc = nextufs_image_pwrite(img, sb, img->sb.super_size, superblock_offset(img, 0));
	if (rc < 0) {
		free(sb);
		return rc;
	}
	for (cg = 0; cg < new_ncg; cg++) {
		off_t off = (off_t)(cgsblock(img, cg) * img->sb.frag_size);

		rc = nextufs_image_pwrite(img, sb, img->sb.super_size, off);
		if (rc < 0) {
			free(sb);
			return rc;
		}
	}
	free(sb);
	return 0;
}

static int
cmd_grow(const char *path, const char *sectors_arg, int force)
{
	struct image_open_result opened;
	struct nextufs_image *img;
	uint8_t *old_sb;
	uint8_t *cg_buf;
	uint64_t sectors;
	uint64_t target_slice_bytes;
	uint64_t target_backing_bytes;
	uint64_t target_frags;
	uint64_t new_ncg64;
	uint64_t new_cssize64;
	uint32_t new_ncg;
	uint32_t new_ncyl;
	uint32_t new_dsize;
	uint32_t new_cssize;
	uint32_t new_csum_addr;
	uint32_t cg;
	int32_t add_nbfree;
	int32_t add_nifree;
	int32_t add_nffree;
	int rc;

	if (nextufs_parse_size_bytes(sectors_arg, NEXTUFS_BARE_SIZE_1K_SECTORS,
	    &target_backing_bytes) < 0) {
		fprintf(stderr, "nextufs.resize: invalid size '%s'\n", sectors_arg);
		return 1;
	}
	if ((target_backing_bytes % NEXTUFS_KIB_BYTES) != 0) {
		fprintf(stderr, "nextufs.resize: requested size must align to 1K sectors\n");
		return 1;
	}
	sectors = target_backing_bytes / NEXTUFS_KIB_BYTES;
	if (!force && target_backing_bytes > NEXTUFS_COMPAT_MAX_BYTES) {
		fprintf(stderr,
		    "nextufs.resize: requested size exceeds the NEXTSTEP/OPENSTEP compatibility limit; use --force-size to override\n");
		return 1;
	}

	rc = open_supported_image(&opened, path, 1);
	if (rc < 0) {
		fprintf(stderr, "nextufs.resize: cannot open '%s' writable: %d\n",
		    path, rc);
		return 1;
	}
	img = &opened.img;
	if (opened.labeled) {
		if (target_backing_bytes <= (uint64_t)img->slice_base) {
			fprintf(stderr, "nextufs.resize: target image size is smaller than the slice base\n");
			nextufs_image_close(img);
			return 1;
		}
		target_slice_bytes = target_backing_bytes - (uint64_t)img->slice_base;
	} else {
		target_slice_bytes = target_backing_bytes;
	}
	if (target_slice_bytes <= (uint64_t)img->slice_size) {
		fprintf(stderr, "nextufs.resize: target size is not larger than filesystem\n");
		nextufs_image_close(img);
		return 1;
	}
	rc = validate_labeled_grow_layout(img);
	if (rc < 0) {
		fprintf(stderr,
		    "nextufs.resize: unsupported labeled image layout: %d\n", rc);
		nextufs_image_close(img);
		return 1;
	}
	if ((target_slice_bytes % img->sb.frag_size) != 0) {
		fprintf(stderr, "nextufs.resize: target size is not fragment-aligned\n");
		nextufs_image_close(img);
		return 1;
	}
	target_frags = target_slice_bytes / img->sb.frag_size;
	if ((target_frags % img->sb.frags_per_group) != 0) {
		fprintf(stderr,
		    "nextufs.resize: initial grow support requires whole cylinder groups\n");
		nextufs_image_close(img);
		return 1;
	}
	new_ncg64 = target_frags / img->sb.frags_per_group;
	if (new_ncg64 <= img->sb.cg_count || new_ncg64 > UINT32_MAX) {
		fprintf(stderr, "nextufs.resize: invalid target cylinder-group count\n");
		nextufs_image_close(img);
		return 1;
	}
	new_cssize64 = ((new_ncg64 * CSUM_SIZE) + img->sb.frag_size - 1U) /
	    img->sb.frag_size * img->sb.frag_size;
	if (new_cssize64 > UINT32_MAX) {
		fprintf(stderr, "nextufs.resize: cylinder-summary area is too large\n");
		nextufs_image_close(img);
		return 1;
	}

	new_ncg = (uint32_t)new_ncg64;
	new_ncyl = new_ncg * img->sb.cpg;
	new_cssize = (uint32_t)new_cssize64;
	new_csum_addr = img->sb.cyl_summary_addr;
	if (new_cssize > img->sb.csum_size) {
		uint32_t moves;

		rc = evacuate_summary_extension_range(img, new_cssize, &moves);
		if (rc < 0) {
			fprintf(stderr,
			    "nextufs.resize: failed to evacuate summary extension range: %d\n",
			    rc);
			nextufs_image_close(img);
			return 1;
		}
		if (moves != 0) {
			rc = nextufs_image_fsync(img);
			if (rc < 0) {
				fprintf(stderr, "nextufs.resize: fsync failed after evacuation: %d\n",
				    rc);
				nextufs_image_close(img);
				return 1;
			}
			printf("nextufs.resize: evacuated %u data runs; restarting grow\n",
			    moves);
			nextufs_image_close(img);
			return cmd_grow(path, sectors_arg, force);
		}
		printf("nextufs.resize: summary extension range is clear\n");
	}
	if (new_cssize > img->sb.csum_size &&
	    img->sb.cyl_summary_addr + (new_cssize / img->sb.frag_size) >
	    img->sb.frags_per_group) {
		fprintf(stderr, "nextufs.resize: enlarged summary does not fit in cg0\n");
		nextufs_image_close(img);
		return 1;
	}
	new_dsize = img->sb.data_frag_count;
	add_nbfree = 0;
	add_nifree = 0;
	add_nffree = 0;
	old_sb = malloc(img->sb.super_size);
	cg_buf = malloc(img->sb.cg_size);
	if (old_sb == NULL || cg_buf == NULL) {
		fprintf(stderr, "nextufs.resize: out of memory\n");
		free(old_sb);
		free(cg_buf);
		nextufs_image_close(img);
		return 1;
	}
	rc = nextufs_image_pread(img, old_sb, img->sb.super_size,
	    superblock_offset(img, 0));
	if (rc < 0) {
		fprintf(stderr, "nextufs.resize: failed to read superblock: %d\n", rc);
		free(old_sb);
		free(cg_buf);
		nextufs_image_close(img);
		return 1;
	}
	if (ftruncate(img->fd, (off_t)target_backing_bytes) < 0) {
		fprintf(stderr, "nextufs.resize: ftruncate failed: %s\n", strerror(errno));
		free(old_sb);
		free(cg_buf);
		nextufs_image_close(img);
		return 1;
	}
	rc = patch_label_partition_size(img, target_slice_bytes);
	if (rc < 0) {
		fprintf(stderr, "nextufs.resize: failed to update disk label: %d\n", rc);
		goto fail;
	}
	{
		int32_t old_tail_nbfree;
		uint32_t old_tail_dsize;

		rc = expand_old_last_cg(img, img->sb.cg_count - 1U,
		    img->sb.frag_count, &old_tail_nbfree, &old_tail_dsize);
		if (rc == 0) {
			add_nbfree += old_tail_nbfree;
			new_dsize += old_tail_dsize;
		}
	}
	if (rc < 0) {
		fprintf(stderr, "nextufs.resize: failed to update old last cg: %d\n", rc);
		goto fail;
	}
	if (new_cssize > img->sb.csum_size) {
		int32_t reserve_nbfree;
		int32_t reserve_nffree;
		uint32_t old_summary_frags;
		uint32_t new_summary_frags;

		old_summary_frags = (img->sb.csum_size + img->sb.frag_size - 1U) /
		    img->sb.frag_size;
		new_summary_frags = (new_cssize + img->sb.frag_size - 1U) /
		    img->sb.frag_size;
		rc = reserve_summary_extension(img, new_cssize, &reserve_nbfree,
		    &reserve_nffree);
		if (rc < 0) {
			fprintf(stderr,
			    "nextufs.resize: failed to reserve summary extension: %d\n",
			    rc);
			goto fail;
		}
		rc = write_zeroes(img,
		    (off_t)((uint64_t)img->sb.cyl_summary_addr * img->sb.frag_size +
		    img->sb.csum_size),
		    (uint64_t)new_cssize - img->sb.csum_size);
		if (rc < 0) {
			fprintf(stderr,
			    "nextufs.resize: failed to clear summary extension: %d\n", rc);
			goto fail;
		}
		add_nbfree += reserve_nbfree;
		add_nffree += reserve_nffree;
		new_dsize -= new_summary_frags - old_summary_frags;
	}

	for (cg = img->sb.cg_count; cg < new_ncg; cg++) {
		int32_t cg_nbfree;
		int32_t cg_nifree;
		int32_t cg_nffree;
		uint16_t cg_ncyl;

		cg_ncyl = cg == new_ncg - 1U ?
		    (uint16_t)(new_ncyl % img->sb.cpg) : (uint16_t)img->sb.cpg;
		rc = build_new_cg(img, cg, cg_ncyl, 0, 0,
		    cg_buf, &cg_nbfree, &cg_nifree, &cg_nffree);
		if (rc < 0) {
			fprintf(stderr, "nextufs.resize: failed to build cg %u: %d\n",
			    cg, rc);
			goto fail;
		}
		rc = write_zeroes(img,
		    (off_t)(cgimin(img, cg) * img->sb.frag_size),
		    (uint64_t)img->sb.inodes_per_group * 128U);
		if (rc < 0) {
			fprintf(stderr, "nextufs.resize: failed to zero cg %u inodes: %d\n",
			    cg, rc);
			goto fail;
		}
		rc = nextufs_image_pwrite(img, cg_buf, img->sb.cg_size,
		    (off_t)(cgtod(img, cg) * img->sb.frag_size));
		if (rc < 0) {
			fprintf(stderr, "nextufs.resize: failed to write cg %u: %d\n",
			    cg, rc);
			goto fail;
		}
		rc = write_csum_entry(img, cg, 0, cg_nbfree, cg_nifree,
		    cg_nffree);
		if (rc < 0) {
			fprintf(stderr,
			    "nextufs.resize: failed to write cg %u summary: %d\n",
			    cg, rc);
			goto fail;
		}
		new_dsize += (uint32_t)(cgsblock(img, cg) -
		    ((uint64_t)img->sb.frags_per_group * cg));
		new_dsize += img->sb.frags_per_group -
		    (uint32_t)(cgdmin(img, cg) -
		    ((uint64_t)img->sb.frags_per_group * cg));
		add_nbfree += cg_nbfree;
		add_nifree += cg_nifree;
		add_nffree += cg_nffree;
	}

	rc = patch_and_write_superblocks(img, old_sb, new_ncg, new_ncyl,
	    (uint32_t)target_frags, new_dsize, new_csum_addr, new_cssize,
	    add_nbfree, add_nifree, add_nffree);
	if (rc < 0) {
		fprintf(stderr, "nextufs.resize: failed to write superblocks: %d\n", rc);
		goto fail;
	}
	rc = nextufs_image_fsync(img);
	if (rc < 0) {
		fprintf(stderr, "nextufs.resize: fsync failed: %d\n", rc);
		goto fail;
	}

	printf("nextufs.resize: grew %s to %" PRIu64 " 1K sectors (%u cylinder groups)\n",
	    path, sectors, new_ncg);
	free(old_sb);
	free(cg_buf);
	nextufs_image_close(img);
	return 0;

fail:
	fprintf(stderr, "nextufs.resize: grow failed: %d\n", rc);
	free(old_sb);
	free(cg_buf);
	nextufs_image_close(img);
	return 1;
}

int
nextufs_resize_main(int argc, char **argv)
{
	if (argc < 2) {
		usage(stderr, argv[0]);
		return 2;
	}
	if (argc == 2 &&
	    (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
		usage(stdout, argv[0]);
		return 0;
	}
	if (strcmp(argv[1], "grow") == 0) {
		int force = 0;
		int argi = 2;

		if (argc == 3 &&
		    (strcmp(argv[argi], "-h") == 0 ||
		    strcmp(argv[argi], "--help") == 0)) {
			usage(stdout, argv[0]);
			return 0;
		}
		if (argc > argi && strcmp(argv[argi], "--force-size") == 0) {
			force = 1;
			argi++;
		}
		if (argc != argi + 2) {
			usage(stderr, argv[0]);
			return 2;
		}
		return cmd_grow(argv[argi], argv[argi + 1], force);
	}
	usage(stderr, argv[0]);
	return 2;
}

#ifndef NEXTUFS_NO_STANDALONE
int
main(int argc, char **argv)
{
	return nextufs_resize_main(argc, argv);
}
#endif
