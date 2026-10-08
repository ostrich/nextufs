#include "nextufs_node.h"
#include "nextufs_report.h"

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define PREVIEW_BYTES 256

static void
print_record(const struct nextufs_node *node, const char *name, size_t len)
{
	const struct nextufs_inode *ino = &node->inode;

	printf("  {\"inode\": %u, \"mode\": %u, \"uid\": %u, \"gid\": %u, "
	    "\"size\": %" PRIu64 ", \"atime\": %" PRIu32 ", \"mtime\": %" PRIu32 ", \"name\": ",
	    node->inode_no, ino->mode, ino->uid, ino->gid, ino->size,
	    ino->atime, ino->mtime);
	nextufs_report_json_string(stdout, name, len);
	putchar('}');
}

static int
print_entry(const struct nextufs_node *node, const char *name,
    size_t len, void *ctx_ptr)
{
	(void)ctx_ptr;
	if ((len == 1 && name[0] == '.') ||
	    (len == 2 && name[0] == '.' && name[1] == '.'))
		return 0;
	fputs(",\n", stdout);
	print_record(node, name, len);
	return 0;
}

static int
browse_json(const struct nextufs_image *img, const char *path)
{
	struct nextufs_node node;
	int rc = nextufs_node_lookup(img, path, 0, &node);

	if (rc < 0)
		return rc;
	fputs("[\n", stdout);
	print_record(&node, ".", 1);
	if (nextufs_node_is_dir(&node))
		rc = nextufs_directory_iterate_nodes(img, &node.inode,
		    print_entry, NULL);
	fputs("\n]\n", stdout);
	return rc;
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

static int
dump_directory(const struct nextufs_image *img, const struct nextufs_inode *dirino,
    unsigned inode_no)
{
	struct dump_dir_ctx ctx;
	int rc;

	ctx.count = 0;
	printf("directory listing for inode %u:\n", inode_no);
	rc = nextufs_directory_iterate(img, dirino, dump_dir_cb, &ctx);
	if (rc < 0)
		fprintf(stderr, "directory iteration failed\n");
	return rc;
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
nextufs_browse_main(int argc, char **argv)
{
	struct nextufs_image img;
	struct nextufs_node node;
	int argi = 1;
	int raw = 0;
	int json = 0;
	int rc;

	if (argc > 1 && strcmp(argv[1], "--raw") == 0) {
		raw = 1;
		argi++;
	} else if (argc > 1 && strcmp(argv[1], "--json") == 0) {
		json = 1;
		argi++;
	}
	if ((raw && argc != argi + 2) ||
	    (argc != argi + 1 && argc != argi + 2)) {
		fprintf(stderr, "usage: %s [--raw|--json] <source> [path]\n", argv[0]);
		return 1;
	}
	rc = nextufs_image_open_source(&img, argv[argi],
	    NEXTUFS_OPEN_READ_ONLY);
	if (rc < 0) {
		nextufs_report_errno(stderr, "browse", "open source",
		    argv[argi], rc);
		return 1;
	}
	if (json) {
		const char *path = argc == argi + 2 ? argv[argi + 1] : "/";
		rc = browse_json(&img, path);
		if (rc < 0)
			nextufs_report_errno(stderr, "browse", "inspect path", path, rc);
		nextufs_image_close(&img);
		return rc < 0 || fflush(stdout) == EOF || ferror(stdout) ? 1 : 0;
	}
	rc = nextufs_node_get_root(&img, &node);
	if (rc < 0) {
		nextufs_report_errno(stderr, "browse", "read root inode",
		    argv[argi], rc);
		nextufs_image_close(&img);
		return 1;
	}
	if (!raw) {
		print_inode(&node.inode, node.inode_off, node.inode_no);
		rc = dump_directory(&img, &node.inode, node.inode_no);
		if (rc < 0) {
			nextufs_image_close(&img);
			return 1;
		}
	}
	if (argc == argi + 2) {
		rc = nextufs_node_lookup(&img, argv[argi + 1], 1, &node);
		if (rc == 0) {
			uint8_t preview[PREVIEW_BYTES];
			size_t got = 0;
			char linkbuf[4096];

			if (!raw) {
				printf("lookup '%s': inode %u\n", argv[argi + 1], node.inode_no);
				print_inode(&node.inode, node.inode_off, node.inode_no);
			}
			if (raw && (node.inode.mode & NEXTUFS_IFMT) == NEXTUFS_IFREG) {
				uint8_t data[65536];
				uint64_t offset = 0;

				while (offset < node.inode.size) {
					size_t wanted = sizeof(data);
					size_t all = 0;
					if (node.inode.size - offset < wanted)
						wanted = (size_t)(node.inode.size - offset);
					rc = nextufs_inode_read_data(&img, &node.inode, offset,
					    data, wanted, &all);
					if (rc < 0 || all != wanted) {
						nextufs_report_errno(stderr, "browse", "read file",
						    argv[argi + 1], rc < 0 ? rc : -EIO);
						nextufs_image_close(&img);
						return 1;
					}
					if (fwrite(data, 1, all, stdout) != all) {
						nextufs_image_close(&img);
						return 1;
					}
					offset += all;
				}
			} else if (raw) {
				nextufs_report_errno(stderr, "browse", "extract regular file",
				    argv[argi + 1], -EINVAL);
				nextufs_image_close(&img);
				return 1;
			} else if ((node.inode.mode & NEXTUFS_IFMT) == NEXTUFS_IFDIR) {
				rc = dump_directory(&img, &node.inode, node.inode_no);
			} else if ((node.inode.mode & NEXTUFS_IFMT) == NEXTUFS_IFLNK) {
				rc = nextufs_inode_readlink(&img, &node.inode, linkbuf,
				    sizeof(linkbuf));
				if (rc == 0)
					printf("symlink target:        '%s'\n", linkbuf);
			} else if ((node.inode.mode & NEXTUFS_IFMT) == NEXTUFS_IFREG) {
				rc = nextufs_inode_read_data(&img, &node.inode, 0, preview,
				    sizeof(preview), &got);
				if (rc == 0)
					print_data_preview(preview, got);
			}
			if (rc < 0) {
				nextufs_report_errno(stderr, "browse", "read path",
				    argv[argi + 1], rc);
				nextufs_image_close(&img);
				return 1;
			}
		} else {
			nextufs_report_errno(stderr, "browse", "lookup path",
			    argv[argi + 1], rc);
			nextufs_image_close(&img);
			return 1;
		}
	}
	nextufs_image_close(&img);
	return fflush(stdout) == EOF || ferror(stdout) ? 1 : 0;
}
