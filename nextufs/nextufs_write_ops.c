#include "nextufs_write.h"
#include "nextufs_write_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int
nextufs_w_open_image_rw(const char *image_path, struct nextufs_image *img)
{
	int fd;
	int rc;

	rc = nextufs_open_image(img, image_path);
	if (rc < 0)
		return rc;
	close(img->fd);
	fd = open(image_path, O_RDWR);
	if (fd < 0) {
		nextufs_close_image(img);
		return -errno;
	}
	img->fd = fd;
	return 0;
}

static int
nextufs_w_ctx_is_root(const struct nextufs_write_ctx *ctx)
{
	return ctx->uid == 0;
}

static int
nextufs_w_id_fits_nextstep(unsigned long id)
{
	return id <= (unsigned long)INT16_MAX;
}

static int
nextufs_w_require_ctx_ids_fit(const struct nextufs_write_ctx *ctx)
{
	if (ctx->policy != NEXTUFS_WRITE_PERMISSIONS)
		return 0;
	if (!nextufs_w_id_fits_nextstep((unsigned long)ctx->uid) ||
	    !nextufs_w_id_fits_nextstep((unsigned long)ctx->gid))
		return -EOVERFLOW;
	return 0;
}

static int
nextufs_w_require_owner_ids_fit(uid_t uid, gid_t gid)
{
	if (uid != (uid_t)-1 && !nextufs_w_id_fits_nextstep((unsigned long)uid))
		return -EOVERFLOW;
	if (gid != (gid_t)-1 && !nextufs_w_id_fits_nextstep((unsigned long)gid))
		return -EOVERFLOW;
	return 0;
}

static int
nextufs_w_ctx_in_group(const struct nextufs_write_ctx *ctx, gid_t gid)
{
	size_t i;

	if (ctx->gid == gid)
		return 1;
	for (i = 0; i < ctx->group_count; i++) {
		if (ctx->groups[i] == gid)
			return 1;
	}
	return 0;
}

static mode_t
nextufs_w_ctx_perms_for_node(const struct nextufs_write_ctx *ctx,
    const struct nextufs_node *node)
{
	if (ctx->uid == node->inode.uid)
		return (node->inode.mode >> 6) & 07;
	if (nextufs_w_ctx_in_group(ctx, node->inode.gid))
		return (node->inode.mode >> 3) & 07;
	return node->inode.mode & 07;
}

static int
nextufs_w_require_permissions(const struct nextufs_write_ctx *ctx,
    const struct nextufs_node *node, int mask)
{
	mode_t bits;
	mode_t perms;

	if (ctx->policy != NEXTUFS_WRITE_PERMISSIONS)
		return 0;
	if (mask == F_OK)
		return 0;
	if (nextufs_w_ctx_is_root(ctx)) {
		if ((mask & X_OK) != 0 && (node->inode.mode & 0111) == 0)
			return -EACCES;
		return 0;
	}
	perms = nextufs_w_ctx_perms_for_node(ctx, node);
	bits = 0;
	if ((mask & R_OK) != 0)
		bits |= 04;
	if ((mask & W_OK) != 0)
		bits |= 02;
	if ((mask & X_OK) != 0)
		bits |= 01;
	return (perms & bits) == bits ? 0 : -EACCES;
}

static int
nextufs_w_require_parent_mutation(const struct nextufs_write_ctx *ctx,
    const struct nextufs_node *parent)
{
	return nextufs_w_require_permissions(ctx, parent, W_OK | X_OK);
}

static int
nextufs_w_check_sticky_parent(const struct nextufs_write_ctx *ctx,
    const struct nextufs_node *parent, const struct nextufs_node *target)
{
	if (ctx->policy != NEXTUFS_WRITE_PERMISSIONS)
		return 0;
	if ((parent->inode.mode & 01000) == 0)
		return 0;
	if (nextufs_w_ctx_is_root(ctx))
		return 0;
	if (ctx->uid == parent->inode.uid || ctx->uid == target->inode.uid)
		return 0;
	return -EPERM;
}

static void
nextufs_w_set_new_inode_owner(const struct nextufs_write_ctx *ctx,
    const struct nextufs_node *parent, struct nextufs_inode *ino)
{
	if (ctx->policy == NEXTUFS_WRITE_PERMISSIONS) {
		ino->uid = (uint16_t)ctx->uid;
		ino->gid = (uint16_t)ctx->gid;
	} else {
		ino->uid = 0;
		ino->gid = parent->inode.gid;
	}
}

static int
nextufs_w_require_chmod_allowed(const struct nextufs_write_ctx *ctx,
    const struct nextufs_node *node)
{
	if (ctx->policy != NEXTUFS_WRITE_PERMISSIONS)
		return 0;
	if (nextufs_w_ctx_is_root(ctx) || ctx->uid == node->inode.uid)
		return 0;
	return -EPERM;
}

static int
nextufs_w_require_chown_allowed(const struct nextufs_write_ctx *ctx,
    const struct nextufs_node *node, uid_t uid, gid_t gid)
{
	if (ctx->policy != NEXTUFS_WRITE_PERMISSIONS)
		return 0;
	if (nextufs_w_ctx_is_root(ctx))
		return 0;
	if (ctx->uid != node->inode.uid)
		return -EPERM;
	if (uid != node->inode.uid)
		return -EPERM;
	return nextufs_w_ctx_in_group(ctx, gid) ? 0 : -EPERM;
}

static void
nextufs_w_sanitize_new_inode_mode(const struct nextufs_write_ctx *ctx,
    struct nextufs_inode *ino)
{
	uint16_t ifmt;

	if (ctx->policy != NEXTUFS_WRITE_PERMISSIONS || nextufs_w_ctx_is_root(ctx))
		return;
	ifmt = ino->mode & NEXTUFS_IFMT;
	if (ifmt != NEXTUFS_IFDIR)
		ino->mode &= ~01000;
	if ((ino->mode & 02000) != 0 && !nextufs_w_ctx_in_group(ctx, ino->gid))
		ino->mode &= ~02000;
}

static uint16_t
nextufs_w_sanitize_chmod_mode(const struct nextufs_write_ctx *ctx,
    const struct nextufs_node *node, uint16_t mode)
{
	uint16_t new_mode;
	uint16_t ifmt;

	new_mode = (node->inode.mode & NEXTUFS_IFMT) | (mode & 07777);
	if (ctx->policy != NEXTUFS_WRITE_PERMISSIONS || nextufs_w_ctx_is_root(ctx))
		return new_mode;
	ifmt = new_mode & NEXTUFS_IFMT;
	if (ifmt != NEXTUFS_IFDIR)
		new_mode &= ~01000;
	if ((new_mode & 02000) != 0 && !nextufs_w_ctx_in_group(ctx, node->inode.gid))
		new_mode &= ~02000;
	return new_mode;
}

static int
nextufs_w_remove_linked_inode(const struct nextufs_image *img,
    struct nextufs_node *target)
{
	struct nextufs_inode cleared;
	int rc;

	if (target->inode.nlink == 0)
		return -EINVAL;
	target->inode.nlink--;
	if (target->inode.nlink == 0) {
		if ((target->inode.mode & NEXTUFS_IFMT) != NEXTUFS_IFIFO &&
		    (target->inode.mode & NEXTUFS_IFMT) != NEXTUFS_IFCHR &&
		    (target->inode.mode & NEXTUFS_IFMT) != NEXTUFS_IFBLK &&
		    (target->inode.mode & NEXTUFS_IFMT) != NEXTUFS_IFSOCK) {
			rc = nextufs_w_free_regular_file_storage(img, &target->inode);
			if (rc < 0)
				return rc;
		}
		memset(&cleared, 0, sizeof(cleared));
		rc = nextufs_w_write_inode_raw(img, target->inode_no, &cleared);
		if (rc < 0)
			return rc;
		return nextufs_w_free_inode_in_cg(img, target->inode_no,
		    target->inode.mode);
	}
	target->inode.ctime = (uint32_t)time(NULL);
	return nextufs_w_write_inode_raw(img, target->inode_no, &target->inode);
}

static int
nextufs_w_remove_dir_inode(const struct nextufs_image *img,
    struct nextufs_node *parent, struct nextufs_node *target)
{
	struct nextufs_inode cleared;
	uint32_t now;
	int rc;

	rc = nextufs_w_free_regular_file_storage(img, &target->inode);
	if (rc < 0)
		return rc;
	memset(&cleared, 0, sizeof(cleared));
	rc = nextufs_w_write_inode_raw(img, target->inode_no, &cleared);
	if (rc < 0)
		return rc;
	rc = nextufs_w_free_inode_in_cg(img, target->inode_no, target->inode.mode);
	if (rc < 0)
		return rc;
	parent->inode.nlink--;
	now = (uint32_t)time(NULL);
	parent->inode.ctime = now;
	parent->inode.mtime = now;
	return nextufs_w_write_inode_raw(img, parent->inode_no, &parent->inode);
}

static int
nextufs_w_rewrite_inode_contents(const struct nextufs_image *img,
    unsigned inode_no, struct nextufs_inode *inode, unsigned preferred_cg,
    const uint8_t *data, size_t data_len)
{
	struct nextufs_inode old_inode;
	uint32_t now;
	int rc;

	old_inode = *inode;
	memset(inode->db, 0, sizeof(inode->db));
	memset(inode->ib, 0, sizeof(inode->ib));
	inode->size = 0;
	inode->blocks = 0;
	rc = nextufs_w_allocate_data_for_inode(img, preferred_cg, data, data_len, inode);
	if (rc < 0)
		return rc;
	now = (uint32_t)time(NULL);
	inode->mtime = now;
	inode->ctime = now;
	inode->atime = now;
	rc = nextufs_w_write_inode_raw(img, inode_no, inode);
	if (rc < 0)
		return rc;
	return nextufs_w_free_regular_file_storage(img, &old_inode);
}

static int
nextufs_w_directory_has_ancestor(const struct nextufs_image *img,
    const struct nextufs_node *dirnode, unsigned ancestor_inode)
{
	struct nextufs_node cur;
	unsigned parent_inode;
	int rc;

	cur = *dirnode;
	for (;;) {
		if (cur.inode_no == ancestor_inode)
			return 1;
		if (cur.inode_no == NEXTUFS_ROOT_INODE)
			return 0;
		rc = nextufs_w_read_directory_parent_inode(img, &cur, &parent_inode);
		if (rc < 0 || parent_inode == cur.inode_no)
			return 0;
		rc = nextufs_get_node_by_inode(img, parent_inode, &cur);
		if (rc < 0)
			return 0;
	}
}

static int
nextufs_w_name_is_dot_or_dotdot(const char *name)
{
	return (name[0] == '.' && name[1] == '\0') ||
	    (name[0] == '.' && name[1] == '.' && name[2] == '\0');
}

static int
nextufs_w_disallow_dot_names(const char *name)
{
	return nextufs_w_name_is_dot_or_dotdot(name) ? -EINVAL : 0;
}

static int
nextufs_w_disallow_target_dot_names(const char *name)
{
	return nextufs_w_name_is_dot_or_dotdot(name) ? -ENOTEMPTY : 0;
}

static void
nextufs_w_clear_inode_layout(struct nextufs_inode *ino)
{
	memset(ino->db, 0, sizeof(ino->db));
	memset(ino->ib, 0, sizeof(ino->ib));
	ino->size = 0;
	ino->blocks = 0;
}

static int
nextufs_w_discard_new_inode(const struct nextufs_image *img, unsigned inode_no,
    struct nextufs_inode *ino)
{
	struct nextufs_inode cleared;
	int rc;

	rc = nextufs_w_free_regular_file_storage(img, ino);
	if (rc < 0)
		return rc;
	memset(&cleared, 0, sizeof(cleared));
	rc = nextufs_w_write_inode_raw(img, inode_no, &cleared);
	if (rc < 0)
		return rc;
	rc = nextufs_w_free_inode_in_cg(img, inode_no, ino->mode);
	if (rc < 0)
		return rc;
	nextufs_w_clear_inode_layout(ino);
	return 0;
}

int
nextufs_create_small_file(const struct nextufs_write_ctx *ctx,
    const char *image_path, const char *path,
    const void *data, size_t data_len)
{
	struct nextufs_image img;
	struct nextufs_node parent;
	struct nextufs_node existing;
	struct nextufs_inode ino;
	char parent_path[4096];
	char name[256];
	unsigned new_inode_no;
	unsigned parent_cg;
	int rc;
	uint32_t now;

	if (ctx == NULL)
		return -EINVAL;
	rc = nextufs_w_require_ctx_ids_fit(ctx);
	if (rc < 0)
		return rc;
	rc = nextufs_w_path_dirname_basename(path, parent_path, sizeof(parent_path),
	    name, sizeof(name));
	if (rc < 0)
		return rc;
	rc = nextufs_w_disallow_dot_names(name);
	if (rc < 0)
		return rc;
	rc = nextufs_w_open_image_rw(image_path, &img);
	if (rc < 0)
		return rc;
	rc = nextufs_lookup(&img, parent_path, 1, &parent);
	if (rc < 0)
		goto out;
	if (!nextufs_node_is_dir(&parent)) {
		rc = -ENOTDIR;
		goto out;
	}
	rc = nextufs_w_require_parent_mutation(ctx, &parent);
	if (rc < 0)
		goto out;
	rc = nextufs_lookup(&img, path, 0, &existing);
	if (rc == 0) {
		rc = -EEXIST;
		goto out;
	}
	if (rc != -ENOENT)
		goto out;
	parent_cg = parent.inode_no / img.sb.inodes_per_group;
	rc = nextufs_w_allocate_inode_in_cg(&img, parent_cg, NEXTUFS_IFREG | 0644,
	    &new_inode_no);
	if (rc < 0)
		goto out;
	memset(&ino, 0, sizeof(ino));
	now = (uint32_t)time(NULL);
	ino.mode = NEXTUFS_IFREG | 0644;
	ino.nlink = 1;
	nextufs_w_set_new_inode_owner(ctx, &parent, &ino);
	nextufs_w_sanitize_new_inode_mode(ctx, &ino);
	ino.size = data_len;
	ino.atime = now;
	ino.mtime = now;
	ino.ctime = now;
	rc = nextufs_w_allocate_data_for_inode(&img, parent_cg, data, data_len, &ino);
	if (rc < 0) {
		(void)nextufs_w_discard_new_inode(&img, new_inode_no, &ino);
		goto out;
	}
	rc = nextufs_w_write_inode_raw(&img, new_inode_no, &ino);
	if (rc < 0) {
		(void)nextufs_w_discard_new_inode(&img, new_inode_no, &ino);
		goto out;
	}
	rc = nextufs_w_insert_dirent_existing_space(&img, &parent, name, new_inode_no);
	if (rc < 0) {
		(void)nextufs_w_discard_new_inode(&img, new_inode_no, &ino);
		goto out;
	}
	rc = fsync(img.fd);
out:
	nextufs_close_image(&img);
	return rc < 0 ? rc : 0;
}

int
nextufs_create_file_from_hostfile(const struct nextufs_write_ctx *ctx,
    const char *image_path, const char *path,
    const char *host_file)
{
	FILE *fp;
	long file_size;
	uint8_t *buf;
	size_t data_len;
	size_t nread;
	int rc;

	fp = fopen(host_file, "rb");
	if (fp == NULL)
		return -errno;
	if (fseek(fp, 0, SEEK_END) != 0) {
		rc = -errno;
		fclose(fp);
		return rc;
	}
	file_size = ftell(fp);
	if (file_size < 0) {
		rc = -errno;
		fclose(fp);
		return rc;
	}
	if (fseek(fp, 0, SEEK_SET) != 0) {
		rc = -errno;
		fclose(fp);
		return rc;
	}
	data_len = (size_t)file_size;
	buf = malloc(data_len == 0 ? 1 : data_len);
	if (buf == NULL) {
		fclose(fp);
		return -ENOMEM;
	}
	nread = fread(buf, 1, data_len, fp);
	if (nread != data_len) {
		rc = ferror(fp) ? -EIO : -EINVAL;
		fclose(fp);
		free(buf);
		return rc;
	}
	fclose(fp);
	rc = nextufs_create_small_file(ctx, image_path, path, buf, data_len);
	free(buf);
	return rc;
}

int
nextufs_unlink_path(const struct nextufs_write_ctx *ctx,
    const char *image_path, const char *path)
{
	struct nextufs_image img;
	struct nextufs_node parent;
	struct nextufs_node target;
	char parent_path[4096];
	char name[256];
	unsigned removed_inode;
	int rc;

	if (ctx == NULL)
		return -EINVAL;
	rc = nextufs_w_require_ctx_ids_fit(ctx);
	if (rc < 0)
		return rc;
	rc = nextufs_w_path_dirname_basename(path, parent_path, sizeof(parent_path),
	    name, sizeof(name));
	if (rc < 0)
		return rc;
	rc = nextufs_w_disallow_dot_names(name);
	if (rc < 0)
		return rc;
	rc = nextufs_w_open_image_rw(image_path, &img);
	if (rc < 0)
		return rc;
	rc = nextufs_lookup(&img, parent_path, 1, &parent);
	if (rc < 0)
		goto out;
	if (!nextufs_node_is_dir(&parent)) {
		rc = -ENOTDIR;
		goto out;
	}
	rc = nextufs_w_require_parent_mutation(ctx, &parent);
	if (rc < 0)
		goto out;
	rc = nextufs_lookup(&img, path, 0, &target);
	if (rc < 0)
		goto out;
	if (nextufs_node_is_dir(&target)) {
		rc = -EISDIR;
		goto out;
	}
	rc = nextufs_w_check_sticky_parent(ctx, &parent, &target);
	if (rc < 0)
		goto out;
	rc = nextufs_w_require_permissions(ctx, &target, W_OK);
	if (rc < 0)
		goto out;
	rc = nextufs_w_remove_dirent(&img, &parent, name, &removed_inode);
	if (rc < 0)
		goto out;
	if (removed_inode != target.inode_no) {
		rc = -EINVAL;
		goto out;
	}
	rc = nextufs_w_remove_linked_inode(&img, &target);
	if (rc < 0)
		goto out;
	rc = fsync(img.fd);
out:
	nextufs_close_image(&img);
	return rc < 0 ? rc : 0;
}

int
nextufs_mkdir_path(const struct nextufs_write_ctx *ctx,
    const char *image_path, const char *path, uint16_t mode)
{
	struct nextufs_image img;
	struct nextufs_node parent;
	struct nextufs_node existing;
	struct nextufs_inode ino;
	char parent_path[4096];
	char name[256];
	unsigned new_inode_no;
	unsigned parent_cg;
	uint32_t alloc_frag;
	int rc;
	uint32_t now;

	if (ctx == NULL)
		return -EINVAL;
	rc = nextufs_w_require_ctx_ids_fit(ctx);
	if (rc < 0)
		return rc;
	rc = nextufs_w_path_dirname_basename(path, parent_path, sizeof(parent_path),
	    name, sizeof(name));
	if (rc < 0)
		return rc;
	rc = nextufs_w_disallow_dot_names(name);
	if (rc < 0)
		return rc;
	rc = nextufs_w_open_image_rw(image_path, &img);
	if (rc < 0)
		return rc;
	rc = nextufs_lookup(&img, parent_path, 1, &parent);
	if (rc < 0)
		goto out;
	if (!nextufs_node_is_dir(&parent)) {
		rc = -ENOTDIR;
		goto out;
	}
	rc = nextufs_w_require_parent_mutation(ctx, &parent);
	if (rc < 0)
		goto out;
	rc = nextufs_lookup(&img, path, 0, &existing);
	if (rc == 0) {
		rc = -EEXIST;
		goto out;
	}
	if (rc != -ENOENT)
		goto out;
	parent_cg = parent.inode_no / img.sb.inodes_per_group;
	rc = nextufs_w_allocate_inode_in_cg(&img, parent_cg, NEXTUFS_IFDIR | (mode & 0777),
	    &new_inode_no);
	if (rc < 0)
		goto out;
	rc = nextufs_w_allocate_frags_anycg(&img, parent_cg, 1, &alloc_frag);
	if (rc < 0)
		goto out;
	rc = nextufs_w_write_new_directory_block(&img, alloc_frag, new_inode_no,
	    parent.inode_no);
	if (rc < 0) {
		(void)nextufs_w_free_fragment_run(&img, alloc_frag, 1);
		(void)nextufs_w_free_inode_in_cg(&img, new_inode_no,
		    NEXTUFS_IFDIR | (mode & 0777));
		goto out;
	}
	memset(&ino, 0, sizeof(ino));
	now = (uint32_t)time(NULL);
	ino.mode = NEXTUFS_IFDIR | (mode & 0777);
	ino.nlink = 2;
	nextufs_w_set_new_inode_owner(ctx, &parent, &ino);
	nextufs_w_sanitize_new_inode_mode(ctx, &ino);
	ino.size = DIRBLKSIZ;
	ino.atime = now;
	ino.mtime = now;
	ino.ctime = now;
	ino.db[0] = alloc_frag;
	ino.blocks = DIRBLKSIZ / DEV_BSIZE;
	rc = nextufs_w_write_inode_raw(&img, new_inode_no, &ino);
	if (rc < 0) {
		(void)nextufs_w_discard_new_inode(&img, new_inode_no, &ino);
		goto out;
	}
	rc = nextufs_w_insert_dirent_existing_space(&img, &parent, name, new_inode_no);
	if (rc < 0) {
		(void)nextufs_w_discard_new_inode(&img, new_inode_no, &ino);
		goto out;
	}
	parent.inode.nlink++;
	parent.inode.ctime = now;
	parent.inode.mtime = now;
	rc = nextufs_w_write_inode_raw(&img, parent.inode_no, &parent.inode);
	if (rc < 0)
		goto out;
	rc = fsync(img.fd);
out:
	nextufs_close_image(&img);
	return rc < 0 ? rc : 0;
}

static int
nextufs_w_rewrite_file_contents(const struct nextufs_write_ctx *ctx,
    const char *image_path, const char *path,
    const void *data, size_t data_len, int append)
{
	struct nextufs_image img;
	struct nextufs_node target;
	uint8_t *buf = NULL;
	size_t old_size;
	size_t final_size;
	unsigned preferred_cg;
	int rc;

	if (ctx == NULL)
		return -EINVAL;
	rc = nextufs_w_require_ctx_ids_fit(ctx);
	if (rc < 0)
		return rc;
	rc = nextufs_w_open_image_rw(image_path, &img);
	if (rc < 0)
		return rc;
	rc = nextufs_lookup(&img, path, 0, &target);
	if (rc < 0)
		goto out;
	if (!nextufs_node_is_reg(&target)) {
		rc = -EISDIR;
		goto out;
	}
	rc = nextufs_w_require_permissions(ctx, &target, W_OK);
	if (rc < 0)
		goto out;
	old_size = (size_t)target.inode.size;
	final_size = append ? old_size + data_len : data_len;
	buf = malloc(final_size == 0 ? 1 : final_size);
	if (buf == NULL) {
		rc = -ENOMEM;
		goto out;
	}
	if (append && old_size != 0) {
		rc = nextufs_read_inode_data(&img, &target.inode, 0, buf, old_size, NULL);
		if (rc < 0)
			goto out;
		memcpy(buf + old_size, data, data_len);
	} else if (final_size != 0) {
		memcpy(buf, data, data_len);
	}
	preferred_cg = target.inode_no / img.sb.inodes_per_group;
	rc = nextufs_w_rewrite_inode_contents(&img, target.inode_no, &target.inode,
	    preferred_cg, buf, final_size);
	if (rc < 0)
		goto out;
	rc = fsync(img.fd);
out:
	free(buf);
	nextufs_close_image(&img);
	return rc < 0 ? rc : 0;
}

int
nextufs_overwrite_file(const struct nextufs_write_ctx *ctx,
    const char *image_path, const char *path,
    const void *data, size_t data_len)
{
	return nextufs_w_rewrite_file_contents(ctx, image_path, path, data,
	    data_len, 0);
}

int
nextufs_append_file(const struct nextufs_write_ctx *ctx,
    const char *image_path, const char *path,
    const void *data, size_t data_len)
{
	return nextufs_w_rewrite_file_contents(ctx, image_path, path, data,
	    data_len, 1);
}

int
nextufs_rmdir_path(const struct nextufs_write_ctx *ctx,
    const char *image_path, const char *path)
{
	struct nextufs_image img;
	struct nextufs_node parent;
	struct nextufs_node target;
	char parent_path[4096];
	char name[256];
	unsigned removed_inode;
	int rc;

	if (ctx == NULL)
		return -EINVAL;
	rc = nextufs_w_require_ctx_ids_fit(ctx);
	if (rc < 0)
		return rc;
	rc = nextufs_w_path_dirname_basename(path, parent_path, sizeof(parent_path),
	    name, sizeof(name));
	if (rc < 0)
		return rc;
	rc = nextufs_w_open_image_rw(image_path, &img);
	if (rc < 0)
		return rc;
	rc = nextufs_lookup(&img, parent_path, 1, &parent);
	if (rc < 0)
		goto out;
	rc = nextufs_w_require_parent_mutation(ctx, &parent);
	if (rc < 0)
		goto out;
	rc = nextufs_lookup(&img, path, 0, &target);
	if (rc < 0)
		goto out;
	if (!nextufs_node_is_dir(&target)) {
		rc = -ENOTDIR;
		goto out;
	}
	rc = nextufs_w_check_sticky_parent(ctx, &parent, &target);
	if (rc < 0)
		goto out;
	if (target.inode.nlink != 2 ||
	    !nextufs_w_directory_is_empty(&img, &target, parent.inode_no)) {
		rc = -ENOTEMPTY;
		goto out;
	}
	rc = nextufs_w_remove_dirent(&img, &parent, name, &removed_inode);
	if (rc < 0)
		goto out;
	if (removed_inode != target.inode_no) {
		rc = -EINVAL;
		goto out;
	}
	rc = nextufs_w_remove_dir_inode(&img, &parent, &target);
	if (rc < 0)
		goto out;
	rc = fsync(img.fd);
out:
	nextufs_close_image(&img);
	return rc < 0 ? rc : 0;
}

int
nextufs_link_path(const struct nextufs_write_ctx *ctx,
    const char *image_path, const char *source_path,
    const char *target_path)
{
	struct nextufs_image img;
	struct nextufs_node source;
	struct nextufs_node parent;
	struct nextufs_node existing;
	char parent_path[4096];
	char name[256];
	int rc;
	uint32_t now;

	if (ctx == NULL)
		return -EINVAL;
	rc = nextufs_w_require_ctx_ids_fit(ctx);
	if (rc < 0)
		return rc;
	rc = nextufs_w_path_dirname_basename(target_path, parent_path, sizeof(parent_path),
	    name, sizeof(name));
	if (rc < 0)
		return rc;
	rc = nextufs_w_disallow_dot_names(name);
	if (rc < 0)
		return rc;
	rc = nextufs_w_open_image_rw(image_path, &img);
	if (rc < 0)
		return rc;
	rc = nextufs_lookup(&img, parent_path, 1, &parent);
	if (rc < 0)
		goto out;
	rc = nextufs_w_require_parent_mutation(ctx, &parent);
	if (rc < 0)
		goto out;
	rc = nextufs_lookup(&img, source_path, 0, &source);
	if (rc < 0)
		goto out;
	if (nextufs_node_is_dir(&source)) {
		rc = -EPERM;
		goto out;
	}
	rc = nextufs_lookup(&img, target_path, 0, &existing);
	if (rc == 0) {
		rc = -EEXIST;
		goto out;
	}
	if (rc != -ENOENT)
		goto out;
	source.inode.nlink++;
	now = (uint32_t)time(NULL);
	source.inode.ctime = now;
	rc = nextufs_w_write_inode_raw(&img, source.inode_no, &source.inode);
	if (rc < 0)
		goto out;
	rc = nextufs_w_insert_dirent_existing_space(&img, &parent, name, source.inode_no);
	if (rc < 0)
		goto out;
	rc = fsync(img.fd);
out:
	nextufs_close_image(&img);
	return rc < 0 ? rc : 0;
}

int
nextufs_symlink_path(const struct nextufs_write_ctx *ctx,
    const char *image_path, const char *target,
    const char *link_path)
{
	struct nextufs_image img;
	struct nextufs_node parent;
	struct nextufs_node existing;
	struct nextufs_inode ino;
	char parent_path[4096];
	char name[256];
	unsigned new_inode_no;
	unsigned parent_cg;
	int rc;
	uint32_t now;
	size_t target_len;

	if (ctx == NULL)
		return -EINVAL;
	rc = nextufs_w_require_ctx_ids_fit(ctx);
	if (rc < 0)
		return rc;
	target_len = strlen(target);
	rc = nextufs_w_path_dirname_basename(link_path, parent_path, sizeof(parent_path),
	    name, sizeof(name));
	if (rc < 0)
		return rc;
	rc = nextufs_w_disallow_dot_names(name);
	if (rc < 0)
		return rc;
	rc = nextufs_w_open_image_rw(image_path, &img);
	if (rc < 0)
		return rc;
	rc = nextufs_lookup(&img, parent_path, 1, &parent);
	if (rc < 0)
		goto out;
	rc = nextufs_w_require_parent_mutation(ctx, &parent);
	if (rc < 0)
		goto out;
	rc = nextufs_lookup(&img, link_path, 0, &existing);
	if (rc == 0) {
		rc = -EEXIST;
		goto out;
	}
	if (rc != -ENOENT)
		goto out;
	parent_cg = parent.inode_no / img.sb.inodes_per_group;
	rc = nextufs_w_allocate_inode_in_cg(&img, parent_cg, NEXTUFS_IFLNK | 0777,
	    &new_inode_no);
	if (rc < 0)
		goto out;
	memset(&ino, 0, sizeof(ino));
	now = (uint32_t)time(NULL);
	ino.mode = NEXTUFS_IFLNK | 0777;
	ino.nlink = 1;
	nextufs_w_set_new_inode_owner(ctx, &parent, &ino);
	nextufs_w_sanitize_new_inode_mode(ctx, &ino);
	ino.size = target_len;
	ino.atime = now;
	ino.mtime = now;
	ino.ctime = now;
	if (target_len <= 60) {
		ino.flags = 0x0001U;
		nextufs_w_store_inline_symlink_bytes(&ino, target, target_len);
	} else {
		rc = nextufs_w_allocate_data_for_inode(&img, parent_cg,
		    (const uint8_t *)target, target_len, &ino);
		if (rc < 0) {
			(void)nextufs_w_discard_new_inode(&img, new_inode_no, &ino);
			goto out;
		}
	}
	rc = nextufs_w_write_inode_raw(&img, new_inode_no, &ino);
	if (rc < 0) {
		(void)nextufs_w_discard_new_inode(&img, new_inode_no, &ino);
		goto out;
	}
	rc = nextufs_w_insert_dirent_existing_space(&img, &parent, name, new_inode_no);
	if (rc < 0) {
		(void)nextufs_w_discard_new_inode(&img, new_inode_no, &ino);
		goto out;
	}
	rc = fsync(img.fd);
out:
	nextufs_close_image(&img);
	return rc < 0 ? rc : 0;
}

int
nextufs_truncate_path(const struct nextufs_write_ctx *ctx,
    const char *image_path, const char *path, uint64_t size)
{
	struct nextufs_image img;
	struct nextufs_node target;
	uint8_t *buf = NULL;
	size_t old_size;
	size_t new_size;
	size_t copy_size;
	unsigned preferred_cg;
	int rc;

	if (ctx == NULL)
		return -EINVAL;
	if (size > SIZE_MAX)
		return -EFBIG;
	rc = nextufs_w_require_ctx_ids_fit(ctx);
	if (rc < 0)
		return rc;
	rc = nextufs_w_open_image_rw(image_path, &img);
	if (rc < 0)
		return rc;
	rc = nextufs_lookup(&img, path, 0, &target);
	if (rc < 0)
		goto out;
	if (!nextufs_node_is_reg(&target)) {
		rc = -EINVAL;
		goto out;
	}
	rc = nextufs_w_require_permissions(ctx, &target, W_OK);
	if (rc < 0)
		goto out;
	old_size = (size_t)target.inode.size;
	new_size = (size_t)size;
	buf = calloc(1, new_size == 0 ? 1 : new_size);
	if (buf == NULL) {
		rc = -ENOMEM;
		goto out;
	}
	copy_size = old_size < new_size ? old_size : new_size;
	if (copy_size != 0) {
		rc = nextufs_read_inode_data(&img, &target.inode, 0, buf, copy_size, NULL);
		if (rc < 0)
			goto out;
	}
	preferred_cg = target.inode_no / img.sb.inodes_per_group;
	rc = nextufs_w_rewrite_inode_contents(&img, target.inode_no, &target.inode,
	    preferred_cg, buf, new_size);
	if (rc < 0)
		goto out;
	rc = fsync(img.fd);
out:
	free(buf);
	nextufs_close_image(&img);
	return rc < 0 ? rc : 0;
}

int
nextufs_pwrite_path(const struct nextufs_write_ctx *ctx,
    const char *image_path, const char *path,
    const void *data, size_t data_len, uint64_t offset)
{
	struct nextufs_image img;
	struct nextufs_node target;
	uint8_t *buf = NULL;
	size_t old_size;
	size_t final_size;
	unsigned preferred_cg;
	int rc;

	if (ctx == NULL)
		return -EINVAL;
	if (offset > SIZE_MAX || data_len > SIZE_MAX - (size_t)offset)
		return -EFBIG;
	rc = nextufs_w_require_ctx_ids_fit(ctx);
	if (rc < 0)
		return rc;
	rc = nextufs_w_open_image_rw(image_path, &img);
	if (rc < 0)
		return rc;
	rc = nextufs_lookup(&img, path, 0, &target);
	if (rc < 0)
		goto out;
	if (!nextufs_node_is_reg(&target)) {
		rc = -EINVAL;
		goto out;
	}
	rc = nextufs_w_require_permissions(ctx, &target, W_OK);
	if (rc < 0)
		goto out;
	old_size = (size_t)target.inode.size;
	final_size = old_size;
	if ((size_t)offset + data_len > final_size)
		final_size = (size_t)offset + data_len;
	buf = calloc(1, final_size == 0 ? 1 : final_size);
	if (buf == NULL) {
		rc = -ENOMEM;
		goto out;
	}
	if (old_size != 0) {
		rc = nextufs_read_inode_data(&img, &target.inode, 0, buf, old_size, NULL);
		if (rc < 0)
			goto out;
	}
	if (data_len != 0)
		memcpy(buf + (size_t)offset, data, data_len);
	preferred_cg = target.inode_no / img.sb.inodes_per_group;
	rc = nextufs_w_rewrite_inode_contents(&img, target.inode_no, &target.inode,
	    preferred_cg, buf, final_size);
	if (rc < 0)
		goto out;
	rc = fsync(img.fd);
out:
	free(buf);
	nextufs_close_image(&img);
	return rc < 0 ? rc : 0;
}

int
nextufs_mknod_path(const struct nextufs_write_ctx *ctx,
    const char *image_path, const char *path,
    uint16_t mode, uint32_t rdev)
{
	struct nextufs_image img;
	struct nextufs_node parent;
	struct nextufs_node existing;
	struct nextufs_inode ino;
	char parent_path[4096];
	char name[256];
	unsigned new_inode_no;
	unsigned parent_cg;
	uint16_t ifmt;
	uint32_t now;
	int rc;

	if (ctx == NULL)
		return -EINVAL;
	ifmt = mode & NEXTUFS_IFMT;
	if (ifmt != NEXTUFS_IFREG && ifmt != NEXTUFS_IFIFO &&
	    ifmt != NEXTUFS_IFCHR && ifmt != NEXTUFS_IFBLK &&
	    ifmt != NEXTUFS_IFSOCK)
		return -EINVAL;
	rc = nextufs_w_require_ctx_ids_fit(ctx);
	if (rc < 0)
		return rc;
	rc = nextufs_w_path_dirname_basename(path, parent_path, sizeof(parent_path),
	    name, sizeof(name));
	if (rc < 0)
		return rc;
	rc = nextufs_w_disallow_dot_names(name);
	if (rc < 0)
		return rc;
	rc = nextufs_w_open_image_rw(image_path, &img);
	if (rc < 0)
		return rc;
	rc = nextufs_lookup(&img, parent_path, 1, &parent);
	if (rc < 0)
		goto out;
	if (!nextufs_node_is_dir(&parent)) {
		rc = -ENOTDIR;
		goto out;
	}
	rc = nextufs_w_require_parent_mutation(ctx, &parent);
	if (rc < 0)
		goto out;
	rc = nextufs_lookup(&img, path, 0, &existing);
	if (rc == 0) {
		rc = -EEXIST;
		goto out;
	}
	if (rc != -ENOENT)
		goto out;
	parent_cg = parent.inode_no / img.sb.inodes_per_group;
	rc = nextufs_w_allocate_inode_in_cg(&img, parent_cg, mode, &new_inode_no);
	if (rc < 0)
		goto out;
	memset(&ino, 0, sizeof(ino));
	now = (uint32_t)time(NULL);
	ino.mode = mode;
	ino.nlink = 1;
	nextufs_w_set_new_inode_owner(ctx, &parent, &ino);
	nextufs_w_sanitize_new_inode_mode(ctx, &ino);
	ino.atime = now;
	ino.mtime = now;
	ino.ctime = now;
	if (ifmt == NEXTUFS_IFCHR || ifmt == NEXTUFS_IFBLK)
		ino.db[0] = rdev;
	rc = nextufs_w_write_inode_raw(&img, new_inode_no, &ino);
	if (rc < 0) {
		(void)nextufs_w_discard_new_inode(&img, new_inode_no, &ino);
		goto out;
	}
	rc = nextufs_w_insert_dirent_existing_space(&img, &parent, name, new_inode_no);
	if (rc < 0) {
		(void)nextufs_w_discard_new_inode(&img, new_inode_no, &ino);
		goto out;
	}
	rc = fsync(img.fd);
out:
	nextufs_close_image(&img);
	return rc < 0 ? rc : 0;
}

int
nextufs_rename_path(const struct nextufs_write_ctx *ctx,
    const char *image_path, const char *source_path,
    const char *target_path)
{
	struct nextufs_image img;
	struct nextufs_node source;
	struct nextufs_node old_parent;
	struct nextufs_node new_parent;
	struct nextufs_node existing;
	char old_parent_path[4096];
	char new_parent_path[4096];
	char old_name[256];
	char new_name[256];
	unsigned removed_inode;
	uint32_t now;
	int rc;
	int moving_dir;
	int changing_parent;

	if (ctx == NULL)
		return -EINVAL;
	rc = nextufs_w_require_ctx_ids_fit(ctx);
	if (rc < 0)
		return rc;
	rc = nextufs_w_path_dirname_basename(source_path, old_parent_path,
	    sizeof(old_parent_path), old_name, sizeof(old_name));
	if (rc < 0)
		return rc;
	rc = nextufs_w_disallow_dot_names(old_name);
	if (rc < 0)
		return rc;
	rc = nextufs_w_path_dirname_basename(target_path, new_parent_path,
	    sizeof(new_parent_path), new_name, sizeof(new_name));
	if (rc < 0)
		return rc;
	rc = nextufs_w_disallow_target_dot_names(new_name);
	if (rc < 0)
		return rc;
	if (strcmp(source_path, target_path) == 0)
		return 0;
	rc = nextufs_w_open_image_rw(image_path, &img);
	if (rc < 0)
		return rc;
	rc = nextufs_lookup(&img, source_path, 0, &source);
	if (rc < 0)
		goto out;
	rc = nextufs_lookup(&img, old_parent_path, 1, &old_parent);
	if (rc < 0)
		goto out;
	rc = nextufs_w_require_parent_mutation(ctx, &old_parent);
	if (rc < 0)
		goto out;
	rc = nextufs_lookup(&img, new_parent_path, 1, &new_parent);
	if (rc < 0)
		goto out;
	if (!nextufs_node_is_dir(&new_parent)) {
		rc = -ENOTDIR;
		goto out;
	}
	rc = nextufs_w_require_parent_mutation(ctx, &new_parent);
	if (rc < 0)
		goto out;
	moving_dir = nextufs_node_is_dir(&source);
	changing_parent = old_parent.inode_no != new_parent.inode_no;
	rc = nextufs_w_check_sticky_parent(ctx, &old_parent, &source);
	if (rc < 0)
		goto out;
	if (moving_dir && changing_parent) {
		rc = nextufs_w_require_permissions(ctx, &source, W_OK);
		if (rc < 0)
			goto out;
	}
	if (moving_dir && nextufs_w_directory_has_ancestor(&img, &new_parent,
	    source.inode_no)) {
		rc = -EINVAL;
		goto out;
	}
	rc = nextufs_lookup(&img, target_path, 0, &existing);
	if (rc == 0) {
		if (existing.inode_no == source.inode_no) {
			rc = 0;
			goto out;
		}
		if (moving_dir != nextufs_node_is_dir(&existing)) {
			rc = moving_dir ? -ENOTDIR : -EISDIR;
			goto out;
		}
		rc = nextufs_w_check_sticky_parent(ctx, &new_parent, &existing);
		if (rc < 0)
			goto out;
		if (moving_dir && (existing.inode.nlink > 2 ||
		    !nextufs_w_directory_is_empty(&img, &existing, new_parent.inode_no))) {
			rc = -ENOTEMPTY;
			goto out;
		}
		rc = nextufs_w_remove_dirent(&img, &new_parent, new_name, &removed_inode);
		if (rc < 0)
			goto out;
		if (removed_inode != existing.inode_no) {
			rc = -EINVAL;
			goto out;
		}
		if (moving_dir)
			rc = nextufs_w_remove_dir_inode(&img, &new_parent, &existing);
		else
			rc = nextufs_w_remove_linked_inode(&img, &existing);
		if (rc < 0)
			goto out;
	} else if (rc != -ENOENT) {
		goto out;
	}
	rc = nextufs_w_insert_dirent_existing_space(&img, &new_parent, new_name,
	    source.inode_no);
	if (rc < 0)
		goto out;
	rc = nextufs_w_remove_dirent(&img, &old_parent, old_name, &removed_inode);
	if (rc < 0)
		goto out;
	if (removed_inode != source.inode_no) {
		rc = -EINVAL;
		goto out;
	}
	now = (uint32_t)time(NULL);
	if (moving_dir && changing_parent) {
		rc = nextufs_w_update_directory_parent_inode(&img, &source,
		    new_parent.inode_no);
		if (rc < 0)
			goto out;
		old_parent.inode.nlink--;
		old_parent.inode.ctime = now;
		old_parent.inode.mtime = now;
		rc = nextufs_w_write_inode_raw(&img, old_parent.inode_no,
		    &old_parent.inode);
		if (rc < 0)
			goto out;
		new_parent.inode.nlink++;
		new_parent.inode.ctime = now;
		new_parent.inode.mtime = now;
		rc = nextufs_w_write_inode_raw(&img, new_parent.inode_no,
		    &new_parent.inode);
		if (rc < 0)
			goto out;
	}
	source.inode.ctime = now;
	rc = nextufs_w_write_inode_raw(&img, source.inode_no, &source.inode);
	if (rc < 0)
		goto out;
	rc = fsync(img.fd);
out:
	nextufs_close_image(&img);
	return rc < 0 ? rc : 0;
}

int
nextufs_chmod_path(const struct nextufs_write_ctx *ctx,
    const char *image_path, const char *path, uint16_t mode)
{
	struct nextufs_image img;
	struct nextufs_node node;
	int rc;

	if (ctx == NULL)
		return -EINVAL;
	rc = nextufs_w_require_ctx_ids_fit(ctx);
	if (rc < 0)
		return rc;
	rc = nextufs_w_open_image_rw(image_path, &img);
	if (rc < 0)
		return rc;
	rc = nextufs_lookup(&img, path, 0, &node);
	if (rc < 0)
		goto out;
	rc = nextufs_w_require_chmod_allowed(ctx, &node);
	if (rc < 0)
		goto out;
	node.inode.mode = nextufs_w_sanitize_chmod_mode(ctx, &node, mode);
	node.inode.ctime = (uint32_t)time(NULL);
	rc = nextufs_w_write_inode_raw(&img, node.inode_no, &node.inode);
	if (rc < 0)
		goto out;
	rc = fsync(img.fd);
out:
	nextufs_close_image(&img);
	return rc < 0 ? rc : 0;
}

int
nextufs_chown_path(const struct nextufs_write_ctx *ctx,
    const char *image_path, const char *path,
    uid_t uid, gid_t gid)
{
	struct nextufs_image img;
	struct nextufs_node node;
	uid_t new_uid;
	gid_t new_gid;
	int rc;

	if (ctx == NULL)
		return -EINVAL;
	rc = nextufs_w_require_ctx_ids_fit(ctx);
	if (rc < 0)
		return rc;
	rc = nextufs_w_require_owner_ids_fit(uid, gid);
	if (rc < 0)
		return rc;
	rc = nextufs_w_open_image_rw(image_path, &img);
	if (rc < 0)
		return rc;
	rc = nextufs_lookup(&img, path, 0, &node);
	if (rc < 0)
		goto out;
	new_uid = uid == (uid_t)-1 ? node.inode.uid : uid;
	new_gid = gid == (gid_t)-1 ? node.inode.gid : gid;
	rc = nextufs_w_require_chown_allowed(ctx, &node, new_uid, new_gid);
	if (rc < 0)
		goto out;
	node.inode.uid = (uint16_t)new_uid;
	node.inode.gid = (uint16_t)new_gid;
	if (ctx->policy == NEXTUFS_WRITE_PERMISSIONS && !nextufs_w_ctx_is_root(ctx))
		node.inode.mode &= ~(04000 | 02000);
	node.inode.ctime = (uint32_t)time(NULL);
	rc = nextufs_w_write_inode_raw(&img, node.inode_no, &node.inode);
	if (rc < 0)
		goto out;
	rc = fsync(img.fd);
out:
	nextufs_close_image(&img);
	return rc < 0 ? rc : 0;
}

int
nextufs_utimes_path(const struct nextufs_write_ctx *ctx,
    const char *image_path, const char *path,
    uint32_t atime, uint32_t mtime)
{
	struct nextufs_image img;
	struct nextufs_node node;
	int rc;

	if (ctx == NULL)
		return -EINVAL;
	rc = nextufs_w_require_ctx_ids_fit(ctx);
	if (rc < 0)
		return rc;
	rc = nextufs_w_open_image_rw(image_path, &img);
	if (rc < 0)
		return rc;
	rc = nextufs_lookup(&img, path, 0, &node);
	if (rc < 0)
		goto out;
	if (ctx->policy == NEXTUFS_WRITE_PERMISSIONS &&
	    !nextufs_w_ctx_is_root(ctx) && ctx->uid != node.inode.uid) {
		rc = -EPERM;
		goto out;
	}
	node.inode.atime = atime;
	node.inode.mtime = mtime;
	node.inode.ctime = (uint32_t)time(NULL);
	rc = nextufs_w_write_inode_raw(&img, node.inode_no, &node.inode);
	if (rc < 0)
		goto out;
	rc = fsync(img.fd);
out:
	nextufs_close_image(&img);
	return rc < 0 ? rc : 0;
}
