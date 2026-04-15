#include "nextufs.h"
#include "nextufs_internal.h"

#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

void
nextufs_probe_info_get(const struct nextufs_image *img,
    struct nextufs_probe_info *info)
{
	memset(info, 0, sizeof(*info));
	info->image_size = img->image_size;
	info->slice_base = img->slice_base;
	info->slice_size = img->slice_size;
	info->label_off = img->label_off;
	info->label_version = img->label_version;
	info->label_secsize = img->label_secsize;
	info->label_front = img->label_front;
	info->rootpartition = img->rootpartition;
	info->used_disk_label = img->used_disk_label;
	info->source_is_container = img->source_is_container;
}

int
nextufs_node_get_root(const struct nextufs_image *img, struct nextufs_node *node)
{
	return nextufs_node_lookup(img, "/", 1, node);
}

int
nextufs_node_get_by_inode(const struct nextufs_image *img, unsigned inode_no,
    struct nextufs_node *node)
{
	node->inode_no = inode_no;
	return nextufs_inode_read(img, inode_no, &node->inode,
	    &node->inode_off);
}

int
nextufs_node_lookup(const struct nextufs_image *img, const char *path,
    int follow_final_symlink, struct nextufs_node *node)
{
	int rc;

	if (follow_final_symlink) {
		rc = nextufs_path_lookup(img, path, &node->inode_no, &node->inode,
		    &node->inode_off);
	} else {
		rc = nextufs_path_lookup_nofollow(img, path, &node->inode_no,
		    &node->inode, &node->inode_off);
	}
	return rc;
}

int
nextufs_node_stat(const struct nextufs_node *node, struct stat *st)
{
	memset(st, 0, sizeof(*st));
	st->st_ino = node->inode_no;
	st->st_mode = node->inode.mode;
	st->st_nlink = node->inode.nlink ? node->inode.nlink : 1;
	st->st_uid = node->inode.uid;
	st->st_gid = node->inode.gid;
	st->st_size = (off_t)node->inode.size;
	st->st_blocks = node->inode.blocks;
	if ((node->inode.mode & NEXTUFS_IFMT) == NEXTUFS_IFCHR ||
	    (node->inode.mode & NEXTUFS_IFMT) == NEXTUFS_IFBLK)
		st->st_rdev = (dev_t)node->inode.db[0];
	st->st_atime = (time_t)node->inode.atime;
	st->st_mtime = (time_t)node->inode.mtime;
	st->st_ctime = (time_t)node->inode.ctime;
	return 0;
}

int
nextufs_node_is_dir(const struct nextufs_node *node)
{
	return (node->inode.mode & NEXTUFS_IFMT) == NEXTUFS_IFDIR;
}

int
nextufs_node_is_reg(const struct nextufs_node *node)
{
	return (node->inode.mode & NEXTUFS_IFMT) == NEXTUFS_IFREG;
}

int
nextufs_node_is_lnk(const struct nextufs_node *node)
{
	return (node->inode.mode & NEXTUFS_IFMT) == NEXTUFS_IFLNK;
}

int
nextufs__node_check_access(const struct nextufs_node *node, uid_t uid, gid_t gid,
    const gid_t *groups, size_t group_count, int mask, int allow_write)
{
	mode_t bits;
	mode_t perms;
	size_t i;

	if (!allow_write && (mask & W_OK))
		return -EROFS;
	if (mask == F_OK)
		return 0;
	if (uid == 0) {
		if ((mask & X_OK) != 0) {
			if ((node->inode.mode & 0111) == 0)
				return -EACCES;
		}
		return 0;
	}
	if (uid == node->inode.uid)
		perms = (node->inode.mode >> 6) & 07;
	else {
		perms = node->inode.mode & 07;
		if (gid == node->inode.gid)
			perms = (node->inode.mode >> 3) & 07;
		else {
			for (i = 0; i < group_count; i++) {
				if (groups[i] == node->inode.gid) {
					perms = (node->inode.mode >> 3) & 07;
					break;
				}
			}
		}
	}
	bits = 0;
	if ((mask & R_OK) != 0)
		bits |= 04;
	if ((mask & W_OK) != 0)
		bits |= 02;
	if ((mask & X_OK) != 0)
		bits |= 01;
	if ((perms & bits) != bits)
		return -EACCES;
	return 0;
}

int
nextufs_node_check_access(const struct nextufs_node *node, uid_t uid, gid_t gid,
    int mask)
{
	return nextufs__node_check_access(node, uid, gid, NULL, 0, mask, 0);
}

int
nextufs_fs_statvfs(const struct nextufs_image *img, struct statvfs *stvfs)
{
	uint64_t free_frags;
	uint64_t reserved_frags;
	uint64_t avail_frags;
	uint64_t total_inodes;

	memset(stvfs, 0, sizeof(*stvfs));
	if (img->sb.frag_size == 0 || img->sb.block_size == 0 ||
	    img->sb.frags_per_block == 0)
		return -EINVAL;
	free_frags = ((uint64_t)img->sb.free_block_count * img->sb.frags_per_block) +
	    img->sb.free_frag_count;
	reserved_frags = ((uint64_t)img->sb.data_frag_count * img->sb.minfree) / 100U;
	avail_frags = free_frags > reserved_frags ? free_frags - reserved_frags : 0;
	total_inodes = (uint64_t)img->sb.cg_count * img->sb.inodes_per_group;

	stvfs->f_bsize = img->sb.block_size;
	stvfs->f_frsize = img->sb.frag_size;
	stvfs->f_blocks = img->sb.data_frag_count;
	stvfs->f_bfree = free_frags;
	stvfs->f_bavail = avail_frags;
	stvfs->f_files = total_inodes;
	stvfs->f_ffree = img->sb.free_inode_count;
	stvfs->f_favail = img->sb.free_inode_count;
	stvfs->f_namemax = 255;
	return 0;
}

int
nextufs_path_readlink(const struct nextufs_image *img, const char *path,
    char *out, size_t out_size)
{
	struct nextufs_node node;
	int rc;

	rc = nextufs_node_lookup(img, path, 0, &node);
	if (rc < 0)
		return rc;
	return nextufs_inode_readlink(img, &node.inode, out, out_size);
}

int
nextufs_directory_iterate_path(const struct nextufs_image *img, const char *path,
    int follow_final_symlink, nextufs_dir_iter_cb cb, void *ctx)
{
	struct nextufs_node node;
	int rc;

	rc = nextufs_node_lookup(img, path, follow_final_symlink, &node);
	if (rc < 0)
		return rc;
	if (!nextufs_node_is_dir(&node))
		return -ENOTDIR;
	return nextufs_directory_iterate(img, &node.inode, cb, ctx);
}

int
nextufs_directory_iterate_nodes_path(const struct nextufs_image *img,
    const char *path, int follow_final_symlink, nextufs_dir_node_iter_cb cb,
    void *ctx)
{
	struct nextufs_node node;
	int rc;

	rc = nextufs_node_lookup(img, path, follow_final_symlink, &node);
	if (rc < 0)
		return rc;
	if (!nextufs_node_is_dir(&node))
		return -ENOTDIR;
	return nextufs_directory_iterate_nodes(img, &node.inode, cb, ctx);
}

int
nextufs_path_read(const struct nextufs_image *img, const char *path,
    uint64_t start, uint8_t *buf, size_t buf_size, size_t *bytes_read)
{
	struct nextufs_node node;
	int rc;

	rc = nextufs_node_lookup(img, path, 1, &node);
	if (rc < 0)
		return rc;
	if (!nextufs_node_is_reg(&node))
		return -EINVAL;
	return nextufs_inode_read_data(img, &node.inode, start, buf, buf_size,
	    bytes_read);
}
