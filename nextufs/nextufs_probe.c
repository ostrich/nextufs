#include "nextufs.h"
#include "nextufs_inspect.h"
#include "nextufs_report.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PREVIEW_BYTES 256

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
nextufs_probe_main(int argc, char **argv)
{
	struct nextufs_image img;
	struct nextufs_inspect_info info;
	struct nextufs_node node;
	int json = 0;
	int argi = 1;
	int rc;

	if (argc > argi && strcmp(argv[argi], "--json") == 0) {
		json = 1;
		argi++;
	}
	if (argc != argi + 1 && argc != argi + 2) {
		fprintf(stderr, "usage: %s [--json] <source> [path]\n", argv[0]);
		return 1;
	}
	if (json && argc != argi + 1) {
		fprintf(stderr, "%s: --json is only supported for image inspection\n",
		    argv[0]);
		return 1;
	}
	rc = nextufs_image_open(&img, argv[argi]);
	if (rc < 0) {
		fprintf(stderr, "failed to open source %s\n", argv[argi]);
		return 1;
	}
	nextufs_inspect_collect(&img, 0, &info);
	if (json) {
		nextufs_report_inspect_json(stdout, argv[argi], &info);
		nextufs_image_close(&img);
		return 0;
	}
	nextufs_report_inspect_text(stdout, argv[argi], &info);
	rc = nextufs_node_get_root(&img, &node);
	if (rc < 0) {
		nextufs_image_close(&img);
		return 1;
	}
	print_inode(&node.inode, node.inode_off, node.inode_no);
	dump_directory(&img, &node.inode, node.inode_no);
	if (argc == argi + 2) {
		rc = nextufs_node_lookup(&img, argv[argi + 1], 1, &node);
		if (rc == 0) {
			uint8_t preview[PREVIEW_BYTES];
			size_t got = 0;
			char linkbuf[4096];

			printf("lookup '%s': inode %u\n", argv[argi + 1], node.inode_no);
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
			fprintf(stderr, "lookup '%s' failed\n", argv[argi + 1]);
		}
	}
	nextufs_image_close(&img);
	return 0;
}

#ifndef NEXTUFS_NO_STANDALONE
int
main(int argc, char **argv)
{
	return nextufs_probe_main(argc, argv);
}
#endif
