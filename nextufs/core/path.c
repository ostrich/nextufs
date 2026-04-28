#include "nextufs_internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
append_path_component(char *path, size_t path_size, const char *component)
{
	char tmp[NEXTUFS_MAX_PATH_LEN];
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

static int
lookup_path_recursive(const struct nextufs_image *img, const char *path,
    unsigned *inode_no_out, struct nextufs_inode *inode_out,
    off_t *inode_off_out, unsigned depth, int follow_final_symlink)
{
	char *scratch;
	char *segment;
	char *cursor;
	char prefix[NEXTUFS_MAX_PATH_LEN];
	unsigned current_inode_no;
	struct nextufs_inode current_inode;
	off_t current_inode_off;

	if (depth > NEXTUFS_MAX_LOOKUP_DEPTH)
		return -ELOOP;
	if (nextufs_inode_read(img, NEXTUFS_ROOT_INODE, &current_inode,
	    &current_inode_off) < 0)
		return -EIO;
	current_inode_no = NEXTUFS_ROOT_INODE;
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
	if (scratch == NULL)
		return -ENOMEM;
	cursor = scratch;
	while ((segment = strsep(&cursor, "/")) != NULL) {
		unsigned next_inode_no;
		char remainder[NEXTUFS_MAX_PATH_LEN];
		char link_target[NEXTUFS_MAX_PATH_LEN];
		char new_path[NEXTUFS_MAX_PATH_LEN];

		if (*segment == '\0')
			continue;
		if ((current_inode.mode & NEXTUFS_IFMT) != NEXTUFS_IFDIR) {
			free(scratch);
			return -ENOTDIR;
		}
		if (nextufs__find_name_in_directory(img, &current_inode, segment,
		    &next_inode_no) < 0) {
			free(scratch);
			return -EIO;
		}
		if (next_inode_no == 0) {
			free(scratch);
			return -ENOENT;
		}
		if (nextufs_inode_read(img, next_inode_no, &current_inode,
		    &current_inode_off) < 0) {
			free(scratch);
			return -EIO;
		}
		if (cursor != NULL && *cursor != '\0') {
			if (snprintf(remainder, sizeof(remainder), "%s", cursor) >=
			    (int)sizeof(remainder)) {
				free(scratch);
				return -ENAMETOOLONG;
			}
		} else {
			remainder[0] = '\0';
		}
		if ((current_inode.mode & NEXTUFS_IFMT) == NEXTUFS_IFLNK &&
		    ((cursor == NULL || *cursor == '\0') ? follow_final_symlink : 1)) {
			if (nextufs_inode_readlink(img, &current_inode, link_target,
			    sizeof(link_target)) < 0) {
				free(scratch);
				return -EIO;
			}
			if (link_target[0] == '/') {
				if (remainder[0] != '\0') {
					if (snprintf(new_path, sizeof(new_path), "%s/%s",
					    link_target, remainder) >= (int)sizeof(new_path)) {
						free(scratch);
						return -ENAMETOOLONG;
					}
				} else if (snprintf(new_path, sizeof(new_path), "%s",
				    link_target) >= (int)sizeof(new_path)) {
					free(scratch);
					return -ENAMETOOLONG;
				}
			} else {
				if (remainder[0] != '\0') {
					if (snprintf(new_path, sizeof(new_path), "%s/%s/%s",
					    prefix, link_target, remainder) >=
					    (int)sizeof(new_path)) {
						free(scratch);
						return -ENAMETOOLONG;
					}
				} else if (snprintf(new_path, sizeof(new_path), "%s/%s",
				    prefix, link_target) >= (int)sizeof(new_path)) {
					free(scratch);
					return -ENAMETOOLONG;
				}
			}
			free(scratch);
			return lookup_path_recursive(img, new_path, inode_no_out,
			    inode_out, inode_off_out, depth + 1,
			    follow_final_symlink);
		}
		current_inode_no = next_inode_no;
		if (append_path_component(prefix, sizeof(prefix), segment) < 0) {
			free(scratch);
			return -ENAMETOOLONG;
		}
	}
	free(scratch);
	*inode_no_out = current_inode_no;
	*inode_out = current_inode;
	if (inode_off_out != NULL)
		*inode_off_out = current_inode_off;
	return 0;
}

int
nextufs_path_lookup(const struct nextufs_image *img, const char *path,
    unsigned *inode_no_out, struct nextufs_inode *inode_out,
    off_t *inode_off_out)
{
	return lookup_path_recursive(img, path, inode_no_out, inode_out,
	    inode_off_out, 0, 1);
}

int
nextufs_path_lookup_nofollow(const struct nextufs_image *img, const char *path,
    unsigned *inode_no_out, struct nextufs_inode *inode_out,
    off_t *inode_off_out)
{
	return lookup_path_recursive(img, path, inode_no_out, inode_out,
	    inode_off_out, 0, 0);
}
