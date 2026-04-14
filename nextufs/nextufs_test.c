#include "nextufs_read.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct expect_name_ctx {
	const char *target;
	int found;
};

struct expect_node_ctx {
	const char *target;
	int found;
	int saw_regular;
};

static void
fail(const char *msg)
{
	fprintf(stderr, "nextufs_test: %s\n", msg);
	exit(1);
}

static int
find_name_cb(uint32_t ino, const char *name, size_t name_len, void *ctx_ptr)
{
	struct expect_name_ctx *ctx = ctx_ptr;
	(void)ino;

	if (strlen(ctx->target) == name_len &&
	    memcmp(name, ctx->target, name_len) == 0)
		ctx->found = 1;
	return 0;
}

static int
find_node_cb(const struct nextufs_node *node, const char *name, size_t name_len,
    void *ctx_ptr)
{
	struct expect_node_ctx *ctx = ctx_ptr;

	if (strlen(ctx->target) == name_len &&
	    memcmp(name, ctx->target, name_len) == 0) {
		ctx->found = 1;
		ctx->saw_regular = nextufs_node_is_reg(node);
	}
	return 0;
}

static void
expect_dir_contains(const struct nextufs_image *img, const char *path,
    const char *name)
{
	struct expect_name_ctx ctx;

	ctx.target = name;
	ctx.found = 0;
	if (nextufs_iterate_directory_path(img, path, 1, find_name_cb, &ctx) < 0)
		fail("directory iteration failed");
	if (!ctx.found) {
		fprintf(stderr, "nextufs_test: '%s' missing from directory '%s'\n",
		    name, path);
		exit(1);
	}
}

int
main(int argc, char **argv)
{
	struct nextufs_image img;
	struct nextufs_probe_info probe;
	struct nextufs_node root;
	struct nextufs_node node;
	struct stat st;
	struct statvfs stvfs;
	char linkbuf[256];
	unsigned char buf[256];
	size_t got;
	struct expect_node_ctx node_ctx;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <raw-image>\n", argv[0]);
		return 2;
	}
	if (nextufs_open_image(&img, argv[1]) < 0)
		fail("open_image failed");
	nextufs_get_probe_info(&img, &probe);
	if (!probe.used_disk_label)
		fail("expected disklabel-backed probe");
	if (probe.label_version == 0 || probe.slice_base <= 0 || probe.slice_size <= 0)
		fail("probe info incomplete");

	if (nextufs_get_root(&img, &root) < 0)
		fail("get_root failed");
	if (root.inode_no != NEXTUFS_ROOT_INODE)
		fail("root inode number mismatch");
	if ((root.inode.mode & NEXTUFS_IFMT) != NEXTUFS_IFDIR)
		fail("root is not a directory");
	if (nextufs_get_node_by_inode(&img, NEXTUFS_ROOT_INODE, &node) < 0)
		fail("get_node_by_inode root failed");
	if (node.inode_off != root.inode_off)
		fail("get_node_by_inode root mismatch");
	if (!nextufs_node_is_dir(&root))
		fail("root dir helper mismatch");

	expect_dir_contains(&img, "/", "etc");
	expect_dir_contains(&img, "/", "usr");
	expect_dir_contains(&img, "/", "mach_kernel");
	expect_dir_contains(&img, "/etc", "passwd");
	node_ctx.target = "mach_kernel";
	node_ctx.found = 0;
	node_ctx.saw_regular = 0;
	if (nextufs_iterate_directory_nodes_path(&img, "/", 1, find_node_cb,
	    &node_ctx) < 0)
		fail("directory node iteration failed");
	if (!node_ctx.found || !node_ctx.saw_regular)
		fail("directory node iteration missing mach_kernel regular file");

	if (nextufs_lookup(&img, "/etc", 0, &node) < 0)
		fail("lookup nofollow /etc failed");
	if ((node.inode.mode & NEXTUFS_IFMT) != NEXTUFS_IFLNK)
		fail("/etc nofollow is not a symlink");
	if (!nextufs_node_is_lnk(&node))
		fail("/etc symlink helper mismatch");
	if (nextufs_readlink_path(&img, "/etc", linkbuf, sizeof(linkbuf)) < 0)
		fail("readlink /etc failed");
	if (strcmp(linkbuf, "private/etc") != 0)
		fail("/etc symlink target mismatch");

	if (nextufs_lookup(&img, "/etc", 1, &node) < 0)
		fail("lookup follow /etc failed");
	if ((node.inode.mode & NEXTUFS_IFMT) != NEXTUFS_IFDIR)
		fail("/etc follow is not a directory");

	if (nextufs_lookup(&img, "/etc/passwd", 1, &node) < 0)
		fail("lookup /etc/passwd failed");
	if ((node.inode.mode & NEXTUFS_IFMT) != NEXTUFS_IFREG)
		fail("/etc/passwd is not a regular file");
	if (!nextufs_node_is_reg(&node))
		fail("/etc/passwd regular helper mismatch");
	if (nextufs_node_stat(&node, &st) < 0)
		fail("stat on /etc/passwd failed");
	if (st.st_size <= 0)
		fail("/etc/passwd size invalid");
	if (st.st_ino != node.inode_no)
		fail("/etc/passwd inode stat mismatch");

	if (nextufs_statvfs(&img, &stvfs) < 0)
		fail("statvfs failed");
	if (stvfs.f_bsize == 0 || stvfs.f_frsize == 0 || stvfs.f_blocks == 0)
		fail("statvfs geometry invalid");
	if (stvfs.f_files == 0 || stvfs.f_ffree == 0)
		fail("statvfs inode counts invalid");
	if (nextufs_check_access(&node, node.inode.uid, node.inode.gid, R_OK) < 0)
		fail("access read check failed");
	if (nextufs_check_access(&node, node.inode.uid, node.inode.gid, W_OK) != -EROFS)
		fail("access write check failed");

	got = 0;
	if (nextufs_read_path(&img, "/etc/passwd", 0, buf, sizeof(buf), &got) < 0)
		fail("read_path /etc/passwd failed");
	if (got < 32)
		fail("/etc/passwd preview too short");
	if (memcmp(buf, "#\n# You probably", 16) != 0)
		fail("/etc/passwd contents mismatch");

	got = 0;
	if (nextufs_read_path(&img, "/mach_kernel", 0x18000, buf, sizeof(buf),
	    &got) < 0)
		fail("read_path /mach_kernel failed");
	if (got == 0)
		fail("/mach_kernel indirect read returned no data");

	nextufs_close_image(&img);
	printf("nextufs_test: ok\n");
	return 0;
}
