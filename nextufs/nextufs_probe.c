#include "nextufs.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PREVIEW_BYTES 256

static void
print_superblock(const struct nextufs_image *img)
{
	printf("image size:            %jd bytes\n", (intmax_t)img->image_size);
	printf("slice base:            0x%jx (%jd)\n",
	    (uintmax_t)img->slice_base, (intmax_t)img->slice_base);
	printf("superblock base:       0x%jx (%jd)\n",
	    (uintmax_t)(img->slice_base + 0x2000),
	    (intmax_t)(img->slice_base + 0x2000));
	printf("superblock magic:      0x%08" PRIx32 "\n", img->sb.fs_magic);
	printf("block size:            %" PRIu32 "\n", img->sb.block_size);
	printf("fragment size:         %" PRIu32 "\n", img->sb.frag_size);
	printf("frags/block:           %" PRIu32 "\n", img->sb.frags_per_block);
	printf("frags total/data:      %" PRIu32 " / %" PRIu32 "\n",
	    img->sb.frag_count, img->sb.data_frag_count);
	printf("cylinder groups:       %" PRIu32 "\n", img->sb.cg_count);
	printf("cylinders/group:       %" PRIu32 "\n", img->sb.cpg);
	printf("inodes/group:          %" PRIu32 "\n", img->sb.inodes_per_group);
	printf("frags/group:           %" PRIu32 "\n", img->sb.frags_per_group);
	printf("inodes/block:          %" PRIu32 "\n", img->sb.inodes_per_block);
	printf("fsbtodb shift:         %" PRIu32 "\n", img->sb.fsbtodb);
	printf("nindir:                %" PRIu32 "\n", img->sb.nindir);
	printf("optim/state:           %" PRIu32 " / %u\n",
	    img->sb.optim, (unsigned)img->sb.state);
}

static void
print_inode(const struct nextufs_inode *ino, off_t off, unsigned inode_no)
{
	size_t i;

	printf("inode %u at 0x%jx: mode=0%o nlink=%u uid=%u gid=%u size=%" PRIu64 "\n",
	    inode_no, (uintmax_t)off, ino->mode, ino->nlink, ino->uid, ino->gid,
	    ino->size);
	if ((ino->mode & NEXTUFS_IFMT) == NEXTUFS_IFCHR ||
	    (ino->mode & NEXTUFS_IFMT) == NEXTUFS_IFBLK)
		printf("inode %u rdev:         %" PRIu32 "\n", inode_no, ino->db[0]);
	printf("inode %u direct blocks:", inode_no);
	for (i = 0; i < 12; i++) {
		if (ino->db[i] == 0)
			break;
		printf(" %" PRIu32, ino->db[i]);
	}
	printf("\n");
	printf("inode %u indirect blocks:", inode_no);
	for (i = 0; i < 3; i++) {
		if (ino->ib[i] == 0)
			break;
		printf(" %" PRIu32, ino->ib[i]);
	}
	printf("\n");
}

struct dump_dir_ctx {
	int count;
};

static int
dump_dir_cb(uint32_t ino, const char *name, size_t name_len, void *ctx_ptr)
{
	struct dump_dir_ctx *ctx = ctx_ptr;

	printf("  ino=%-6" PRIu32 " name='", ino);
	fwrite(name, 1, name_len, stdout);
	printf("'\n");
	ctx->count++;
	return 0;
}

static void
dump_directory(const struct nextufs_image *img, const struct nextufs_inode *dirino,
    unsigned inode_no)
{
	struct dump_dir_ctx ctx;

	ctx.count = 0;
	printf("directory listing for inode %u:\n", inode_no);
	if (nextufs_directory_iterate(img, dirino, dump_dir_cb, &ctx) < 0)
		fprintf(stderr, "directory iteration failed\n");
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

int
main(int argc, char **argv)
{
	struct nextufs_image img;
	struct nextufs_probe_info probe;
	struct nextufs_node node;
	int rc;

	if (argc != 2 && argc != 3) {
		fprintf(stderr, "usage: %s <source> [path]\n", argv[0]);
		return 1;
	}
	rc = nextufs_image_open(&img, argv[1]);
	if (rc < 0) {
		fprintf(stderr, "failed to open source %s\n", argv[1]);
		return 1;
	}
	printf("source: %s\n", argv[1]);
	nextufs_probe_info_get(&img, &probe);
	if (probe.used_disk_label) {
		printf("disk label:            version=0x%08x off=0x%jx secsize=%u front=%u root=%c\n",
		    probe.label_version, (uintmax_t)probe.label_off,
		    probe.label_secsize, probe.label_front,
		    probe.rootpartition ? probe.rootpartition : '?');
	}
	print_superblock(&img);
	rc = nextufs_node_get_root(&img, &node);
	if (rc < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	print_inode(&node.inode, node.inode_off, node.inode_no);
	dump_directory(&img, &node.inode, node.inode_no);
	if (argc == 3) {
		rc = nextufs_node_lookup(&img, argv[2], 1, &node);
		if (rc == 0) {
			uint8_t preview[PREVIEW_BYTES];
			size_t got = 0;
			char linkbuf[4096];

			printf("lookup '%s': inode %u\n", argv[2], node.inode_no);
			print_inode(&node.inode, node.inode_off, node.inode_no);
			if ((node.inode.mode & NEXTUFS_IFMT) == NEXTUFS_IFDIR) {
				dump_directory(&img, &node.inode, node.inode_no);
			} else if ((node.inode.mode & NEXTUFS_IFMT) == NEXTUFS_IFLNK) {
				if (nextufs_inode_readlink(&img, &node.inode, linkbuf,
				    sizeof(linkbuf)) == 0)
					printf("symlink target:        '%s'\n", linkbuf);
			} else if ((node.inode.mode & NEXTUFS_IFMT) == NEXTUFS_IFREG) {
				if (nextufs_inode_read_data(&img, &node.inode, 0, preview,
				    sizeof(preview), &got) == 0)
					print_data_preview(preview, got);
			}
		} else {
			fprintf(stderr, "lookup '%s' failed\n", argv[2]);
		}
	}
	nextufs_image_close(&img);
	return 0;
}
