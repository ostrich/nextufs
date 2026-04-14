#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define SECTOR_SIZE 512
#define UFS_FS_MAGIC 0x00011954U
#define UFS_CG_MAGIC 0x00090255U
#define DEFAULT_SCAN_LIMIT (32U * 1024U * 1024U)
#define UFS_SBLOCK_OFFSET 0x2000U
#define UFS_SUPER_MAGIC_OFFSET 0x55cU
#define UFS_INODE_SIZE 128U
#define ROOT_INODE 2U
#define LABEL_SCAN_LIMIT (128U * 1024U)
#define LABEL_PART_COUNT 8
#define LABEL_PART_OFF 0x0c0U
#define LABEL_PART_SIZE 64U
#define MAX_LOOKUP_DEPTH 16
#define MAX_PATH_LEN 4096
#define PREVIEW_BYTES 256

#define IFMT 0170000U
#define IFDIR 0040000U
#define IFREG 0100000U
#define IFLNK 0120000U

struct ufs_superblock {
	uint32_t sb_off;
	uint32_t cg_off;
	uint32_t ino_off;
	uint32_t data_off;
	uint32_t cg_delta;
	uint32_t cg_cyc_mask;
	uint32_t write_time;
	uint32_t frag_count;
	uint32_t data_frag_count;
	uint32_t cg_count;
	uint32_t block_size;
	uint32_t frag_size;
	uint32_t frags_per_block;
	uint32_t minfree;
	uint32_t optim;
	uint32_t maxcontig;
	uint32_t maxbpg;
	uint32_t frag_shift;
	uint32_t fsbtodb;
	uint32_t sectors_per_frag;
	uint32_t inodes_per_block;
	uint32_t cyl_summary_addr;
	uint32_t csum_size;
	uint32_t cg_size;
	uint32_t tracks_per_cyl;
	uint32_t sectors_per_track;
	uint32_t sectors_per_cyl;
	uint32_t ncyl;
	uint32_t cpg;
	uint32_t inodes_per_group;
	uint32_t frags_per_group;
	uint32_t clean_flag;
	uint32_t fs_magic;
};

struct ufs_inode {
	uint16_t mode;
	uint16_t nlink;
	uint64_t size;
	uint32_t atime;
	uint32_t mtime;
	uint32_t ctime;
	uint32_t db[12];
	uint32_t ib[3];
	uint32_t uid;
	uint32_t gid;
};

struct next_partition {
	int present;
	uint32_t base_blocks;
	uint32_t size_blocks;
	uint16_t block_size;
	uint16_t frag_size;
	char opt;
	uint8_t automount;
	uint16_t cpg;
	uint16_t density;
	uint8_t minfree;
	uint8_t newfs;
	char mountpt[19];
	char type[9];
};

struct next_disk_label {
	off_t label_off;
	char magic[5];
	uint32_t serial;
	char label[25];
	char name[25];
	char type[25];
	uint16_t secsize;
	uint16_t ntrack;
	uint16_t nsect;
	uint16_t ncyl;
	uint16_t rpm;
	uint16_t front;
	uint16_t back;
	uint16_t ngroups;
	uint16_t ag_size;
	uint16_t ag_alts;
	uint16_t ag_off;
	uint16_t boot0_blkno[2];
	char bootfile[25];
	char hostname[33];
	char rootpartition;
	char rwpartition;
	struct next_partition part[LABEL_PART_COUNT];
};

static int read_exact(int fd, void *buf, size_t size, off_t offset);
static off_t inode_offset_guess(off_t slice_base,
    const struct ufs_superblock *sb, unsigned inode_no);
static int lookup_path(int fd, off_t slice_base, const struct ufs_superblock *sb,
    const char *path, unsigned *inode_no_out, struct ufs_inode *inode_out,
    off_t *inode_off_out);

struct fsck_ctx {
	const struct ufs_superblock *sb;
	uint8_t *frag_seen;
	size_t frag_seen_size;
	uint8_t *allocated;
	uint8_t *reachable;
	uint32_t *link_refs;
	unsigned inode_max;
	unsigned allocated_inodes;
	unsigned reachable_inodes;
	unsigned dir_count;
	unsigned file_count;
	unsigned symlink_count;
	unsigned block_errors;
	unsigned dup_frags;
	unsigned dir_errors;
	unsigned link_mismatches;
};

static int validate_filesystem(int fd, off_t slice_base,
    const struct ufs_superblock *sb, unsigned root_inode_no);
static int scan_allocated_inodes(int fd, off_t slice_base,
    const struct ufs_superblock *sb, struct fsck_ctx *ctx);
static int walk_directory_tree(int fd, off_t slice_base,
    const struct ufs_superblock *sb, struct fsck_ctx *ctx, unsigned inode_no,
    unsigned parent_inode_no, const char *path);
static int check_inode_blocks(int fd, off_t slice_base,
    const struct ufs_superblock *sb, struct fsck_ctx *ctx,
    unsigned inode_no, const struct ufs_inode *ino);
static unsigned g_target_inode = 0;
static const char *g_target_name = NULL;

struct mbr_partition {
	uint8_t status;
	uint8_t chs_first[3];
	uint8_t type;
	uint8_t chs_last[3];
	uint32_t lba_first;
	uint32_t sectors;
};

static uint16_t
read_le16(const uint8_t *p)
{
	return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t
read_le32(const uint8_t *p)
{
	return (uint32_t)p[0] |
	    ((uint32_t)p[1] << 8) |
	    ((uint32_t)p[2] << 16) |
	    ((uint32_t)p[3] << 24);
}

static uint32_t
read_be32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) |
	    ((uint32_t)p[1] << 16) |
	    ((uint32_t)p[2] << 8) |
	    (uint32_t)p[3];
}

static uint64_t
read_be64(const uint8_t *p)
{
	return ((uint64_t)read_be32(p) << 32) | read_be32(p + 4);
}

static uint16_t
read_be16(const uint8_t *p)
{
	return ((uint16_t)p[0] << 8) | (uint16_t)p[1];
}

static uint32_t
read_be24(const uint8_t *p)
{
	return ((uint32_t)p[0] << 16) |
	    ((uint32_t)p[1] << 8) |
	    (uint32_t)p[2];
}

static void
copy_cstr_field(char *dst, size_t dst_size, const uint8_t *src, size_t src_size)
{
	size_t n;

	n = 0;
	while (n < src_size && src[n] != '\0')
		n++;
	if (n >= dst_size)
		n = dst_size - 1;
	memcpy(dst, src, n);
	dst[n] = '\0';
}

static void
print_partition(const struct mbr_partition *part, int index)
{
	if (part->type == 0 && part->lba_first == 0 && part->sectors == 0)
		return;

	printf("partition %d: boot=0x%02x type=0x%02x start_lba=%" PRIu32
	    " sectors=%" PRIu32 " size=%" PRIu64 " bytes\n",
	    index,
	    part->status,
	    part->type,
	    part->lba_first,
	    part->sectors,
	    (uint64_t)part->sectors * SECTOR_SIZE);
}

static void
probe_mbr(const uint8_t *sector0)
{
	int i;
	int nonempty = 0;

	printf("sector0 signature: 0x%04x\n", read_le16(sector0 + 510));
	for (i = 0; i < 4; i++) {
		const uint8_t *base = sector0 + 446 + (i * 16);
		struct mbr_partition part;

		part.status = base[0];
		memcpy(part.chs_first, base + 1, 3);
		part.type = base[4];
		memcpy(part.chs_last, base + 5, 3);
		part.lba_first = read_le32(base + 8);
		part.sectors = read_le32(base + 12);
		if (part.type != 0 || part.lba_first != 0 || part.sectors != 0) {
			print_partition(&part, i);
			nonempty = 1;
		}
	}
	if (!nonempty)
		printf("mbr partitions: none populated\n");
}

static void
scan_magic(const uint8_t *buf, size_t size, uint32_t magic, const char *label)
{
	size_t off;
	int hits = 0;

	for (off = 0; off + 4 <= size; off += 4) {
		uint32_t le = read_le32(buf + off);
		uint32_t be = read_be32(buf + off);

		if (le == magic || be == magic) {
			printf("%s candidate at image offset 0x%zx (%zu), sector %zu, %s-endian\n",
			    label, off, off, off / SECTOR_SIZE, le == magic ? "little" : "big");
			hits++;
			if (hits >= 32) {
				printf("%s: hit limit reached, stopping scan\n", label);
				return;
			}
		}
	}
	if (hits == 0)
		printf("%s candidates: none in first %zu bytes\n", label, size);
}

static void
decode_superblock(struct ufs_superblock *sb, const uint8_t *buf)
{
	sb->sb_off = read_be32(buf + 0x08);
	sb->cg_off = read_be32(buf + 0x0c);
	sb->ino_off = read_be32(buf + 0x10);
	sb->data_off = read_be32(buf + 0x14);
	sb->cg_delta = read_be32(buf + 0x18);
	sb->cg_cyc_mask = read_be32(buf + 0x1c);
	sb->write_time = read_be32(buf + 0x20);
	sb->frag_count = read_be32(buf + 0x24);
	sb->data_frag_count = read_be32(buf + 0x28);
	sb->cg_count = read_be32(buf + 0x2c);
	sb->block_size = read_be32(buf + 0x30);
	sb->frag_size = read_be32(buf + 0x34);
	sb->frags_per_block = read_be32(buf + 0x38);
	sb->minfree = read_be32(buf + 0x3c);
	sb->maxcontig = read_be32(buf + 0x44);
	sb->optim = read_be32(buf + 0x58);
	sb->maxbpg = read_be32(buf + 0x5c);
	sb->frag_shift = read_be32(buf + 0x60);
	sb->fsbtodb = read_be32(buf + 0x7c);
	sb->sectors_per_frag = read_be32(buf + 0x80);
	sb->inodes_per_block = read_be32(buf + 0x78);
	sb->cyl_summary_addr = read_be32(buf + 0x98);
	sb->csum_size = read_be32(buf + 0x9c);
	sb->cg_size = read_be32(buf + 0x0a0);
	sb->tracks_per_cyl = read_be32(buf + 0x0a4);
	sb->sectors_per_track = read_be32(buf + 0x0a8);
	sb->sectors_per_cyl = read_be32(buf + 0x0ac);
	sb->ncyl = read_be32(buf + 0x0b0);
	sb->cpg = read_be32(buf + 0x0b4);
	sb->inodes_per_group = read_be32(buf + 0x0b8);
	sb->frags_per_group = read_be32(buf + 0x0bc);
	sb->clean_flag = read_be32(buf + 0x15c);
	sb->fs_magic = read_be32(buf + UFS_SUPER_MAGIC_OFFSET);
}

static void
print_superblock(const struct ufs_superblock *sb, off_t slice_base, off_t super_off)
{
	printf("candidate slice base: 0x%jx (%jd)\n",
	    (uintmax_t)slice_base, (intmax_t)slice_base);
	printf("superblock base:      0x%jx (%jd)\n",
	    (uintmax_t)super_off, (intmax_t)super_off);
	printf("superblock magic:     0x%08" PRIx32 " (big-endian)\n", sb->fs_magic);
	printf("block size:           %" PRIu32 "\n", sb->block_size);
	printf("fragment size:        %" PRIu32 "\n", sb->frag_size);
	printf("frags/block:          %" PRIu32 "\n", sb->frags_per_block);
	printf("frags total/data:     %" PRIu32 " / %" PRIu32 "\n",
	    sb->frag_count, sb->data_frag_count);
	printf("cylinder groups:      %" PRIu32 "\n", sb->cg_count);
	printf("cylinders/group:      %" PRIu32 "\n", sb->cpg);
	printf("inodes/group:         %" PRIu32 "\n", sb->inodes_per_group);
	printf("frags/group:          %" PRIu32 "\n", sb->frags_per_group);
	printf("inodes/block:         %" PRIu32 "\n", sb->inodes_per_block);
	printf("frag shift:           %" PRIu32 "\n", sb->frag_shift);
	printf("fsbtodb shift:        %" PRIu32 "\n", sb->fsbtodb);
	printf("sectors/frag:         %" PRIu32 "\n", sb->sectors_per_frag);
	printf("sb/cg/ino/data offs:  %" PRIu32 " / %" PRIu32 " / %" PRIu32
	    " / %" PRIu32 " fragments\n",
	    sb->sb_off, sb->cg_off, sb->ino_off, sb->data_off);
	printf("cg delta/mask:        %" PRIu32 " / 0x%08" PRIx32 "\n",
	    sb->cg_delta, sb->cg_cyc_mask);
	printf("cg summary addr/size: %" PRIu32 " / %" PRIu32 "\n",
	    sb->cyl_summary_addr, sb->csum_size);
	printf("cg struct size:       %" PRIu32 "\n", sb->cg_size);
	printf("tracks/sector/cyl:    %" PRIu32 " / %" PRIu32 " / %" PRIu32 "\n",
	    sb->tracks_per_cyl, sb->sectors_per_track, sb->sectors_per_cyl);
	printf("ncyl:                 %" PRIu32 "\n", sb->ncyl);
}

static int
decode_next_disk_label(struct next_disk_label *dl, const uint8_t *buf, off_t off)
{
	size_t i;

	if (memcmp(buf, "dlV3", 4) != 0)
		return -1;
	memset(dl, 0, sizeof(*dl));
	dl->label_off = off;
	memcpy(dl->magic, buf, 4);
	dl->magic[4] = '\0';
	dl->serial = read_be32(buf + 0x04);
	copy_cstr_field(dl->label, sizeof(dl->label), buf + 0x0c, 24);
	copy_cstr_field(dl->name, sizeof(dl->name), buf + 0x2c, 24);
	copy_cstr_field(dl->type, sizeof(dl->type), buf + 0x44, 24);
	dl->secsize = read_be16(buf + 0x5e);
	dl->ntrack = read_be16(buf + 0x62);
	dl->nsect = read_be16(buf + 0x66);
	dl->ncyl = read_be16(buf + 0x6a);
	dl->rpm = read_be16(buf + 0x6e);
	dl->front = read_be16(buf + 0x70);
	dl->back = read_be16(buf + 0x72);
	dl->ngroups = read_be16(buf + 0x74);
	dl->ag_size = read_be16(buf + 0x76);
	dl->ag_alts = read_be16(buf + 0x78);
	dl->ag_off = read_be16(buf + 0x7e);
	dl->boot0_blkno[0] = read_be16(buf + 0x82);
	copy_cstr_field(dl->bootfile, sizeof(dl->bootfile), buf + 0x84, 24);
	copy_cstr_field(dl->hostname, sizeof(dl->hostname), buf + 0x9c, 32);
	dl->rootpartition = (char)buf[0xbc];
	dl->rwpartition = (char)buf[0xbd];
	for (i = 0; i < LABEL_PART_COUNT; i++) {
		const uint8_t *p = buf + LABEL_PART_OFF + (i * LABEL_PART_SIZE);
		struct next_partition *part = &dl->part[i];
		int all_zero = 1;
		int all_ff = 1;
		size_t j;

		for (j = 0; j < LABEL_PART_SIZE; j++) {
			if (p[j] != 0x00)
				all_zero = 0;
			if (p[j] != 0xff)
				all_ff = 0;
		}
		part->base_blocks = read_be24(p + 0x00);
		part->size_blocks = read_be24(p + 0x03);
		part->block_size = read_be16(p + 0x06);
		part->frag_size = read_be16(p + 0x08);
		part->opt = (char)p[0x0a];
		part->automount = 0;
		part->cpg = read_be16(p + 0x0c);
		part->density = read_be16(p + 0x0e);
		part->minfree = p[0x10];
		part->newfs = p[0x11];
		copy_cstr_field(part->mountpt, sizeof(part->mountpt), p + 0x12, 17);
		copy_cstr_field(part->type, sizeof(part->type), p + 0x23, 8);
		part->present = !all_zero && !all_ff &&
		    part->size_blocks != 0 &&
		    part->size_blocks != 0xffffffU &&
		    part->block_size != 0 &&
		    part->frag_size != 0 &&
		    part->block_size != 0xffff &&
		    part->frag_size != 0xffff &&
		    (part->block_size % 512) == 0 &&
		    (part->frag_size % 512) == 0 &&
		    part->block_size >= part->frag_size &&
		    part->cpg != 0xffff &&
		    part->density != 0xffff;
	}
	return dl->secsize == 0 ? -1 : 0;
}

static void
print_next_disk_label(const struct next_disk_label *dl)
{
	size_t i;

	printf("NeXT disk label:      '%s' at 0x%jx magic=%s serial=%" PRIu32 "\n",
	    dl->label[0] ? dl->label : "(empty)", (uintmax_t)dl->label_off,
	    dl->magic, dl->serial);
	printf("label name/type:      '%s' / '%s'\n", dl->name, dl->type);
	printf("sector geom:          secsize=%u ntrack=%u nsect=%u ncyl=%u rpm=%u\n",
	    dl->secsize, dl->ntrack, dl->nsect, dl->ncyl, dl->rpm);
	printf("porches/ag:           front=%u back=%u ngroups=%u ag_size=%u ag_alts=%u ag_off=%u\n",
	    dl->front, dl->back, dl->ngroups, dl->ag_size, dl->ag_alts, dl->ag_off);
	printf("boot/root/rw:         boot0=%u bootfile='%s' host='%s' root=%c rw=%c\n",
	    dl->boot0_blkno[0], dl->bootfile, dl->hostname,
	    dl->rootpartition ? dl->rootpartition : '-',
	    dl->rwpartition ? dl->rwpartition : '-');
	printf("label-derived slice:  0x%jx (%ju)\n",
	    (uintmax_t)((uint64_t)dl->front * dl->secsize),
	    (uintmax_t)((uint64_t)dl->front * dl->secsize));
	printf("partitions:\n");
	for (i = 0; i < LABEL_PART_COUNT; i++) {
		const struct next_partition *part = &dl->part[i];
		uint64_t abs_base;

		if (!part->present)
			continue;
		abs_base = ((uint64_t)dl->front + part->base_blocks) * dl->secsize;
		printf("  %c: base=%" PRIu32 " size=%" PRIu32 " abs=0x%jx block=%u frag=%u "
		    "cpg=%u density=%u minfree=%u newfs=%u opt=%c automnt=%u "
		    "type='%s' mount='%s'\n",
		    (int)('a' + i),
		    part->base_blocks,
		    part->size_blocks,
		    (uintmax_t)abs_base,
		    part->block_size,
		    part->frag_size,
		    part->cpg,
		    part->density,
		    part->minfree,
		    part->newfs,
		    part->opt ? part->opt : '-',
		    part->automount,
		    part->type[0] ? part->type : "",
		    part->mountpt[0] ? part->mountpt : "");
	}
}

static int
pick_label_slice(const struct next_disk_label *dl, off_t *slice_base_out,
    char *part_name_out)
{
	size_t i;

	if (dl->rootpartition >= 'a' &&
	    dl->rootpartition < 'a' + LABEL_PART_COUNT) {
		i = (size_t)(dl->rootpartition - 'a');
		if (dl->part[i].present && dl->part[i].size_blocks != 0) {
			*slice_base_out =
			    (off_t)(((uint64_t)dl->front + dl->part[i].base_blocks) *
			    dl->secsize);
			if (part_name_out != NULL)
				*part_name_out = dl->rootpartition;
			return 0;
		}
	}

	for (i = 0; i < LABEL_PART_COUNT; i++) {
		if (!dl->part[i].present || dl->part[i].size_blocks == 0)
			continue;
		if (strcmp(dl->part[i].type, "4.3BSD") != 0)
			continue;
		*slice_base_out =
		    (off_t)(((uint64_t)dl->front + dl->part[i].base_blocks) *
		    dl->secsize);
		if (part_name_out != NULL)
			*part_name_out = (char)('a' + i);
		return 0;
	}

	for (i = 0; i < LABEL_PART_COUNT; i++) {
		if (!dl->part[i].present || dl->part[i].size_blocks == 0)
			continue;
		*slice_base_out =
		    (off_t)(((uint64_t)dl->front + dl->part[i].base_blocks) *
		    dl->secsize);
		if (part_name_out != NULL)
			*part_name_out = (char)('a' + i);
		return 0;
	}

	*slice_base_out = (off_t)((uint64_t)dl->front * dl->secsize);
	if (part_name_out != NULL)
		*part_name_out = '-';
	return 0;
}

static int
append_path_component(char *path, size_t path_size, const char *component)
{
	char tmp[MAX_PATH_LEN];
	int n;

	if (strcmp(path, "/") == 0 || path[0] == '\0')
		n = snprintf(tmp, sizeof(tmp), "/%s", component);
	else
		n = snprintf(tmp, sizeof(tmp), "%s/%s", path, component);
	if (n < 0 || (size_t)n >= path_size)
		return -1;
	memcpy(path, tmp, (size_t)n + 1);
	return 0;
}

static void
decode_inode(struct ufs_inode *ino, const uint8_t *buf)
{
	size_t i;

	ino->mode = read_be16(buf + 0x00);
	ino->nlink = read_be16(buf + 0x02);
	ino->size = read_be64(buf + 0x08);
	ino->atime = read_be32(buf + 0x10);
	ino->mtime = read_be32(buf + 0x18);
	ino->ctime = read_be32(buf + 0x20);
	for (i = 0; i < 12; i++)
		ino->db[i] = read_be32(buf + 0x28 + (i * 4));
	for (i = 0; i < 3; i++)
		ino->ib[i] = read_be32(buf + 0x58 + (i * 4));
	ino->uid = read_be32(buf + 0x70);
	ino->gid = read_be32(buf + 0x74);
}

static void
print_inode(const struct ufs_inode *ino, off_t off, unsigned inode_no)
{
	printf("inode %u at 0x%jx: mode=0%o nlink=%u uid=%u gid=%u size=%" PRIu64 "\n",
	    inode_no, (uintmax_t)off, ino->mode, ino->nlink, ino->uid, ino->gid, ino->size);
	printf("inode %u direct blocks:", inode_no);
	for (size_t i = 0; i < 12; i++) {
		if (ino->db[i] == 0)
			break;
		printf(" %" PRIu32, ino->db[i]);
	}
	printf("\n");
}

static int
read_dirent_v1(const uint8_t *buf, size_t size, size_t off, uint32_t *ino,
    uint16_t *reclen, uint8_t *type, uint8_t *namlen, const char **name)
{
	if (off + 8 > size)
		return -1;
	*ino = read_be32(buf + off + 0);
	*reclen = read_be16(buf + off + 4);
	*type = buf[off + 6];
	*namlen = buf[off + 7];
	if (*reclen < 8 || off + *reclen > size || off + 8 + *namlen > size)
		return -1;
	*name = (const char *)(buf + off + 8);
	return 0;
}

static int
read_inode_by_number(int fd, off_t slice_base, const struct ufs_superblock *sb,
    unsigned inode_no, struct ufs_inode *ino, off_t *ino_off)
{
	uint8_t ibuf[UFS_INODE_SIZE];
	off_t off;

	off = inode_offset_guess(slice_base, sb, inode_no);
	if (off < 0)
		return -1;
	if (read_exact(fd, ibuf, sizeof(ibuf), off) < 0)
		return -1;
	decode_inode(ino, ibuf);
	if (ino_off != NULL)
		*ino_off = off;
	return 0;
}

static int
read_indirect_entry(int fd, off_t slice_base, const struct ufs_superblock *sb,
    uint32_t block_frag, uint64_t entry_index, uint32_t *entry_out)
{
	uint8_t entry_buf[4];
	uint64_t entries_per_block;
	off_t entry_off;

	entries_per_block = sb->block_size / sizeof(uint32_t);
	if (block_frag == 0 || entry_index >= entries_per_block)
		return -1;
	entry_off = slice_base + ((off_t)block_frag * sb->frag_size) +
	    (off_t)(entry_index * sizeof(uint32_t));
	if (read_exact(fd, entry_buf, sizeof(entry_buf), entry_off) < 0)
		return -1;
	*entry_out = read_be32(entry_buf);
	return 0;
}

static int
resolve_indirect_block_frag(int fd, off_t slice_base, const struct ufs_superblock *sb,
    uint32_t block_frag, unsigned level, uint64_t logical_index, uint32_t *data_frag_out)
{
	uint64_t entries_per_block;
	uint64_t span;
	uint64_t entry_index;
	uint64_t remainder;
	uint32_t next_frag;

	entries_per_block = sb->block_size / sizeof(uint32_t);
	if (block_frag == 0 || level == 0 || entries_per_block == 0)
		return -1;
	span = 1;
	for (unsigned i = 1; i < level; i++)
		span *= entries_per_block;
	entry_index = logical_index / span;
	remainder = logical_index % span;
	if (read_indirect_entry(fd, slice_base, sb, block_frag, entry_index,
	    &next_frag) < 0)
		return -1;
	if (level == 1) {
		*data_frag_out = next_frag;
		return 0;
	}
	if (next_frag == 0) {
		*data_frag_out = 0;
		return 0;
	}
	return resolve_indirect_block_frag(fd, slice_base, sb, next_frag,
	    level - 1, remainder, data_frag_out);
}

static int
resolve_file_block_frag(int fd, off_t slice_base, const struct ufs_superblock *sb,
    const struct ufs_inode *ino, uint64_t logical_block_index, uint32_t *data_frag_out)
{
	uint64_t entries_per_block;
	uint64_t span;
	uint64_t indirect_index;

	if (logical_block_index < 12) {
		*data_frag_out = ino->db[logical_block_index];
		return 0;
	}
	logical_block_index -= 12;
	entries_per_block = sb->block_size / sizeof(uint32_t);
	span = entries_per_block;
	for (indirect_index = 0; indirect_index < 3; indirect_index++) {
		if (logical_block_index < span)
			return resolve_indirect_block_frag(fd, slice_base, sb,
			    ino->ib[indirect_index], (unsigned)indirect_index + 1,
			    logical_block_index, data_frag_out);
		logical_block_index -= span;
		span *= entries_per_block;
	}
	return -1;
}

static int
read_inode_data(int fd, off_t slice_base, const struct ufs_superblock *sb,
    const struct ufs_inode *ino, uint64_t start, uint8_t *buf, size_t buf_size,
    size_t *bytes_read)
{
	uint64_t file_remaining;
	uint64_t logical_block_index;
	size_t done;
	size_t block_offset;

	if (start >= ino->size) {
		if (bytes_read != NULL)
			*bytes_read = 0;
		return 0;
	}
	file_remaining = ino->size - start;
	if (file_remaining > buf_size)
		file_remaining = buf_size;
	done = 0;
	logical_block_index = start / sb->block_size;
	block_offset = (size_t)(start % sb->block_size);
	while (file_remaining > 0) {
		size_t chunk_size;
		uint32_t data_frag;

		chunk_size = sb->block_size - block_offset;
		if (chunk_size > file_remaining)
			chunk_size = (size_t)file_remaining;
		if (resolve_file_block_frag(fd, slice_base, sb, ino, logical_block_index,
		    &data_frag) < 0)
			return -1;
		if (data_frag == 0)
			memset(buf + done, 0, chunk_size);
		else if (read_exact(fd, buf + done, chunk_size,
		    slice_base + ((off_t)data_frag * sb->frag_size) + (off_t)block_offset) < 0)
			return -1;
		done += chunk_size;
		file_remaining -= chunk_size;
		logical_block_index++;
		block_offset = 0;
	}
	if (bytes_read != NULL)
		*bytes_read = done;
	return 0;
}

static int
find_name_in_directory(int fd, off_t slice_base, const struct ufs_superblock *sb,
    const struct ufs_inode *dirino, const char *target_name, unsigned *target_inode)
{
	uint8_t *buf;
	uint64_t remaining;
	size_t block_index;
	size_t target_len;

	target_len = strlen(target_name);
	*target_inode = 0;

	buf = malloc(sb->block_size);
	if (buf == NULL) {
		fprintf(stderr, "malloc failed for directory buffer\n");
		return -1;
	}

	remaining = dirino->size;
	for (block_index = 0; remaining > 0; block_index++) {
		size_t off = 0;
		size_t chunk_size;

		chunk_size = remaining < sb->block_size ? (size_t)remaining : sb->block_size;
		if (read_inode_data(fd, slice_base, sb, dirino,
		    (uint64_t)block_index * sb->block_size, buf, chunk_size, NULL) < 0) {
			free(buf);
			return -1;
		}
		while (off < chunk_size) {
			uint32_t d_ino;
			uint16_t d_reclen;
			uint8_t d_type;
			uint8_t d_namlen;
			const char *d_name;

			if (read_dirent_v1(buf, chunk_size, off, &d_ino, &d_reclen,
			    &d_type, &d_namlen, &d_name) < 0)
				break;
			if (d_ino != 0 && target_len == d_namlen &&
			    memcmp(d_name, target_name, d_namlen) == 0) {
				*target_inode = d_ino;
				free(buf);
				return 0;
			}
			off += d_reclen;
		}
		remaining -= chunk_size;
	}

	free(buf);
	return 0;
}

static off_t
inode_offset_guess(off_t slice_base, const struct ufs_superblock *sb, unsigned inode_no)
{
	uint64_t cg;
	uint64_t cgbase;
	uint64_t cgstart;
	uint64_t cgimin;
	uint64_t frag_addr;
	uint64_t inum_in_group;
	uint64_t inode_slot;

	if (inode_no == 0 || sb->inodes_per_group == 0 || sb->frags_per_group == 0 ||
	    sb->inodes_per_block == 0)
		return -1;
	cg = inode_no / sb->inodes_per_group;
	inum_in_group = inode_no % sb->inodes_per_group;
	cgbase = (uint64_t)sb->frags_per_group * cg;
	cgstart = cgbase + ((uint64_t)sb->cg_delta *
	    (cg & (uint64_t)(~sb->cg_cyc_mask)));
	cgimin = cgstart + sb->ino_off;
	frag_addr = cgimin + ((inum_in_group / sb->inodes_per_block) << sb->frag_shift);
	inode_slot = inum_in_group % sb->inodes_per_block;
	return slice_base + (off_t)(frag_addr * sb->frag_size) +
	    (off_t)(inode_slot * UFS_INODE_SIZE);
}

static int
dump_directory(int fd, off_t slice_base, const struct ufs_superblock *sb,
    const struct ufs_inode *dirino, unsigned inode_no)
{
	uint8_t *buf;
	uint64_t remaining;
	size_t block_index;

	buf = malloc(sb->block_size);
	if (buf == NULL) {
		fprintf(stderr, "malloc failed for directory buffer\n");
		return -1;
	}

	printf("directory listing for inode %u:\n", inode_no);
	remaining = dirino->size;
	for (block_index = 0; remaining > 0; block_index++) {
		size_t off = 0;
		size_t chunk_size;

		chunk_size = remaining < sb->block_size ? (size_t)remaining : sb->block_size;
		if (read_inode_data(fd, slice_base, sb, dirino,
		    (uint64_t)block_index * sb->block_size, buf, chunk_size, NULL) < 0) {
			free(buf);
			return -1;
		}
		while (off < chunk_size) {
			uint32_t d_ino;
			uint16_t d_reclen;
			uint8_t d_type;
			uint8_t d_namlen;
			const char *d_name;

			if (read_dirent_v1(buf, chunk_size, off, &d_ino, &d_reclen,
			    &d_type, &d_namlen, &d_name) < 0)
				break;
			if (d_ino != 0) {
				printf("  ino=%-6" PRIu32 " reclen=%-4u type=%-2u name='",
				    d_ino, d_reclen, d_type);
				fwrite(d_name, 1, d_namlen, stdout);
				printf("'\n");
			}
			off += d_reclen;
		}
		remaining -= chunk_size;
	}

	free(buf);
	return 0;
}

static size_t
decode_inline_symlink(const struct ufs_inode *ino, char *out, size_t out_size)
{
	uint8_t raw[60];
	size_t nbytes;

	if ((ino->mode & IFMT) != IFLNK)
		return 0;
	if (ino->size == 0 || ino->size > sizeof(raw) || out_size == 0)
		return 0;
	for (size_t i = 0; i < 12; i++) {
		raw[(i * 4) + 0] = (uint8_t)(ino->db[i] >> 24);
		raw[(i * 4) + 1] = (uint8_t)(ino->db[i] >> 16);
		raw[(i * 4) + 2] = (uint8_t)(ino->db[i] >> 8);
		raw[(i * 4) + 3] = (uint8_t)ino->db[i];
	}
	for (size_t i = 0; i < 3; i++) {
		raw[48 + (i * 4) + 0] = (uint8_t)(ino->ib[i] >> 24);
		raw[48 + (i * 4) + 1] = (uint8_t)(ino->ib[i] >> 16);
		raw[48 + (i * 4) + 2] = (uint8_t)(ino->ib[i] >> 8);
		raw[48 + (i * 4) + 3] = (uint8_t)ino->ib[i];
	}
	nbytes = (size_t)ino->size;
	if (nbytes >= out_size)
		nbytes = out_size - 1;
	memcpy(out, raw, nbytes);
	out[nbytes] = '\0';
	return nbytes;
}

static int
read_symlink_target(int fd, off_t slice_base, const struct ufs_superblock *sb,
    const struct ufs_inode *ino, char *out, size_t out_size)
{
	size_t got;

	if ((ino->mode & IFMT) != IFLNK || out_size == 0)
		return -1;
	if (decode_inline_symlink(ino, out, out_size) != 0)
		return 0;
	if (read_inode_data(fd, slice_base, sb, ino, 0, (uint8_t *)out,
	    out_size - 1, &got) < 0)
		return -1;
	out[got] = '\0';
	return 0;
}

static void
print_data_preview(const uint8_t *buf, size_t size)
{
	size_t off;

	printf("data preview (%zu bytes):\n", size);
	for (off = 0; off < size; off += 16) {
		size_t i;
		size_t line_size = size - off < 16 ? size - off : 16;

		printf("  %04zx:", off);
		for (i = 0; i < 16; i++) {
			if (i < line_size)
				printf(" %02x", buf[off + i]);
			else
				printf("   ");
		}
		printf("  ");
		for (i = 0; i < line_size; i++) {
			unsigned char c = buf[off + i];
			putchar(c >= 32 && c < 127 ? c : '.');
		}
		printf("\n");
	}
}

static int
lookup_path_recursive(int fd, off_t slice_base, const struct ufs_superblock *sb,
    const char *path, unsigned *inode_no_out, struct ufs_inode *inode_out,
    off_t *inode_off_out, unsigned depth)
{
	char *scratch;
	char *segment;
	char *cursor;
	char prefix[MAX_PATH_LEN];
	unsigned current_inode_no;
	struct ufs_inode current_inode;
	off_t current_inode_off;

	if (depth > MAX_LOOKUP_DEPTH) {
		fprintf(stderr, "lookup '%s': too many symlink expansions\n", path);
		return -1;
	}
	if (read_inode_by_number(fd, slice_base, sb, ROOT_INODE, &current_inode,
	    &current_inode_off) < 0)
		return -1;
	current_inode_no = ROOT_INODE;
	strcpy(prefix, "/");
	while (*path == '/')
		path++;
	if (*path == '\0') {
		*inode_no_out = current_inode_no;
		*inode_out = current_inode;
		if (inode_off_out != NULL)
			*inode_off_out = current_inode_off;
		return 0;
	}

	scratch = strdup(path);
	if (scratch == NULL) {
		perror("strdup");
		return -1;
	}

	cursor = scratch;
	while ((segment = strsep(&cursor, "/")) != NULL) {
		unsigned next_inode_no;
		char remainder[MAX_PATH_LEN];
		char link_target[MAX_PATH_LEN];
		char new_path[MAX_PATH_LEN];

		if (*segment == '\0')
			continue;
		if ((current_inode.mode & IFMT) != IFDIR) {
			fprintf(stderr, "lookup stopped at inode %u: not a directory\n",
			    current_inode_no);
			free(scratch);
			return -1;
		}
		if (find_name_in_directory(fd, slice_base, sb, &current_inode, segment,
		    &next_inode_no) < 0) {
			free(scratch);
			return -1;
		}
		if (next_inode_no == 0) {
			fprintf(stderr, "lookup '%s': component '%s' not found\n",
			    path, segment);
			free(scratch);
			return -1;
		}
		if (read_inode_by_number(fd, slice_base, sb, next_inode_no,
		    &current_inode, &current_inode_off) < 0) {
			free(scratch);
			return -1;
		}
		if (cursor != NULL && *cursor != '\0') {
			if (snprintf(remainder, sizeof(remainder), "%s", cursor) >=
			    (int)sizeof(remainder)) {
				free(scratch);
				return -1;
			}
		} else
			remainder[0] = '\0';
		if ((current_inode.mode & IFMT) == IFLNK) {
			if (read_symlink_target(fd, slice_base, sb, &current_inode,
			    link_target, sizeof(link_target)) < 0) {
				free(scratch);
				return -1;
			}
			if (link_target[0] == '/') {
				if (remainder[0] != '\0') {
					if (snprintf(new_path, sizeof(new_path), "%s/%s",
					    link_target, remainder) >= (int)sizeof(new_path)) {
						free(scratch);
						return -1;
					}
				} else if (snprintf(new_path, sizeof(new_path), "%s",
				    link_target) >= (int)sizeof(new_path)) {
					free(scratch);
					return -1;
				}
			} else {
				if (remainder[0] != '\0') {
					if (snprintf(new_path, sizeof(new_path), "%s/%s/%s",
					    prefix, link_target, remainder) >= (int)sizeof(new_path)) {
						free(scratch);
						return -1;
					}
				} else if (snprintf(new_path, sizeof(new_path), "%s/%s",
				    prefix, link_target) >= (int)sizeof(new_path)) {
					free(scratch);
					return -1;
				}
			}
			free(scratch);
			return lookup_path_recursive(fd, slice_base, sb, new_path,
			    inode_no_out, inode_out, inode_off_out, depth + 1);
		}
		current_inode_no = next_inode_no;
		if (append_path_component(prefix, sizeof(prefix), segment) < 0) {
			free(scratch);
			return -1;
		}
	}

	free(scratch);
	*inode_no_out = current_inode_no;
	*inode_out = current_inode;
	if (inode_off_out != NULL)
		*inode_off_out = current_inode_off;
	return 0;
}

static int
lookup_path(int fd, off_t slice_base, const struct ufs_superblock *sb,
    const char *path, unsigned *inode_no_out, struct ufs_inode *inode_out,
    off_t *inode_off_out)
{
	return lookup_path_recursive(fd, slice_base, sb, path, inode_no_out,
	    inode_out, inode_off_out, 0);
}

static int
check_inode_blocks(int fd, off_t slice_base, const struct ufs_superblock *sb,
    struct fsck_ctx *ctx, unsigned inode_no, const struct ufs_inode *ino)
{
	uint64_t logical_blocks;
	uint64_t i;

	if ((ino->mode & IFMT) != IFDIR &&
	    (ino->mode & IFMT) != IFREG &&
	    (ino->mode & IFMT) != IFLNK)
		return 0;
	if ((ino->mode & IFMT) == IFLNK && ino->size <=
	    (sizeof(ino->db) + sizeof(ino->ib)))
		return 0;

	logical_blocks = (ino->size + sb->block_size - 1) / sb->block_size;
	for (i = 0; i < logical_blocks; i++) {
		uint32_t frag;
		uint64_t remaining;
		uint32_t frag_count;
		uint32_t j;

		if (resolve_file_block_frag(fd, slice_base, sb, ino, i, &frag) < 0) {
			fprintf(stderr, "phase1: inode %u block %" PRIu64
			    " resolution failed\n", inode_no, i);
			ctx->block_errors++;
			continue;
		}
		if (frag == 0)
			continue;
		remaining = ino->size - (i * (uint64_t)sb->block_size);
		frag_count = remaining >= sb->block_size ?
		    sb->frags_per_block :
		    (uint32_t)((remaining + sb->frag_size - 1) / sb->frag_size);
		if (frag + frag_count > sb->frag_count) {
			fprintf(stderr, "phase1: inode %u block %" PRIu64
			    " points outside filesystem (frag=%" PRIu32
			    " count=%" PRIu32 ")\n",
			    inode_no, i, frag, frag_count);
			ctx->block_errors++;
			continue;
		}
		for (j = 0; j < frag_count; j++) {
			uint32_t idx = frag + j;
			if (idx >= ctx->frag_seen_size)
				continue;
			if (ctx->frag_seen[idx] != 0)
				ctx->dup_frags++;
			ctx->frag_seen[idx] = 1;
		}
	}
	return 0;
}

static int
scan_allocated_inodes(int fd, off_t slice_base, const struct ufs_superblock *sb,
    struct fsck_ctx *ctx)
{
	unsigned inode_no;

	for (inode_no = 1; inode_no <= ctx->inode_max; inode_no++) {
		struct ufs_inode ino;
		off_t inode_off;
		uint16_t type;

		if (read_inode_by_number(fd, slice_base, sb, inode_no, &ino,
		    &inode_off) < 0)
			return -1;
		type = ino.mode & IFMT;
		if (type == 0)
			continue;
		ctx->allocated[inode_no] = 1;
		ctx->allocated_inodes++;
		switch (type) {
		case IFDIR:
			ctx->dir_count++;
			break;
		case IFREG:
			ctx->file_count++;
			break;
		case IFLNK:
			ctx->symlink_count++;
			break;
		default:
			break;
		}
		if (check_inode_blocks(fd, slice_base, sb, ctx, inode_no, &ino) < 0)
			return -1;
	}
	return 0;
}

static int
walk_directory_tree(int fd, off_t slice_base, const struct ufs_superblock *sb,
    struct fsck_ctx *ctx, unsigned inode_no, unsigned parent_inode_no,
    const char *path)
{
	struct ufs_inode dirino;
	off_t inode_off;
	uint8_t *buf;
	uint64_t offset;
	int saw_dot = 0;
	int saw_dotdot = 0;

	if (inode_no == 0 || inode_no > ctx->inode_max)
		return -1;
	if (ctx->reachable[inode_no] != 0)
		return 0;
	if (read_inode_by_number(fd, slice_base, sb, inode_no, &dirino, &inode_off) < 0)
		return -1;
	if ((dirino.mode & IFMT) != IFDIR)
		return -1;

	ctx->reachable[inode_no] = 1;
	ctx->reachable_inodes++;
	buf = malloc(sb->block_size);
	if (buf == NULL) {
		perror("malloc");
		return -1;
	}

	for (offset = 0; offset < dirino.size; offset += sb->block_size) {
		size_t got = 0;
		size_t pos = 0;

		if (read_inode_data(fd, slice_base, sb, &dirino, offset, buf,
		    sb->block_size, &got) < 0) {
			free(buf);
			return -1;
		}
		while (pos + 8 <= got) {
			uint32_t child_ino;
			uint16_t reclen;
			uint8_t type;
			uint8_t namlen;
			const char *name;

			if (read_dirent_v1(buf, got, pos, &child_ino, &reclen, &type,
			    &namlen, &name) < 0)
				break;
			if (reclen == 0 || pos + reclen > got) {
				fprintf(stderr, "phase2: directory %s has invalid record "
				    "at offset %" PRIu64 "\n", path, offset + pos);
				ctx->dir_errors++;
				break;
			}
			if (child_ino != 0) {
				if (child_ino <= ctx->inode_max)
					ctx->link_refs[child_ino]++;
				if (namlen == 1 && memcmp(name, ".", 1) == 0) {
					saw_dot = (child_ino == inode_no);
				} else if (namlen == 2 && memcmp(name, "..", 2) == 0) {
					saw_dotdot = (child_ino == parent_inode_no);
				} else {
					struct ufs_inode child;
					off_t child_off;

					if (child_ino > ctx->inode_max ||
					    read_inode_by_number(fd, slice_base, sb, child_ino,
					    &child, &child_off) < 0) {
						fprintf(stderr, "phase2: %s references invalid inode %"
						    PRIu32 "\n", path, child_ino);
						ctx->dir_errors++;
					} else {
						if (ctx->reachable[child_ino] == 0) {
							ctx->reachable[child_ino] = 1;
							ctx->reachable_inodes++;
						}
						if ((child.mode & IFMT) == IFDIR) {
						char child_path[MAX_PATH_LEN];
						int n;

						if (strcmp(path, "/") == 0)
							n = snprintf(child_path, sizeof(child_path),
							    "/%.*s", (int)namlen, name);
						else
							n = snprintf(child_path, sizeof(child_path),
							    "%s/%.*s", path, (int)namlen, name);
						if (n > 0 && (size_t)n < sizeof(child_path))
							if (walk_directory_tree(fd, slice_base, sb, ctx,
							    child_ino, inode_no, child_path) < 0) {
								free(buf);
								return -1;
							}
						}
					}
				}
			}
			pos += reclen;
		}
	}

	if (!saw_dot || !saw_dotdot) {
		fprintf(stderr, "phase2: directory %s missing valid %s%s%s entry\n",
		    path,
		    !saw_dot ? "." : "",
		    (!saw_dot && !saw_dotdot) ? " and " : "",
		    !saw_dotdot ? ".." : "");
		ctx->dir_errors++;
	}
	free(buf);
	return 0;
}

static int
validate_filesystem(int fd, off_t slice_base, const struct ufs_superblock *sb,
    unsigned root_inode_no)
{
	struct fsck_ctx ctx;
	unsigned inode_no;

	memset(&ctx, 0, sizeof(ctx));
	ctx.sb = sb;
	ctx.inode_max = sb->cg_count * sb->inodes_per_group;
	ctx.frag_seen_size = sb->frag_count;
	ctx.frag_seen = calloc(ctx.frag_seen_size, 1);
	ctx.allocated = calloc(ctx.inode_max + 1, 1);
	ctx.reachable = calloc(ctx.inode_max + 1, 1);
	ctx.link_refs = calloc(ctx.inode_max + 1, sizeof(uint32_t));
	if (ctx.frag_seen == NULL || ctx.allocated == NULL ||
	    ctx.reachable == NULL || ctx.link_refs == NULL) {
		perror("calloc");
		free(ctx.frag_seen);
		free(ctx.allocated);
		free(ctx.reachable);
		free(ctx.link_refs);
		return -1;
	}

	printf("** Phase 1 - Check Blocks and Sizes\n");
	if (scan_allocated_inodes(fd, slice_base, sb, &ctx) < 0)
		goto fail;

	printf("** Phase 2 - Check Pathnames\n");
	printf("** Phase 3 - Check Connectivity\n");
	if (walk_directory_tree(fd, slice_base, sb, &ctx, root_inode_no,
	    root_inode_no, "/") < 0)
		goto fail;

	printf("** Phase 4 - Check Reference Counts\n");
	for (inode_no = 1; inode_no <= ctx.inode_max; inode_no++) {
		struct ufs_inode ino;
		off_t inode_off;

		if (!ctx.allocated[inode_no])
			continue;
		if (read_inode_by_number(fd, slice_base, sb, inode_no, &ino,
		    &inode_off) < 0)
			goto fail;
		if (ino.nlink != ctx.link_refs[inode_no]) {
			if ((ino.mode & IFMT) == IFDIR || ctx.link_refs[inode_no] != 0) {
				fprintf(stderr, "phase4: inode %u nlink=%u observed=%" PRIu32 "\n",
				    inode_no, ino.nlink, ctx.link_refs[inode_no]);
				ctx.link_mismatches++;
			}
		}
		if ((ino.mode & IFMT) == IFDIR && !ctx.reachable[inode_no]) {
			fprintf(stderr, "phase3: unreachable directory inode %u\n",
			    inode_no);
			ctx.dir_errors++;
		}
	}

	printf("** Phase 5 - Check Cyl groups\n");
	printf("phase5: cylinder-group summary verification not implemented yet\n");
	printf("%u files, %u directories, %u symlinks, %u allocated inodes\n",
	    ctx.file_count, ctx.dir_count, ctx.symlink_count, ctx.allocated_inodes);
	printf("reachable inodes: %u\n", ctx.reachable_inodes);
	printf("block issues: %u, duplicate fragments: %u, directory issues: %u, link mismatches: %u\n",
	    ctx.block_errors, ctx.dup_frags, ctx.dir_errors, ctx.link_mismatches);

	free(ctx.frag_seen);
	free(ctx.allocated);
	free(ctx.reachable);
	free(ctx.link_refs);
	return (ctx.block_errors == 0 && ctx.dir_errors == 0 &&
	    ctx.link_mismatches == 0) ? 0 : 1;

fail:
	free(ctx.frag_seen);
	free(ctx.allocated);
	free(ctx.reachable);
	free(ctx.link_refs);
	return -1;
}

static int
probe_candidate_superblock(int fd, off_t magic_off)
{
	uint8_t sbuf[2048];
	uint8_t ibuf[UFS_INODE_SIZE];
	uint8_t dirbuf[1024];
	struct ufs_superblock sb;
	struct ufs_inode dirino;
	off_t super_off;
	off_t slice_base;
	off_t dir_off;
	off_t inode_off;
	off_t inferred_inode_base = -1;
	unsigned inferred_inode_no = 0;
	unsigned root_candidate_no = 0;
	off_t root_candidate_off = -1;
	struct ufs_inode root_candidate;
	uint32_t dir_ino;
	uint16_t dir_reclen;
	uint8_t dir_type;
	uint8_t dir_namlen;
	const char *dir_name;
	size_t scan_off;

	super_off = magic_off - (off_t)UFS_SUPER_MAGIC_OFFSET;
	slice_base = super_off - (off_t)UFS_SBLOCK_OFFSET;
	if (slice_base < 0)
		return -1;
	if (read_exact(fd, sbuf, sizeof(sbuf), super_off) < 0)
		return -1;
	decode_superblock(&sb, sbuf);
	if (sb.fs_magic != UFS_FS_MAGIC || sb.block_size == 0 || sb.frag_size == 0)
		return -1;

	print_superblock(&sb, slice_base, super_off);

	/*
	 * Infer the inode table base by scanning early filesystem space for
	 * directories whose "." entry points back to themselves.
	 */
	for (scan_off = 0; scan_off < 0x20000; scan_off += UFS_INODE_SIZE) {
		inode_off = slice_base + (off_t)scan_off;
		if (read_exact(fd, ibuf, sizeof(ibuf), inode_off) < 0)
			return -1;
		decode_inode(&dirino, ibuf);
		if ((dirino.mode & IFMT) != IFDIR || dirino.db[0] == 0)
			continue;
		dir_off = slice_base + ((off_t)dirino.db[0] * sb.frag_size);
		if (read_exact(fd, dirbuf, sizeof(dirbuf), dir_off) < 0)
			return -1;
		if (read_dirent_v1(dirbuf, sizeof(dirbuf), 0, &dir_ino, &dir_reclen,
		    &dir_type, &dir_namlen, &dir_name) < 0)
			continue;
		if (dir_namlen != 1 || memcmp(dir_name, ".", 1) != 0)
			continue;
		inferred_inode_no = dir_ino;
		inferred_inode_base =
		    inode_off - (((off_t)inferred_inode_no - 1) * UFS_INODE_SIZE);
		printf("directory candidate: inode=%u at 0x%jx data=0x%jx inferred_inode_base=0x%jx\n",
		    inferred_inode_no,
		    (uintmax_t)inode_off,
		    (uintmax_t)dir_off,
		    (uintmax_t)inferred_inode_base);
		print_inode(&dirino, inode_off, inferred_inode_no);
		if (root_candidate_no == 0 || inferred_inode_no < root_candidate_no) {
			root_candidate_no = inferred_inode_no;
			root_candidate_off = inode_off;
			root_candidate = dirino;
		}
	}
	if (root_candidate_no == 0)
		return -1;

	printf("lowest directory inode candidate: %u at 0x%jx\n",
	    root_candidate_no, (uintmax_t)root_candidate_off);
	print_inode(&root_candidate, root_candidate_off, root_candidate_no);
	dir_off = slice_base + ((off_t)root_candidate.db[0] * sb.frag_size);
	if (read_exact(fd, dirbuf, sizeof(dirbuf), dir_off) < 0)
		return -1;
	printf("candidate directory block: 0x%jx\n", (uintmax_t)dir_off);
	if (read_dirent_v1(dirbuf, sizeof(dirbuf), 0, &dir_ino, &dir_reclen,
	    &dir_type, &dir_namlen, &dir_name) == 0) {
		printf("dir[0]: ino=%" PRIu32 " reclen=%u type=%u namlen=%u name='",
		    dir_ino, dir_reclen, dir_type, dir_namlen);
		fwrite(dir_name, 1, dir_namlen, stdout);
		printf("'\n");
	}
	if (read_dirent_v1(dirbuf, sizeof(dirbuf), dir_reclen, &dir_ino, &dir_reclen,
	    &dir_type, &dir_namlen, &dir_name) == 0) {
		printf("dir[1]: ino=%" PRIu32 " reclen=%u type=%u namlen=%u name='",
		    dir_ino, dir_reclen, dir_type, dir_namlen);
		fwrite(dir_name, 1, dir_namlen, stdout);
		printf("'\n");
	}
	if (validate_filesystem(fd, slice_base, &sb, root_candidate_no) < 0)
		return -1;
	if (g_target_name != NULL) {
		off_t target_off;
		struct ufs_inode target;
		char linkbuf[MAX_PATH_LEN];
		uint8_t preview[PREVIEW_BYTES];
		size_t preview_size = 0;

		if (lookup_path(fd, slice_base, &sb, g_target_name, &g_target_inode,
		    &target, &target_off) == 0) {
			printf("lookup '%s': inode %u\n", g_target_name, g_target_inode);
			printf("lookup '%s': guessed inode offset 0x%jx\n",
			    g_target_name, (uintmax_t)target_off);
			print_inode(&target, target_off, g_target_inode);
			if ((target.mode & IFMT) == IFDIR) {
				if (dump_directory(fd, slice_base, &sb, &target,
				    g_target_inode) < 0)
					return -1;
			} else if ((target.mode & IFMT) == IFLNK) {
				if (read_symlink_target(fd, slice_base, &sb, &target,
				    linkbuf, sizeof(linkbuf)) == 0)
					printf("symlink target:        '%s'\n", linkbuf);
			} else if ((target.mode & IFMT) == IFREG) {
				if (read_inode_data(fd, slice_base, &sb, &target, 0,
				    preview, sizeof(preview), &preview_size) < 0)
					return -1;
				print_data_preview(preview, preview_size);
				if (target.size > (uint64_t)(12 * sb.block_size)) {
					size_t indirect_preview_size = 0;
					uint64_t boundary = (uint64_t)12 * sb.block_size;

					if (read_inode_data(fd, slice_base, &sb, &target, boundary,
					    preview, sizeof(preview), &indirect_preview_size) < 0)
						return -1;
					printf("data preview at first indirect block boundary "
					    "(offset 0x%jx):\n", (uintmax_t)boundary);
					print_data_preview(preview, indirect_preview_size);
				}
			}
		}
	}
	return 0;
}

static int
read_exact(int fd, void *buf, size_t size, off_t offset)
{
	uint8_t *out = buf;
	size_t done = 0;

	while (done < size) {
		ssize_t n = pread(fd, out + done, size - done, offset + (off_t)done);
		if (n < 0) {
			perror("pread");
			return -1;
		}
		if (n == 0) {
			fprintf(stderr, "short read at offset %jd\n", (intmax_t)(offset + (off_t)done));
			return -1;
		}
		done += (size_t)n;
	}
	return 0;
}

int
main(int argc, char **argv)
{
	const char *path;
	struct stat st;
	struct next_disk_label dl;
	uint8_t sector0[SECTOR_SIZE];
	uint8_t *scanbuf = NULL;
	size_t scan_size;
	size_t label_scan_size;
	int fd;
	size_t off;
	int decoded = 0;
	off_t label_slice_base;
	char label_part_name;

	if (argc != 2 && argc != 3) {
		fprintf(stderr, "usage: %s <raw-image> [name-in-/]\n", argv[0]);
		return 1;
	}
	path = argv[1];
	if (argc == 3)
		g_target_name = argv[2];
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		perror(path);
		return 1;
	}
	if (fstat(fd, &st) < 0) {
		perror("fstat");
		close(fd);
		return 1;
	}
	printf("image: %s\n", path);
	printf("size: %" PRIuMAX " bytes\n", (uintmax_t)st.st_size);

	if (read_exact(fd, sector0, sizeof(sector0), 0) < 0) {
		close(fd);
		return 1;
	}
	probe_mbr(sector0);

	scan_size = DEFAULT_SCAN_LIMIT;
	if ((off_t)scan_size > st.st_size)
		scan_size = (size_t)st.st_size;
	scanbuf = malloc(scan_size);
	if (scanbuf == NULL) {
		fprintf(stderr, "malloc failed for %zu bytes\n", scan_size);
		close(fd);
		return 1;
	}
	if (read_exact(fd, scanbuf, scan_size, 0) < 0) {
		free(scanbuf);
		close(fd);
		return 1;
	}

	label_scan_size = scan_size < LABEL_SCAN_LIMIT ? scan_size : LABEL_SCAN_LIMIT;
	for (off = 0; off + LABEL_PART_OFF +
	    (LABEL_PART_COUNT * LABEL_PART_SIZE) <= label_scan_size;
	    off += 0x200) {
		if (memcmp(scanbuf + off, "dlV3", 4) != 0)
			continue;
		if (decode_next_disk_label(&dl, scanbuf + off, (off_t)off) < 0)
			continue;
		print_next_disk_label(&dl);
		if (pick_label_slice(&dl, &label_slice_base, &label_part_name) < 0)
			continue;
		printf("selected label slice: partition %c at 0x%jx (%jd)\n",
		    label_part_name ? label_part_name : '-',
		    (uintmax_t)label_slice_base,
		    (intmax_t)label_slice_base);
		off = (size_t)(label_slice_base + UFS_SBLOCK_OFFSET +
		    UFS_SUPER_MAGIC_OFFSET);
		if ((off_t)off < 0 || off + 4 > scan_size)
			continue;
		if (read_be32(scanbuf + off) == UFS_FS_MAGIC &&
		    probe_candidate_superblock(fd, (off_t)off) == 0) {
			decoded = 1;
			break;
		}
	}
	if (decoded) {
		free(scanbuf);
		close(fd);
		return 0;
	}

	printf("scan window: first %zu bytes\n", scan_size);
	scan_magic(scanbuf, scan_size, UFS_FS_MAGIC, "ufs superblock magic");
	scan_magic(scanbuf, scan_size, UFS_CG_MAGIC, "ufs cylinder-group magic");
	for (off = 0; off + 4 <= scan_size; off += 4) {
		if (read_be32(scanbuf + off) != UFS_FS_MAGIC)
			continue;
		if (probe_candidate_superblock(fd, (off_t)off) == 0) {
			decoded = 1;
			break;
		}
	}
	if (!decoded)
		printf("decoded superblock: none validated yet\n");

	free(scanbuf);
	close(fd);
	return 0;
}
