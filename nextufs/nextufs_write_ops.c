#include "nextufs_write.h"
#include "nextufs_write_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int
nextufs_create_small_file_rw(const char *image_path, const char *path,
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
	int fd;
	int rc;
	uint32_t now;

	rc = nextufs_w_path_dirname_basename(path, parent_path, sizeof(parent_path),
	    name, sizeof(name));
	if (rc < 0)
		return rc;
	rc = nextufs_open_image(&img, image_path);
	if (rc < 0)
		return rc;
	close(img.fd);
	fd = open(image_path, O_RDWR);
	if (fd < 0) {
		nextufs_close_image(&img);
		return -errno;
	}
	img.fd = fd;
	rc = nextufs_lookup(&img, parent_path, 1, &parent);
	if (rc < 0)
		goto out;
	if (!nextufs_node_is_dir(&parent)) {
		rc = -ENOTDIR;
		goto out;
	}
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
	ino.uid = 0;
	ino.gid = parent.inode.gid;
	ino.size = data_len;
	ino.atime = now;
	ino.mtime = now;
	ino.ctime = now;
	rc = nextufs_w_allocate_data_for_inode(&img, parent_cg, data, data_len, &ino);
	if (rc < 0)
		goto out;
	rc = nextufs_w_write_inode_raw(&img, new_inode_no, &ino);
	if (rc < 0)
		goto out;
	rc = nextufs_w_insert_dirent_existing_space(&img, &parent, name, new_inode_no);
	if (rc < 0)
		goto out;
	rc = fsync(img.fd);
out:
	nextufs_close_image(&img);
	return rc < 0 ? rc : 0;
}

int
nextufs_create_file_from_hostfile_rw(const char *image_path, const char *path,
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
	rc = nextufs_create_small_file_rw(image_path, path, buf, data_len);
	free(buf);
	return rc;
}

int
nextufs_unlink_path_rw(const char *image_path, const char *path)
{
	struct nextufs_image img;
	struct nextufs_node parent;
	struct nextufs_node target;
	struct nextufs_inode cleared;
	char parent_path[4096];
	char name[256];
	unsigned removed_inode;
	int fd;
	int rc;

	rc = nextufs_w_path_dirname_basename(path, parent_path, sizeof(parent_path),
	    name, sizeof(name));
	if (rc < 0)
		return rc;
	rc = nextufs_open_image(&img, image_path);
	if (rc < 0)
		return rc;
	close(img.fd);
	fd = open(image_path, O_RDWR);
	if (fd < 0) {
		nextufs_close_image(&img);
		return -errno;
	}
	img.fd = fd;
	rc = nextufs_lookup(&img, parent_path, 1, &parent);
	if (rc < 0)
		goto out;
	if (!nextufs_node_is_dir(&parent)) {
		rc = -ENOTDIR;
		goto out;
	}
	rc = nextufs_lookup(&img, path, 0, &target);
	if (rc < 0)
		goto out;
	if (nextufs_node_is_dir(&target)) {
		rc = -EISDIR;
		goto out;
	}
	rc = nextufs_w_remove_dirent(&img, &parent, name, &removed_inode);
	if (rc < 0)
		goto out;
	if (removed_inode != target.inode_no) {
		rc = -EINVAL;
		goto out;
	}
	if (target.inode.nlink == 0) {
		rc = -EINVAL;
		goto out;
	}
	target.inode.nlink--;
	if (target.inode.nlink == 0) {
		rc = nextufs_w_free_regular_file_storage(&img, &target.inode);
		if (rc < 0)
			goto out;
		memset(&cleared, 0, sizeof(cleared));
		rc = nextufs_w_write_inode_raw(&img, target.inode_no, &cleared);
		if (rc < 0)
			goto out;
		rc = nextufs_w_free_inode_in_cg(&img, target.inode_no, target.inode.mode);
		if (rc < 0)
			goto out;
	} else {
		target.inode.ctime = (uint32_t)time(NULL);
		rc = nextufs_w_write_inode_raw(&img, target.inode_no, &target.inode);
		if (rc < 0)
			goto out;
	}
	rc = fsync(img.fd);
out:
	nextufs_close_image(&img);
	return rc < 0 ? rc : 0;
}

int
nextufs_mkdir_path_rw(const char *image_path, const char *path, uint16_t mode)
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
	int fd;
	int rc;
	uint32_t now;

	rc = nextufs_w_path_dirname_basename(path, parent_path, sizeof(parent_path),
	    name, sizeof(name));
	if (rc < 0)
		return rc;
	rc = nextufs_open_image(&img, image_path);
	if (rc < 0)
		return rc;
	close(img.fd);
	fd = open(image_path, O_RDWR);
	if (fd < 0) {
		nextufs_close_image(&img);
		return -errno;
	}
	img.fd = fd;
	rc = nextufs_lookup(&img, parent_path, 1, &parent);
	if (rc < 0)
		goto out;
	if (!nextufs_node_is_dir(&parent)) {
		rc = -ENOTDIR;
		goto out;
	}
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
	if (rc < 0)
		goto out;
	memset(&ino, 0, sizeof(ino));
	now = (uint32_t)time(NULL);
	ino.mode = NEXTUFS_IFDIR | (mode & 0777);
	ino.nlink = 2;
	ino.uid = 0;
	ino.gid = parent.inode.gid;
	ino.size = DIRBLKSIZ;
	ino.atime = now;
	ino.mtime = now;
	ino.ctime = now;
	ino.db[0] = alloc_frag;
	ino.blocks = DIRBLKSIZ / DEV_BSIZE;
	rc = nextufs_w_write_inode_raw(&img, new_inode_no, &ino);
	if (rc < 0)
		goto out;
	rc = nextufs_w_insert_dirent_existing_space(&img, &parent, name, new_inode_no);
	if (rc < 0)
		goto out;
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
nextufs_w_rewrite_file_contents_rw(const char *image_path, const char *path,
    const void *data, size_t data_len, int append)
{
	struct nextufs_image img;
	struct nextufs_node target;
	struct nextufs_inode old_inode;
	struct nextufs_inode new_inode;
	uint8_t *buf = NULL;
	size_t old_size;
	size_t final_size;
	unsigned preferred_cg;
	int fd;
	int rc;
	uint32_t now;

	rc = nextufs_open_image(&img, image_path);
	if (rc < 0)
		return rc;
	close(img.fd);
	fd = open(image_path, O_RDWR);
	if (fd < 0) {
		nextufs_close_image(&img);
		return -errno;
	}
	img.fd = fd;
	rc = nextufs_lookup(&img, path, 0, &target);
	if (rc < 0)
		goto out;
	if (!nextufs_node_is_reg(&target)) {
		rc = -EISDIR;
		goto out;
	}
	if (target.inode.ib[0] != 0 || target.inode.ib[1] != 0 || target.inode.ib[2] != 0) {
		rc = -EOPNOTSUPP;
		goto out;
	}
	old_inode = target.inode;
	old_size = (size_t)old_inode.size;
	final_size = append ? old_size + data_len : data_len;
	buf = malloc(final_size == 0 ? 1 : final_size);
	if (buf == NULL) {
		rc = -ENOMEM;
		goto out;
	}
	if (append && old_size != 0) {
		rc = nextufs_read_inode_data(&img, &old_inode, 0, buf, old_size, NULL);
		if (rc < 0)
			goto out;
		memcpy(buf + old_size, data, data_len);
	} else if (final_size != 0) {
		memcpy(buf, data, data_len);
	}
	new_inode = old_inode;
	memset(new_inode.db, 0, sizeof(new_inode.db));
	memset(new_inode.ib, 0, sizeof(new_inode.ib));
	new_inode.size = 0;
	new_inode.blocks = 0;
	preferred_cg = target.inode_no / img.sb.inodes_per_group;
	rc = nextufs_w_allocate_data_for_inode(&img, preferred_cg, buf, final_size,
	    &new_inode);
	if (rc < 0)
		goto out;
	now = (uint32_t)time(NULL);
	new_inode.mtime = now;
	new_inode.ctime = now;
	rc = nextufs_w_write_inode_raw(&img, target.inode_no, &new_inode);
	if (rc < 0)
		goto out;
	rc = nextufs_w_free_regular_file_storage(&img, &old_inode);
	if (rc < 0)
		goto out;
	rc = fsync(img.fd);
out:
	free(buf);
	nextufs_close_image(&img);
	return rc < 0 ? rc : 0;
}

int
nextufs_overwrite_file_rw(const char *image_path, const char *path,
    const void *data, size_t data_len)
{
	return nextufs_w_rewrite_file_contents_rw(image_path, path, data, data_len, 0);
}

int
nextufs_append_file_rw(const char *image_path, const char *path,
    const void *data, size_t data_len)
{
	return nextufs_w_rewrite_file_contents_rw(image_path, path, data, data_len, 1);
}

int
nextufs_rmdir_path_rw(const char *image_path, const char *path)
{
	struct nextufs_image img;
	struct nextufs_node parent;
	struct nextufs_node target;
	struct nextufs_inode cleared;
	char parent_path[4096];
	char name[256];
	unsigned removed_inode;
	int fd;
	int rc;
	uint32_t now;

	rc = nextufs_w_path_dirname_basename(path, parent_path, sizeof(parent_path),
	    name, sizeof(name));
	if (rc < 0)
		return rc;
	rc = nextufs_open_image(&img, image_path);
	if (rc < 0)
		return rc;
	close(img.fd);
	fd = open(image_path, O_RDWR);
	if (fd < 0) {
		nextufs_close_image(&img);
		return -errno;
	}
	img.fd = fd;
	rc = nextufs_lookup(&img, parent_path, 1, &parent);
	if (rc < 0)
		goto out;
	rc = nextufs_lookup(&img, path, 0, &target);
	if (rc < 0)
		goto out;
	if (!nextufs_node_is_dir(&target)) {
		rc = -ENOTDIR;
		goto out;
	}
	if (!nextufs_w_directory_is_empty(&img, &target, parent.inode_no)) {
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
	rc = nextufs_w_free_fragment_run(&img, target.inode.db[0], 1);
	if (rc < 0)
		goto out;
	memset(&cleared, 0, sizeof(cleared));
	rc = nextufs_w_write_inode_raw(&img, target.inode_no, &cleared);
	if (rc < 0)
		goto out;
	rc = nextufs_w_free_inode_in_cg(&img, target.inode_no, target.inode.mode);
	if (rc < 0)
		goto out;
	parent.inode.nlink--;
	now = (uint32_t)time(NULL);
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

int
nextufs_link_path_rw(const char *image_path, const char *source_path,
    const char *target_path)
{
	struct nextufs_image img;
	struct nextufs_node source;
	struct nextufs_node parent;
	struct nextufs_node existing;
	char parent_path[4096];
	char name[256];
	int fd;
	int rc;
	uint32_t now;

	rc = nextufs_w_path_dirname_basename(target_path, parent_path, sizeof(parent_path),
	    name, sizeof(name));
	if (rc < 0)
		return rc;
	rc = nextufs_open_image(&img, image_path);
	if (rc < 0)
		return rc;
	close(img.fd);
	fd = open(image_path, O_RDWR);
	if (fd < 0) {
		nextufs_close_image(&img);
		return -errno;
	}
	img.fd = fd;
	rc = nextufs_lookup(&img, source_path, 0, &source);
	if (rc < 0)
		goto out;
	if (nextufs_node_is_dir(&source)) {
		rc = -EPERM;
		goto out;
	}
	rc = nextufs_lookup(&img, parent_path, 1, &parent);
	if (rc < 0)
		goto out;
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
nextufs_symlink_path_rw(const char *image_path, const char *target,
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
	int fd;
	int rc;
	uint32_t now;
	size_t target_len;

	target_len = strlen(target);
	if (target_len > 60)
		return -EOPNOTSUPP;
	rc = nextufs_w_path_dirname_basename(link_path, parent_path, sizeof(parent_path),
	    name, sizeof(name));
	if (rc < 0)
		return rc;
	rc = nextufs_open_image(&img, image_path);
	if (rc < 0)
		return rc;
	close(img.fd);
	fd = open(image_path, O_RDWR);
	if (fd < 0) {
		nextufs_close_image(&img);
		return -errno;
	}
	img.fd = fd;
	rc = nextufs_lookup(&img, parent_path, 1, &parent);
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
	ino.uid = 0;
	ino.gid = parent.inode.gid;
	ino.size = target_len;
	ino.atime = now;
	ino.mtime = now;
	ino.ctime = now;
	ino.flags = 0x0001U;
	nextufs_w_store_inline_symlink_bytes(&ino, target, target_len);
	rc = nextufs_w_write_inode_raw(&img, new_inode_no, &ino);
	if (rc < 0)
		goto out;
	rc = nextufs_w_insert_dirent_existing_space(&img, &parent, name, new_inode_no);
	if (rc < 0)
		goto out;
	rc = fsync(img.fd);
out:
	nextufs_close_image(&img);
	return rc < 0 ? rc : 0;
}

int
nextufs_chmod_path_rw(const char *image_path, const char *path, uint16_t mode)
{
	struct nextufs_image img;
	struct nextufs_node node;
	int fd;
	int rc;

	rc = nextufs_open_image(&img, image_path);
	if (rc < 0)
		return rc;
	close(img.fd);
	fd = open(image_path, O_RDWR);
	if (fd < 0) {
		nextufs_close_image(&img);
		return -errno;
	}
	img.fd = fd;
	rc = nextufs_lookup(&img, path, 0, &node);
	if (rc < 0)
		goto out;
	node.inode.mode = (node.inode.mode & NEXTUFS_IFMT) | (mode & 07777);
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
nextufs_chown_path_rw(const char *image_path, const char *path,
    uint16_t uid, uint16_t gid)
{
	struct nextufs_image img;
	struct nextufs_node node;
	int fd;
	int rc;

	rc = nextufs_open_image(&img, image_path);
	if (rc < 0)
		return rc;
	close(img.fd);
	fd = open(image_path, O_RDWR);
	if (fd < 0) {
		nextufs_close_image(&img);
		return -errno;
	}
	img.fd = fd;
	rc = nextufs_lookup(&img, path, 0, &node);
	if (rc < 0)
		goto out;
	node.inode.uid = uid;
	node.inode.gid = gid;
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
nextufs_utimes_path_rw(const char *image_path, const char *path,
    uint32_t atime, uint32_t mtime)
{
	struct nextufs_image img;
	struct nextufs_node node;
	int fd;
	int rc;

	rc = nextufs_open_image(&img, image_path);
	if (rc < 0)
		return rc;
	close(img.fd);
	fd = open(image_path, O_RDWR);
	if (fd < 0) {
		nextufs_close_image(&img);
		return -errno;
	}
	img.fd = fd;
	rc = nextufs_lookup(&img, path, 0, &node);
	if (rc < 0)
		goto out;
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
