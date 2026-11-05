#include "nextufs_mutate.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
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

static const struct nextufs_write_ctx write_ctx = { .policy = NEXTUFS_WRITE_SU };

static void
check_result(int rc, const char *operation)
{
	if (rc < 0) {
		fprintf(stderr, "nextufs_test: %s: %s\n", operation, strerror(-rc));
		exit(1);
	}
}

static struct nextufs_superblock
read_superblock(const char *path)
{
	struct nextufs_image img;
	struct nextufs_superblock sb;

	check_result(nextufs_image_open(&img, path), "open test image");
	sb = img.sb;
	nextufs_image_close(&img);
	return sb;
}

static uint64_t
free_fragments(const struct nextufs_superblock *sb)
{
	return (uint64_t)sb->free_block_count * sb->frags_per_block + sb->free_frag_count;
}

static void
check_fsck(const char *image)
{
	const char *tool = getenv("NEXTUFS");
	pid_t child;
	int status;

	if (tool == NULL)
		tool = "./nextufs";
	fflush(stdout);
	child = fork();
	if (child < 0)
		fail("fork fsck failed");
	if (child == 0) {
		execlp(tool, tool, "fsck", "-n", image, (char *)NULL);
		_exit(127);
	}
	while (waitpid(child, &status, 0) < 0) {
		if (errno != EINTR)
			fail("wait for fsck failed");
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		fail("fragment test image failed fsck");
}

static void
check_file(const char *image, const char *path, size_t size)
{
	struct nextufs_image img;
	struct nextufs_node node;
	uint8_t *data = malloc(size);
	size_t got;

	if (data == NULL)
		fail("allocate readback buffer failed");
	check_result(nextufs_image_open(&img, image), "open for readback");
	check_result(nextufs_node_lookup(&img, path, 1, &node), path);
	if (node.inode.size != size)
		fail("fragment test file size mismatch");
	check_result(nextufs_path_read(&img, path, 0, data, size, &got), path);
	if (got != size)
		fail("short fragment test readback");
	for (size_t i = 0; i < size; i++) {
		if (data[i] != i % 251)
			fail("fragment test file contents mismatch");
	}
	nextufs_image_close(&img);
	free(data);
}

static void
put_file(const char *image, const char *path, size_t size)
{
	uint8_t *data = malloc(size);

	if (data == NULL)
		fail("allocate test payload failed");
	for (size_t i = 0; i < size; i++)
		data[i] = i % 251;
	check_result(nextufs_path_create_file(&write_ctx, image, path, data, size), path);
	free(data);
	check_file(image, path, size);
}

static void
test_fragments(const char *scenario, const char *image)
{
	struct nextufs_superblock before = read_superblock(image);
	struct nextufs_superblock after;
	char path[32];
	unsigned i;

	if (strcmp(scenario, "pack") == 0) {
		for (i = 0; i < 24; i++) {
			snprintf(path, sizeof(path), "/tiny%u", i);
			put_file(image, path, 1);
		}
		after = read_superblock(image);
		if (before.free_block_count - after.free_block_count > 3 ||
		    free_fragments(&before) - free_fragments(&after) != 24)
			fail("small files do not share partial blocks");
		check_fsck(image);
		for (i = 0; i < 24; i++) {
			snprintf(path, sizeof(path), "/tiny%u", i);
			check_file(image, path, 1);
			check_result(nextufs_path_unlink(&write_ctx, image, path), path);
		}
		after = read_superblock(image);
		if (after.free_block_count != before.free_block_count ||
		    after.free_frag_count != before.free_frag_count ||
		    after.free_inode_count != before.free_inode_count)
			fail("unlink did not restore free-space counts");
	} else if (strcmp(scenario, "exhaust") == 0) {
		uint32_t blocks;

		put_file(image, "/seed", 1);
		before = read_superblock(image);
		blocks = before.free_block_count;
		/* The 1 MiB fixture needs at most one indirect-pointer block. */
		if (blocks == 0 || blocks > 12 + before.block_size / sizeof(uint32_t))
			fail("unexpected exhaustion fixture geometry");
		put_file(image, "/fill", (size_t)(blocks - (blocks > 12)) * before.block_size);
		before = read_superblock(image);
		if (before.free_block_count != 0 || before.free_frag_count == 0)
			fail("expected free fragments but no free whole blocks");
		put_file(image, "/tiny", 1);
		after = read_superblock(image);
		if (after.free_block_count != 0 || after.free_frag_count != before.free_frag_count - 1)
			fail("failed to reuse fragments without free whole blocks");
	} else if (strcmp(scenario, "runs") == 0) {
		for (i = 1; i < before.frags_per_block; i++) {
			snprintf(path, sizeof(path), "/run%u", i);
			put_file(image, path, (size_t)i * before.frag_size - 7);
			check_fsck(image);
		}
		for (i = 2; i < before.frags_per_block; i += 2) {
			snprintf(path, sizeof(path), "/run%u", i);
			check_result(nextufs_path_unlink(&write_ctx, image, path), path);
		}
		check_fsck(image);
		for (i = before.frags_per_block - 2; i > 0; i -= 2) {
			snprintf(path, sizeof(path), "/again%u", i);
			put_file(image, path, (size_t)i * before.frag_size - 7);
		}
		for (i = 1; i < before.frags_per_block; i += 2) {
			snprintf(path, sizeof(path), "/run%u", i);
			check_file(image, path, (size_t)i * before.frag_size - 7);
		}
	} else if (strcmp(scenario, "geometry") == 0) {
		if (before.frag_count % before.frags_per_block != 1)
			fail("expected a partial final block after growth");
		put_file(image, "/first", 1);
		put_file(image, "/second", before.frag_size + 1);
		check_result(nextufs_path_mkdir(&write_ctx, image, "/directory", 0755), "mkdir");
	} else {
		fail("unknown fragment test scenario");
	}
	check_fsck(image);
	printf("nextufs_test: fragments %s: ok\n", scenario);
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
	if (nextufs_directory_iterate_path(img, path, 1, find_name_cb, &ctx) < 0)
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
	struct nextufs_image_info image_info;
	struct nextufs_node root;
	struct nextufs_node node;
	struct stat st;
	struct statvfs stvfs;
	char linkbuf[256];
	unsigned char buf[256];
	size_t got;
	struct expect_node_ctx node_ctx;

	if (argc == 4 && strcmp(argv[1], "--fragments") == 0) {
		test_fragments(argv[2], argv[3]);
		return 0;
	}
	if (argc != 2) {
		fprintf(stderr, "usage: %s <source>\n"
		    "       %s --fragments <pack|exhaust|runs|geometry> <scratch-image>\n",
		    argv[0], argv[0]);
		return 2;
	}
	if (nextufs_image_open(&img, argv[1]) < 0)
		fail("open_image failed");
	nextufs_image_info_get(&img, &image_info);
	if (!image_info.used_disk_label)
		fail("expected disklabel-backed image info");
	if (image_info.label_version == 0 || image_info.slice_base <= 0 ||
	    image_info.slice_size <= 0)
		fail("image info incomplete");

	if (nextufs_node_get_root(&img, &root) < 0)
		fail("get_root failed");
	if (root.inode_no != NEXTUFS_ROOT_INODE)
		fail("root inode number mismatch");
	if ((root.inode.mode & NEXTUFS_IFMT) != NEXTUFS_IFDIR)
		fail("root is not a directory");
	if (nextufs_node_get_by_inode(&img, NEXTUFS_ROOT_INODE, &node) < 0)
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
	if (nextufs_directory_iterate_nodes_path(&img, "/", 1, find_node_cb,
	    &node_ctx) < 0)
		fail("directory node iteration failed");
	if (!node_ctx.found || !node_ctx.saw_regular)
		fail("directory node iteration missing mach_kernel regular file");

	if (nextufs_node_lookup(&img, "/etc", 0, &node) < 0)
		fail("lookup nofollow /etc failed");
	if ((node.inode.mode & NEXTUFS_IFMT) != NEXTUFS_IFLNK)
		fail("/etc nofollow is not a symlink");
	if (!nextufs_node_is_lnk(&node))
		fail("/etc symlink helper mismatch");
	if (nextufs_path_readlink(&img, "/etc", linkbuf, sizeof(linkbuf)) < 0)
		fail("readlink /etc failed");
	if (strcmp(linkbuf, "private/etc") != 0)
		fail("/etc symlink target mismatch");

	if (nextufs_node_lookup(&img, "/etc", 1, &node) < 0)
		fail("lookup follow /etc failed");
	if ((node.inode.mode & NEXTUFS_IFMT) != NEXTUFS_IFDIR)
		fail("/etc follow is not a directory");

	if (nextufs_node_lookup(&img, "/etc/passwd", 1, &node) < 0)
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

	if (nextufs_fs_statvfs(&img, &stvfs) < 0)
		fail("statvfs failed");
	if (stvfs.f_bsize == 0 || stvfs.f_frsize == 0 || stvfs.f_blocks == 0)
		fail("statvfs geometry invalid");
	if (stvfs.f_files == 0 || stvfs.f_ffree == 0)
		fail("statvfs inode counts invalid");
	if (nextufs_node_check_access(&node, node.inode.uid, node.inode.gid, R_OK) < 0)
		fail("access read check failed");
	if (nextufs_node_check_access(&node, node.inode.uid, node.inode.gid, W_OK) != -EROFS)
		fail("access write check failed");

	got = 0;
	if (nextufs_path_read(&img, "/etc/passwd", 0, buf, sizeof(buf), &got) < 0)
		fail("read_path /etc/passwd failed");
	if (got < 32)
		fail("/etc/passwd preview too short");
	if (memcmp(buf, "#\n# You probably", 16) != 0)
		fail("/etc/passwd contents mismatch");

	got = 0;
	if (nextufs_path_read(&img, "/mach_kernel", 0x18000, buf, sizeof(buf),
	    &got) < 0)
		fail("read_path /mach_kernel failed");
	if (got == 0)
		fail("/mach_kernel indirect read returned no data");

	nextufs_image_close(&img);
	printf("nextufs_test: ok\n");
	return 0;
}
