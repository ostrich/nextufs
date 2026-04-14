#include "nextufs.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NEXTUFS_STRESS_MAX_OBJECTS 512
#define NEXTUFS_STRESS_MAX_PATHS 1024
#define NEXTUFS_STRESS_MAX_NAME 32
#define NEXTUFS_STRESS_MAX_PATH 512
#define NEXTUFS_STRESS_MAX_FILE 131072
#define NEXTUFS_STRESS_MAX_OP_DESC 512

enum nextufs_stress_type {
	NEXTUFS_STRESS_DIR = 0,
	NEXTUFS_STRESS_REG = 1,
	NEXTUFS_STRESS_LNK = 2,
	NEXTUFS_STRESS_FIFO = 3,
	NEXTUFS_STRESS_CHR = 4,
	NEXTUFS_STRESS_BLK = 5,
};

struct nextufs_stress_obj {
	int used;
	unsigned id;
	enum nextufs_stress_type type;
	uint16_t mode;
	uid_t uid;
	gid_t gid;
	uint32_t rdev;
	size_t size;
	uint8_t *data;
	char *link_target;
	unsigned refcount;
};

struct nextufs_stress_path {
	int used;
	unsigned obj_id;
	char *path;
};

struct nextufs_stress_ctx {
	const char *image_path;
	char root_path[NEXTUFS_STRESS_MAX_PATH];
	struct nextufs_write_ctx write_ctx;
	uint64_t seed;
	unsigned op_count;
	unsigned current_op;
	int verbose;
	struct nextufs_stress_obj objects[NEXTUFS_STRESS_MAX_OBJECTS];
	struct nextufs_stress_path paths[NEXTUFS_STRESS_MAX_PATHS];
	unsigned next_obj_id;
	char last_op[NEXTUFS_STRESS_MAX_OP_DESC];
};

struct nextufs_stress_path_vec {
	char **items;
	size_t count;
	size_t cap;
};

static uint64_t
nextufs_stress_rand64(struct nextufs_stress_ctx *ctx)
{
	uint64_t x = ctx->seed;

	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	ctx->seed = x;
	return x;
}

static unsigned
nextufs_stress_rand_range(struct nextufs_stress_ctx *ctx, unsigned limit)
{
	if (limit == 0)
		return 0;
	return (unsigned)(nextufs_stress_rand64(ctx) % limit);
}

static int
nextufs_stress_one_in(struct nextufs_stress_ctx *ctx, unsigned n)
{
	return n != 0 && nextufs_stress_rand_range(ctx, n) == 0;
}

static void
nextufs_stress_set_op(struct nextufs_stress_ctx *ctx, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(ctx->last_op, sizeof(ctx->last_op), fmt, ap);
	va_end(ap);
	if (ctx->verbose)
		printf("[%03u] %s\n", ctx->current_op, ctx->last_op);
}

static enum nextufs_stress_type
nextufs_stress_mode_type(mode_t mode)
{
	switch (mode & NEXTUFS_IFMT) {
	case NEXTUFS_IFDIR:
		return NEXTUFS_STRESS_DIR;
	case NEXTUFS_IFLNK:
		return NEXTUFS_STRESS_LNK;
	case NEXTUFS_IFIFO:
		return NEXTUFS_STRESS_FIFO;
	case NEXTUFS_IFCHR:
		return NEXTUFS_STRESS_CHR;
	case NEXTUFS_IFBLK:
		return NEXTUFS_STRESS_BLK;
	default:
		return NEXTUFS_STRESS_REG;
	}
}

static struct nextufs_stress_obj *
nextufs_stress_find_obj(struct nextufs_stress_ctx *ctx, unsigned obj_id)
{
	size_t i;

	for (i = 0; i < NEXTUFS_STRESS_MAX_OBJECTS; i++) {
		if (ctx->objects[i].used && ctx->objects[i].id == obj_id)
			return &ctx->objects[i];
	}
	return NULL;
}

static struct nextufs_stress_path *
nextufs_stress_find_path(struct nextufs_stress_ctx *ctx, const char *path)
{
	size_t i;

	for (i = 0; i < NEXTUFS_STRESS_MAX_PATHS; i++) {
		if (ctx->paths[i].used && strcmp(ctx->paths[i].path, path) == 0)
			return &ctx->paths[i];
	}
	return NULL;
}

static int
nextufs_stress_object_count(const struct nextufs_stress_ctx *ctx,
    enum nextufs_stress_type type)
{
	size_t i;
	int count = 0;

	for (i = 0; i < NEXTUFS_STRESS_MAX_OBJECTS; i++) {
		if (!ctx->objects[i].used)
			continue;
		if (ctx->objects[i].type == type)
			count++;
	}
	return count;
}

static int
nextufs_stress_path_count(const struct nextufs_stress_ctx *ctx)
{
	size_t i;
	int count = 0;

	for (i = 0; i < NEXTUFS_STRESS_MAX_PATHS; i++) {
		if (ctx->paths[i].used)
			count++;
	}
	return count;
}

static int
nextufs_stress_add_object(struct nextufs_stress_ctx *ctx,
    enum nextufs_stress_type type, uint16_t mode, uid_t uid, gid_t gid,
    uint32_t rdev, const uint8_t *data, size_t size, const char *link_target,
    unsigned *obj_id_out)
{
	size_t i;
	struct nextufs_stress_obj *obj;

	for (i = 0; i < NEXTUFS_STRESS_MAX_OBJECTS; i++) {
		if (!ctx->objects[i].used)
			break;
	}
	if (i == NEXTUFS_STRESS_MAX_OBJECTS)
		return -ENOSPC;
	obj = &ctx->objects[i];
	memset(obj, 0, sizeof(*obj));
	obj->used = 1;
	obj->id = ++ctx->next_obj_id;
	obj->type = type;
	obj->mode = mode;
	obj->uid = uid;
	obj->gid = gid;
	obj->rdev = rdev;
	obj->size = size;
	if (size != 0) {
		obj->data = malloc(size);
		if (obj->data == NULL) {
			memset(obj, 0, sizeof(*obj));
			return -ENOMEM;
		}
		memcpy(obj->data, data, size);
	}
	if (link_target != NULL) {
		obj->link_target = strdup(link_target);
		if (obj->link_target == NULL) {
			free(obj->data);
			memset(obj, 0, sizeof(*obj));
			return -ENOMEM;
		}
	}
	*obj_id_out = obj->id;
	return 0;
}

static void
nextufs_stress_drop_object(struct nextufs_stress_obj *obj)
{
	free(obj->data);
	free(obj->link_target);
	memset(obj, 0, sizeof(*obj));
}

static int
nextufs_stress_add_path(struct nextufs_stress_ctx *ctx, const char *path,
    unsigned obj_id)
{
	size_t i;

	for (i = 0; i < NEXTUFS_STRESS_MAX_PATHS; i++) {
		if (!ctx->paths[i].used)
			break;
	}
	if (i == NEXTUFS_STRESS_MAX_PATHS)
		return -ENOSPC;
	ctx->paths[i].path = strdup(path);
	if (ctx->paths[i].path == NULL)
		return -ENOMEM;
	ctx->paths[i].used = 1;
	ctx->paths[i].obj_id = obj_id;
	return 0;
}

static int
nextufs_stress_release_object(struct nextufs_stress_ctx *ctx, unsigned obj_id)
{
	struct nextufs_stress_obj *obj;

	obj = nextufs_stress_find_obj(ctx, obj_id);
	if (obj == NULL)
		return -ENOENT;
	if (obj->refcount == 0)
		return -EINVAL;
	obj->refcount--;
	if (obj->refcount == 0)
		nextufs_stress_drop_object(obj);
	return 0;
}

static int
nextufs_stress_remove_path(struct nextufs_stress_ctx *ctx, const char *path)
{
	struct nextufs_stress_path *entry;
	unsigned obj_id;

	entry = nextufs_stress_find_path(ctx, path);
	if (entry == NULL)
		return -ENOENT;
	obj_id = entry->obj_id;
	free(entry->path);
	memset(entry, 0, sizeof(*entry));
	return nextufs_stress_release_object(ctx, obj_id);
}

static int
nextufs_stress_resize_data(struct nextufs_stress_obj *obj, size_t new_size)
{
	uint8_t *new_data;

	if (new_size > NEXTUFS_STRESS_MAX_FILE)
		return -EFBIG;
	if (new_size == 0) {
		free(obj->data);
		obj->data = NULL;
		obj->size = 0;
		return 0;
	}
	new_data = realloc(obj->data, new_size);
	if (new_data == NULL)
		return -ENOMEM;
	if (new_size > obj->size)
		memset(new_data + obj->size, 0, new_size - obj->size);
	obj->data = new_data;
	obj->size = new_size;
	return 0;
}

static int
nextufs_stress_path_has_prefix(const char *path, const char *prefix)
{
	size_t prefix_len = strlen(prefix);

	if (strncmp(path, prefix, prefix_len) != 0)
		return 0;
	if (path[prefix_len] == '\0')
		return 1;
	return prefix[prefix_len - 1] == '/' || path[prefix_len] == '/';
}

static int
nextufs_stress_dir_empty(const struct nextufs_stress_ctx *ctx, const char *path)
{
	size_t i;
	size_t len = strlen(path);

	for (i = 0; i < NEXTUFS_STRESS_MAX_PATHS; i++) {
		if (!ctx->paths[i].used || strcmp(ctx->paths[i].path, path) == 0)
			continue;
		if (strncmp(ctx->paths[i].path, path, len) == 0 &&
		    ctx->paths[i].path[len] == '/')
			return 0;
	}
	return 1;
}

static int
nextufs_stress_make_child_path(char *out, size_t out_size, const char *dir,
    const char *name)
{
	int n;

	if (strcmp(dir, "/") == 0)
		n = snprintf(out, out_size, "/%s", name);
	else
		n = snprintf(out, out_size, "%s/%s", dir, name);
	if (n < 0 || (size_t)n >= out_size)
		return -ENAMETOOLONG;
	return 0;
}

static int
nextufs_stress_random_name(struct nextufs_stress_ctx *ctx, char *name,
    size_t name_size)
{
	static const char alphabet[] =
	    "abcdefghijklmnopqrstuvwxyz0123456789_-";
	size_t i;
	size_t len;

	len = 4 + nextufs_stress_rand_range(ctx, 12);
	if (len + 1 > name_size)
		return -ENAMETOOLONG;
	for (i = 0; i < len; i++)
		name[i] = alphabet[nextufs_stress_rand_range(ctx, sizeof(alphabet) - 1)];
	name[len] = '\0';
	return 0;
}

static int
nextufs_stress_pick_dir_path(struct nextufs_stress_ctx *ctx, int empty_only,
    char *path_out, size_t path_out_size)
{
	unsigned indices[NEXTUFS_STRESS_MAX_PATHS];
	size_t i;
	unsigned count = 0;

	for (i = 0; i < NEXTUFS_STRESS_MAX_PATHS; i++) {
		struct nextufs_stress_obj *obj;

		if (!ctx->paths[i].used)
			continue;
		obj = nextufs_stress_find_obj(ctx, ctx->paths[i].obj_id);
		if (obj == NULL || obj->type != NEXTUFS_STRESS_DIR)
			continue;
		if (empty_only && !nextufs_stress_dir_empty(ctx, ctx->paths[i].path))
			continue;
		indices[count++] = (unsigned)i;
	}
	if (count == 0)
		return -ENOENT;
	i = indices[nextufs_stress_rand_range(ctx, count)];
	if (strlen(ctx->paths[i].path) + 1 > path_out_size)
		return -ENAMETOOLONG;
	memcpy(path_out, ctx->paths[i].path, strlen(ctx->paths[i].path) + 1);
	return 0;
}

static int
nextufs_stress_pick_path_of_type(struct nextufs_stress_ctx *ctx,
    enum nextufs_stress_type type, int exclude_root, char *path_out,
    size_t path_out_size)
{
	unsigned indices[NEXTUFS_STRESS_MAX_PATHS];
	size_t i;
	unsigned count = 0;

	for (i = 0; i < NEXTUFS_STRESS_MAX_PATHS; i++) {
		struct nextufs_stress_obj *obj;

		if (!ctx->paths[i].used)
			continue;
		if (exclude_root && strcmp(ctx->paths[i].path, ctx->root_path) == 0)
			continue;
		obj = nextufs_stress_find_obj(ctx, ctx->paths[i].obj_id);
		if (obj == NULL || obj->type != type)
			continue;
		indices[count++] = (unsigned)i;
	}
	if (count == 0)
		return -ENOENT;
	i = indices[nextufs_stress_rand_range(ctx, count)];
	if (strlen(ctx->paths[i].path) + 1 > path_out_size)
		return -ENAMETOOLONG;
	memcpy(path_out, ctx->paths[i].path, strlen(ctx->paths[i].path) + 1);
	return 0;
}

static int
nextufs_stress_pick_nondir_path(struct nextufs_stress_ctx *ctx,
    char *path_out, size_t path_out_size)
{
	unsigned indices[NEXTUFS_STRESS_MAX_PATHS];
	size_t i;
	unsigned count = 0;

	for (i = 0; i < NEXTUFS_STRESS_MAX_PATHS; i++) {
		struct nextufs_stress_obj *obj;

		if (!ctx->paths[i].used)
			continue;
		obj = nextufs_stress_find_obj(ctx, ctx->paths[i].obj_id);
		if (obj == NULL || obj->type == NEXTUFS_STRESS_DIR)
			continue;
		indices[count++] = (unsigned)i;
	}
	if (count == 0)
		return -ENOENT;
	i = indices[nextufs_stress_rand_range(ctx, count)];
	if (strlen(ctx->paths[i].path) + 1 > path_out_size)
		return -ENAMETOOLONG;
	memcpy(path_out, ctx->paths[i].path, strlen(ctx->paths[i].path) + 1);
	return 0;
}

static int
nextufs_stress_pick_file_path(struct nextufs_stress_ctx *ctx, char *path_out,
    size_t path_out_size)
{
	return nextufs_stress_pick_path_of_type(ctx, NEXTUFS_STRESS_REG, 0,
	    path_out, path_out_size);
}

static int
nextufs_stress_pick_linkable_file_path(struct nextufs_stress_ctx *ctx,
    char *path_out, size_t path_out_size)
{
	return nextufs_stress_pick_path_of_type(ctx, NEXTUFS_STRESS_REG, 0,
	    path_out, path_out_size);
}

static int
nextufs_stress_make_unique_child(struct nextufs_stress_ctx *ctx,
    const char *parent, char *path_out, size_t path_out_size)
{
	char name[NEXTUFS_STRESS_MAX_NAME];
	unsigned tries;

	for (tries = 0; tries < 128; tries++) {
		if (nextufs_stress_random_name(ctx, name, sizeof(name)) < 0)
			return -ENAMETOOLONG;
		if (nextufs_stress_make_child_path(path_out, path_out_size, parent,
		    name) < 0)
			return -ENAMETOOLONG;
		if (nextufs_stress_find_path(ctx, path_out) == NULL)
			return 0;
	}
	return -EEXIST;
}

static void
nextufs_stress_fill_bytes(struct nextufs_stress_ctx *ctx, uint8_t *buf, size_t size)
{
	size_t i;

	for (i = 0; i < size; i++)
		buf[i] = (uint8_t)nextufs_stress_rand_range(ctx, 256);
}

static uint16_t
nextufs_stress_random_perms(struct nextufs_stress_ctx *ctx)
{
	uint16_t perms;

	perms = (uint16_t)nextufs_stress_rand_range(ctx, 010000);
	if (nextufs_stress_one_in(ctx, 4))
		perms |= 04000;
	if (nextufs_stress_one_in(ctx, 4))
		perms |= 02000;
	if (nextufs_stress_one_in(ctx, 4))
		perms |= 01000;
	return perms;
}

static uid_t
nextufs_stress_random_id(struct nextufs_stress_ctx *ctx)
{
	return (uid_t)nextufs_stress_rand_range(ctx, 32000);
}

static int
nextufs_stress_refresh_root(struct nextufs_stress_ctx *ctx)
{
	struct nextufs_image img;
	struct nextufs_node node;
	struct nextufs_stress_path *path_entry;
	struct nextufs_stress_obj *obj;
	int rc;

	rc = nextufs_image_open(&img, ctx->image_path);
	if (rc < 0)
		return rc;
	rc = nextufs_node_lookup(&img, ctx->root_path, 0, &node);
	nextufs_image_close(&img);
	if (rc < 0)
		return rc;
	path_entry = nextufs_stress_find_path(ctx, ctx->root_path);
	if (path_entry == NULL)
		return -ENOENT;
	obj = nextufs_stress_find_obj(ctx, path_entry->obj_id);
	if (obj == NULL)
		return -ENOENT;
	obj->mode = node.inode.mode;
	obj->uid = node.inode.uid;
	obj->gid = node.inode.gid;
	return 0;
}

static int
nextufs_stress_compare_regular(const struct nextufs_stress_obj *obj,
    const struct nextufs_image *img, const char *path)
{
	uint8_t *buf = NULL;
	size_t got = 0;
	int rc;

	if (obj->size != 0) {
		buf = malloc(obj->size);
		if (buf == NULL)
			return -ENOMEM;
	}
	rc = nextufs_path_read(img, path, 0, buf, obj->size, &got);
	if (rc < 0) {
		free(buf);
		return rc;
	}
	if (got != obj->size || (obj->size != 0 &&
	    memcmp(buf, obj->data, obj->size) != 0)) {
		free(buf);
		return -EINVAL;
	}
	free(buf);
	return 0;
}

static int
nextufs_stress_compare_symlink(const struct nextufs_stress_obj *obj,
    const struct nextufs_image *img, const char *path)
{
	char linkbuf[NEXTUFS_STRESS_MAX_PATH];
	int rc;

	rc = nextufs_path_readlink(img, path, linkbuf, sizeof(linkbuf));
	if (rc < 0)
		return rc;
	if (strcmp(linkbuf, obj->link_target) != 0)
		return -EINVAL;
	return 0;
}

static int
nextufs_stress_vec_push(struct nextufs_stress_path_vec *vec, const char *path)
{
	char **new_items;

	if (vec->count == vec->cap) {
		size_t new_cap = vec->cap == 0 ? 16 : vec->cap * 2;

		new_items = realloc(vec->items, new_cap * sizeof(vec->items[0]));
		if (new_items == NULL)
			return -ENOMEM;
		vec->items = new_items;
		vec->cap = new_cap;
	}
	vec->items[vec->count] = strdup(path);
	if (vec->items[vec->count] == NULL)
		return -ENOMEM;
	vec->count++;
	return 0;
}

static void
nextufs_stress_vec_free(struct nextufs_stress_path_vec *vec)
{
	size_t i;

	for (i = 0; i < vec->count; i++)
		free(vec->items[i]);
	free(vec->items);
	memset(vec, 0, sizeof(*vec));
}

struct nextufs_stress_walk_ctx {
	const struct nextufs_image *img;
	struct nextufs_stress_path_vec *vec;
	char parent[NEXTUFS_STRESS_MAX_PATH];
};

static int
nextufs_stress_walk_tree(const struct nextufs_image *img, const char *path,
    struct nextufs_stress_path_vec *vec);

static int
nextufs_stress_walk_cb(const struct nextufs_node *node, const char *name,
    size_t name_len, void *ctx_ptr)
{
	struct nextufs_stress_walk_ctx *ctx = ctx_ptr;
	char child[NEXTUFS_STRESS_MAX_PATH];
	char namebuf[NEXTUFS_STRESS_MAX_NAME];

	(void)node;
	if ((name_len == 1 && name[0] == '.') ||
	    (name_len == 2 && name[0] == '.' && name[1] == '.'))
		return 0;
	if (name_len + 1 > sizeof(namebuf))
		return -ENAMETOOLONG;
	memcpy(namebuf, name, name_len);
	namebuf[name_len] = '\0';
	if (nextufs_stress_make_child_path(child, sizeof(child), ctx->parent,
	    namebuf) < 0)
		return -ENAMETOOLONG;
	return nextufs_stress_walk_tree(ctx->img, child, ctx->vec);
}

static int
nextufs_stress_walk_tree(const struct nextufs_image *img, const char *path,
    struct nextufs_stress_path_vec *vec)
{
	struct nextufs_node node;
	struct nextufs_stress_walk_ctx walk_ctx;
	int rc;

	rc = nextufs_stress_vec_push(vec, path);
	if (rc < 0)
		return rc;
	rc = nextufs_node_lookup(img, path, 0, &node);
	if (rc < 0)
		return rc;
	if (!nextufs_node_is_dir(&node))
		return 0;
	memset(&walk_ctx, 0, sizeof(walk_ctx));
	walk_ctx.img = img;
	walk_ctx.vec = vec;
	if (strlen(path) + 1 > sizeof(walk_ctx.parent))
		return -ENAMETOOLONG;
	memcpy(walk_ctx.parent, path, strlen(path) + 1);
	return nextufs_directory_iterate_nodes_path(img, path, 0,
	    nextufs_stress_walk_cb, &walk_ctx);
}

static int
nextufs_stress_cmp_str(const void *a, const void *b)
{
	const char *const *sa = a;
	const char *const *sb = b;

	return strcmp(*sa, *sb);
}

static int
nextufs_stress_validate_tree(struct nextufs_stress_ctx *ctx,
    const struct nextufs_image *img)
{
	struct nextufs_stress_path_vec vec = { 0 };
	char *expected[NEXTUFS_STRESS_MAX_PATHS];
	size_t expected_count = 0;
	size_t i;
	int rc;

	rc = nextufs_stress_walk_tree(img, ctx->root_path, &vec);
	if (rc < 0) {
		nextufs_stress_vec_free(&vec);
		return rc;
	}
	for (i = 0; i < NEXTUFS_STRESS_MAX_PATHS; i++) {
		if (!ctx->paths[i].used)
			continue;
		expected[expected_count++] = ctx->paths[i].path;
	}
	qsort(vec.items, vec.count, sizeof(vec.items[0]), nextufs_stress_cmp_str);
	qsort(expected, expected_count, sizeof(expected[0]), nextufs_stress_cmp_str);
	if (vec.count != expected_count) {
		nextufs_stress_vec_free(&vec);
		return -EINVAL;
	}
	for (i = 0; i < vec.count; i++) {
		if (strcmp(vec.items[i], expected[i]) != 0) {
			nextufs_stress_vec_free(&vec);
			return -EINVAL;
		}
	}
	nextufs_stress_vec_free(&vec);
	return 0;
}

static int
nextufs_stress_validate(struct nextufs_stress_ctx *ctx)
{
	struct nextufs_image img;
	size_t i;
	int rc;

	rc = nextufs_image_open(&img, ctx->image_path);
	if (rc < 0)
		return rc;
	rc = nextufs_stress_validate_tree(ctx, &img);
	if (rc < 0) {
		nextufs_image_close(&img);
		return rc;
	}
	for (i = 0; i < NEXTUFS_STRESS_MAX_PATHS; i++) {
		struct nextufs_stress_obj *obj;
		struct nextufs_node node;
		struct stat st;

		if (!ctx->paths[i].used)
			continue;
		obj = nextufs_stress_find_obj(ctx, ctx->paths[i].obj_id);
		if (obj == NULL) {
			nextufs_image_close(&img);
			return -ENOENT;
		}
		rc = nextufs_node_lookup(&img, ctx->paths[i].path, 0, &node);
		if (rc < 0) {
			nextufs_image_close(&img);
			return rc;
		}
		rc = nextufs_node_stat(&node, &st);
		if (rc < 0) {
			nextufs_image_close(&img);
			return rc;
		}
		if (nextufs_stress_mode_type(st.st_mode) != obj->type ||
		    (st.st_mode & 07777) != (obj->mode & 07777) ||
		    st.st_uid != obj->uid || st.st_gid != obj->gid) {
			nextufs_image_close(&img);
			return -EINVAL;
		}
		if (obj->type == NEXTUFS_STRESS_REG) {
			if ((size_t)st.st_size != obj->size) {
				nextufs_image_close(&img);
				return -EINVAL;
			}
			if (obj->refcount != 0 && st.st_nlink != obj->refcount) {
				nextufs_image_close(&img);
				return -EINVAL;
			}
			rc = nextufs_stress_compare_regular(obj, &img,
			    ctx->paths[i].path);
			if (rc < 0) {
				nextufs_image_close(&img);
				return rc;
			}
		} else if (obj->type == NEXTUFS_STRESS_LNK) {
			rc = nextufs_stress_compare_symlink(obj, &img,
			    ctx->paths[i].path);
			if (rc < 0) {
				nextufs_image_close(&img);
				return rc;
			}
		} else if (obj->type == NEXTUFS_STRESS_CHR ||
		    obj->type == NEXTUFS_STRESS_BLK) {
			if ((uint32_t)st.st_rdev != obj->rdev) {
				nextufs_image_close(&img);
				return -EINVAL;
			}
		}
	}
	nextufs_image_close(&img);
	return 0;
}

static int
nextufs_stress_create_root(struct nextufs_stress_ctx *ctx)
{
	struct nextufs_image img;
	struct nextufs_node node;
	unsigned obj_id;
	int rc;

	rc = nextufs_path_mkdir(&ctx->write_ctx, ctx->image_path, ctx->root_path, 0777);
	if (rc < 0)
		return rc;
	rc = nextufs_image_open(&img, ctx->image_path);
	if (rc < 0)
		return rc;
	rc = nextufs_node_lookup(&img, ctx->root_path, 0, &node);
	nextufs_image_close(&img);
	if (rc < 0)
		return rc;
	rc = nextufs_stress_add_object(ctx, NEXTUFS_STRESS_DIR, node.inode.mode,
	    node.inode.uid, node.inode.gid, 0, NULL, 0, NULL, &obj_id);
	if (rc < 0)
		return rc;
	rc = nextufs_stress_add_path(ctx, ctx->root_path, obj_id);
	if (rc < 0)
		return rc;
	nextufs_stress_find_obj(ctx, obj_id)->refcount = 1;
	return 0;
}

static int
nextufs_stress_create_file_op(struct nextufs_stress_ctx *ctx)
{
	char parent[NEXTUFS_STRESS_MAX_PATH];
	char path[NEXTUFS_STRESS_MAX_PATH];
	uint8_t *data = NULL;
	size_t size;
	unsigned obj_id;
	int rc;

	rc = nextufs_stress_pick_dir_path(ctx, 0, parent, sizeof(parent));
	if (rc < 0)
		return rc;
	rc = nextufs_stress_make_unique_child(ctx, parent, path, sizeof(path));
	if (rc < 0)
		return rc;
	size = nextufs_stress_one_in(ctx, 10) ? 65536 + nextufs_stress_rand_range(ctx, 32768) :
	    nextufs_stress_rand_range(ctx, 4096);
	if (size != 0) {
		data = malloc(size);
		if (data == NULL)
			return -ENOMEM;
		nextufs_stress_fill_bytes(ctx, data, size);
	}
	nextufs_stress_set_op(ctx, "create-file %s size=%zu", path, size);
	rc = nextufs_path_create_file(&ctx->write_ctx, ctx->image_path, path, data,
	    size);
	if (rc < 0) {
		free(data);
		return rc;
	}
	rc = nextufs_stress_add_object(ctx, NEXTUFS_STRESS_REG,
	    NEXTUFS_IFREG | 0644, ctx->write_ctx.uid, ctx->write_ctx.gid, 0,
	    data, size, NULL, &obj_id);
	free(data);
	if (rc < 0)
		return rc;
	rc = nextufs_stress_add_path(ctx, path, obj_id);
	if (rc < 0)
		return rc;
	nextufs_stress_find_obj(ctx, obj_id)->refcount = 1;
	return 0;
}

static int
nextufs_stress_mkdir_op(struct nextufs_stress_ctx *ctx)
{
	char parent[NEXTUFS_STRESS_MAX_PATH];
	char path[NEXTUFS_STRESS_MAX_PATH];
	uint16_t perms;
	unsigned obj_id;
	int rc;

	rc = nextufs_stress_pick_dir_path(ctx, 0, parent, sizeof(parent));
	if (rc < 0)
		return rc;
	rc = nextufs_stress_make_unique_child(ctx, parent, path, sizeof(path));
	if (rc < 0)
		return rc;
	perms = nextufs_stress_random_perms(ctx) & 0777;
	nextufs_stress_set_op(ctx, "mkdir %s mode=%04o", path, perms);
	rc = nextufs_path_mkdir(&ctx->write_ctx, ctx->image_path, path, perms);
	if (rc < 0)
		return rc;
	rc = nextufs_stress_add_object(ctx, NEXTUFS_STRESS_DIR,
	    NEXTUFS_IFDIR | perms, ctx->write_ctx.uid, ctx->write_ctx.gid, 0,
	    NULL, 0, NULL, &obj_id);
	if (rc < 0)
		return rc;
	rc = nextufs_stress_add_path(ctx, path, obj_id);
	if (rc < 0)
		return rc;
	nextufs_stress_find_obj(ctx, obj_id)->refcount = 1;
	return nextufs_stress_refresh_root(ctx);
}

static int
nextufs_stress_symlink_op(struct nextufs_stress_ctx *ctx)
{
	char parent[NEXTUFS_STRESS_MAX_PATH];
	char path[NEXTUFS_STRESS_MAX_PATH];
	char target[NEXTUFS_STRESS_MAX_PATH];
	unsigned obj_id;
	int rc;

	rc = nextufs_stress_pick_dir_path(ctx, 0, parent, sizeof(parent));
	if (rc < 0)
		return rc;
	rc = nextufs_stress_make_unique_child(ctx, parent, path, sizeof(path));
	if (rc < 0)
		return rc;
	if (nextufs_stress_one_in(ctx, 2) && nextufs_stress_path_count(ctx) > 1) {
		char existing[NEXTUFS_STRESS_MAX_PATH];

		rc = nextufs_stress_pick_nondir_path(ctx, existing, sizeof(existing));
		if (rc < 0)
			return rc;
		if (nextufs_stress_one_in(ctx, 2))
			snprintf(target, sizeof(target), "%s", existing);
		else
			snprintf(target, sizeof(target), "%s", strrchr(existing, '/') + 1);
	} else {
		snprintf(target, sizeof(target), "dangling-%08x",
		    nextufs_stress_rand_range(ctx, UINT_MAX));
	}
	nextufs_stress_set_op(ctx, "symlink %s -> %s", path, target);
	rc = nextufs_path_symlink(&ctx->write_ctx, ctx->image_path, target, path);
	if (rc < 0)
		return rc;
	rc = nextufs_stress_add_object(ctx, NEXTUFS_STRESS_LNK,
	    NEXTUFS_IFLNK | 0777, ctx->write_ctx.uid, ctx->write_ctx.gid, 0,
	    NULL, 0, target, &obj_id);
	if (rc < 0)
		return rc;
	rc = nextufs_stress_add_path(ctx, path, obj_id);
	if (rc < 0)
		return rc;
	nextufs_stress_find_obj(ctx, obj_id)->refcount = 1;
	return 0;
}

static int
nextufs_stress_mknod_op(struct nextufs_stress_ctx *ctx)
{
	char parent[NEXTUFS_STRESS_MAX_PATH];
	char path[NEXTUFS_STRESS_MAX_PATH];
	enum nextufs_stress_type type;
	uint16_t mode;
	uint32_t rdev = 0;
	unsigned obj_id;
	int rc;

	rc = nextufs_stress_pick_dir_path(ctx, 0, parent, sizeof(parent));
	if (rc < 0)
		return rc;
	rc = nextufs_stress_make_unique_child(ctx, parent, path, sizeof(path));
	if (rc < 0)
		return rc;
	switch (nextufs_stress_rand_range(ctx, 3)) {
	case 0:
		type = NEXTUFS_STRESS_FIFO;
		mode = NEXTUFS_IFIFO | (nextufs_stress_random_perms(ctx) & 0777);
		break;
	case 1:
		type = NEXTUFS_STRESS_CHR;
		mode = NEXTUFS_IFCHR | (nextufs_stress_random_perms(ctx) & 0777);
		rdev = (uint32_t)nextufs_stress_rand_range(ctx, 256);
		break;
	default:
		type = NEXTUFS_STRESS_BLK;
		mode = NEXTUFS_IFBLK | (nextufs_stress_random_perms(ctx) & 0777);
		rdev = (uint32_t)nextufs_stress_rand_range(ctx, 256);
		break;
	}
	nextufs_stress_set_op(ctx, "mknod %s mode=%06o rdev=%" PRIu32, path, mode,
	    rdev);
	rc = nextufs_path_mknod(&ctx->write_ctx, ctx->image_path, path, mode, rdev);
	if (rc < 0)
		return rc;
	rc = nextufs_stress_add_object(ctx, type, mode, ctx->write_ctx.uid,
	    ctx->write_ctx.gid, rdev, NULL, 0, NULL, &obj_id);
	if (rc < 0)
		return rc;
	rc = nextufs_stress_add_path(ctx, path, obj_id);
	if (rc < 0)
		return rc;
	nextufs_stress_find_obj(ctx, obj_id)->refcount = 1;
	return 0;
}

static int
nextufs_stress_overwrite_op(struct nextufs_stress_ctx *ctx)
{
	char path[NEXTUFS_STRESS_MAX_PATH];
	struct nextufs_stress_path *entry;
	struct nextufs_stress_obj *obj;
	uint8_t *data = NULL;
	size_t size;
	int rc;

	rc = nextufs_stress_pick_file_path(ctx, path, sizeof(path));
	if (rc < 0)
		return rc;
	entry = nextufs_stress_find_path(ctx, path);
	obj = nextufs_stress_find_obj(ctx, entry->obj_id);
	if (obj == NULL)
		return -ENOENT;
	size = nextufs_stress_one_in(ctx, 8) ? 32768 + nextufs_stress_rand_range(ctx, 32768) :
	    nextufs_stress_rand_range(ctx, 4096);
	if (size != 0) {
		data = malloc(size);
		if (data == NULL)
			return -ENOMEM;
		nextufs_stress_fill_bytes(ctx, data, size);
	}
	nextufs_stress_set_op(ctx, "overwrite %s size=%zu", path, size);
	rc = nextufs_path_overwrite_file(&ctx->write_ctx, ctx->image_path, path,
	    data, size);
	if (rc < 0) {
		free(data);
		return rc;
	}
	free(obj->data);
	obj->data = data;
	obj->size = size;
	return 0;
}

static int
nextufs_stress_append_op(struct nextufs_stress_ctx *ctx)
{
	char path[NEXTUFS_STRESS_MAX_PATH];
	struct nextufs_stress_path *entry;
	struct nextufs_stress_obj *obj;
	uint8_t *tail = NULL;
	size_t tail_size;
	int rc;

	rc = nextufs_stress_pick_file_path(ctx, path, sizeof(path));
	if (rc < 0)
		return rc;
	entry = nextufs_stress_find_path(ctx, path);
	obj = nextufs_stress_find_obj(ctx, entry->obj_id);
	if (obj == NULL)
		return -ENOENT;
	tail_size = 1 + nextufs_stress_rand_range(ctx, 4096);
	if (obj->size + tail_size > NEXTUFS_STRESS_MAX_FILE)
		tail_size = NEXTUFS_STRESS_MAX_FILE - obj->size;
	if (tail_size == 0)
		return -EFBIG;
	tail = malloc(tail_size);
	if (tail == NULL)
		return -ENOMEM;
	nextufs_stress_fill_bytes(ctx, tail, tail_size);
	nextufs_stress_set_op(ctx, "append %s size=%zu", path, tail_size);
	rc = nextufs_path_append_file(&ctx->write_ctx, ctx->image_path, path, tail,
	    tail_size);
	if (rc < 0) {
		free(tail);
		return rc;
	}
	rc = nextufs_stress_resize_data(obj, obj->size + tail_size);
	if (rc < 0) {
		free(tail);
		return rc;
	}
	memcpy(obj->data + obj->size - tail_size, tail, tail_size);
	free(tail);
	return 0;
}

static int
nextufs_stress_pwrite_op(struct nextufs_stress_ctx *ctx)
{
	char path[NEXTUFS_STRESS_MAX_PATH];
	struct nextufs_stress_path *entry;
	struct nextufs_stress_obj *obj;
	uint8_t *buf = NULL;
	size_t size;
	uint64_t offset;
	int rc;

	rc = nextufs_stress_pick_file_path(ctx, path, sizeof(path));
	if (rc < 0)
		return rc;
	entry = nextufs_stress_find_path(ctx, path);
	obj = nextufs_stress_find_obj(ctx, entry->obj_id);
	if (obj == NULL)
		return -ENOENT;
	size = 1 + nextufs_stress_rand_range(ctx, 4096);
	offset = nextufs_stress_one_in(ctx, 4) ? obj->size + nextufs_stress_rand_range(ctx, 8192) :
	    (obj->size == 0 ? 0 : nextufs_stress_rand_range(ctx, (unsigned)obj->size + 1));
	if (offset + size > NEXTUFS_STRESS_MAX_FILE)
		size = NEXTUFS_STRESS_MAX_FILE - (size_t)offset;
	if (size == 0)
		return -EFBIG;
	buf = malloc(size);
	if (buf == NULL)
		return -ENOMEM;
	nextufs_stress_fill_bytes(ctx, buf, size);
	nextufs_stress_set_op(ctx, "pwrite %s off=%" PRIu64 " size=%zu", path,
	    offset, size);
	rc = nextufs_path_pwrite(&ctx->write_ctx, ctx->image_path, path, buf, size,
	    offset);
	if (rc < 0) {
		free(buf);
		return rc;
	}
	if (offset + size > obj->size) {
		rc = nextufs_stress_resize_data(obj, (size_t)(offset + size));
		if (rc < 0) {
			free(buf);
			return rc;
		}
	}
	memcpy(obj->data + offset, buf, size);
	free(buf);
	return 0;
}

static int
nextufs_stress_truncate_op(struct nextufs_stress_ctx *ctx)
{
	char path[NEXTUFS_STRESS_MAX_PATH];
	struct nextufs_stress_path *entry;
	struct nextufs_stress_obj *obj;
	size_t new_size;
	int rc;

	rc = nextufs_stress_pick_file_path(ctx, path, sizeof(path));
	if (rc < 0)
		return rc;
	entry = nextufs_stress_find_path(ctx, path);
	obj = nextufs_stress_find_obj(ctx, entry->obj_id);
	if (obj == NULL)
		return -ENOENT;
	new_size = nextufs_stress_one_in(ctx, 4) ? nextufs_stress_rand_range(ctx, NEXTUFS_STRESS_MAX_FILE) :
	    nextufs_stress_rand_range(ctx, 32768);
	nextufs_stress_set_op(ctx, "truncate %s size=%zu", path, new_size);
	rc = nextufs_path_truncate(&ctx->write_ctx, ctx->image_path, path, new_size);
	if (rc < 0)
		return rc;
	return nextufs_stress_resize_data(obj, new_size);
}

static int
nextufs_stress_chmod_op(struct nextufs_stress_ctx *ctx)
{
	char path[NEXTUFS_STRESS_MAX_PATH];
	struct nextufs_stress_path *entry;
	struct nextufs_stress_obj *obj;
	uint16_t perms;
	int rc;

	rc = nextufs_stress_pick_nondir_path(ctx, path, sizeof(path));
	if (rc < 0)
		return rc;
	entry = nextufs_stress_find_path(ctx, path);
	obj = nextufs_stress_find_obj(ctx, entry->obj_id);
	if (obj == NULL)
		return -ENOENT;
	perms = nextufs_stress_random_perms(ctx);
	nextufs_stress_set_op(ctx, "chmod %s mode=%04o", path, perms);
	rc = nextufs_path_chmod(&ctx->write_ctx, ctx->image_path, path, perms);
	if (rc < 0)
		return rc;
	obj->mode = (obj->mode & NEXTUFS_IFMT) | perms;
	return 0;
}

static int
nextufs_stress_chown_op(struct nextufs_stress_ctx *ctx)
{
	char path[NEXTUFS_STRESS_MAX_PATH];
	struct nextufs_stress_path *entry;
	struct nextufs_stress_obj *obj;
	uid_t uid;
	gid_t gid;
	int rc;

	rc = nextufs_stress_pick_nondir_path(ctx, path, sizeof(path));
	if (rc < 0)
		return rc;
	entry = nextufs_stress_find_path(ctx, path);
	obj = nextufs_stress_find_obj(ctx, entry->obj_id);
	if (obj == NULL)
		return -ENOENT;
	uid = nextufs_stress_random_id(ctx);
	gid = nextufs_stress_random_id(ctx);
	nextufs_stress_set_op(ctx, "chown %s uid=%u gid=%u", path, (unsigned)uid,
	    (unsigned)gid);
	rc = nextufs_path_chown(&ctx->write_ctx, ctx->image_path, path, uid, gid);
	if (rc < 0)
		return rc;
	obj->uid = uid;
	obj->gid = gid;
	return 0;
}

static int
nextufs_stress_utimes_op(struct nextufs_stress_ctx *ctx)
{
	char path[NEXTUFS_STRESS_MAX_PATH];
	uint32_t atime;
	uint32_t mtime;
	int rc;

	rc = nextufs_stress_pick_nondir_path(ctx, path, sizeof(path));
	if (rc < 0)
		return rc;
	atime = 1000000000U + (uint32_t)nextufs_stress_rand_range(ctx, 500000000U);
	mtime = atime + (uint32_t)nextufs_stress_rand_range(ctx, 500000000U);
	nextufs_stress_set_op(ctx, "utimes %s atime=%" PRIu32 " mtime=%" PRIu32,
	    path, atime, mtime);
	return nextufs_path_utimes(&ctx->write_ctx, ctx->image_path, path, atime,
	    mtime);
}

static int
nextufs_stress_rename_op(struct nextufs_stress_ctx *ctx)
{
	char src[NEXTUFS_STRESS_MAX_PATH];
	char dst_parent[NEXTUFS_STRESS_MAX_PATH];
	char dst[NEXTUFS_STRESS_MAX_PATH];
	struct nextufs_stress_path *entry;
	size_t i;
	size_t src_len;
	int rc;

	rc = nextufs_stress_pick_nondir_path(ctx, src, sizeof(src));
	if (rc < 0 && nextufs_stress_object_count(ctx, NEXTUFS_STRESS_DIR) > 1)
		rc = nextufs_stress_pick_path_of_type(ctx, NEXTUFS_STRESS_DIR, 1, src,
		    sizeof(src));
	if (rc < 0)
		return rc;
	rc = nextufs_stress_pick_dir_path(ctx, 0, dst_parent, sizeof(dst_parent));
	if (rc < 0)
		return rc;
	rc = nextufs_stress_make_unique_child(ctx, dst_parent, dst, sizeof(dst));
	if (rc < 0)
		return rc;
	if (nextufs_stress_path_has_prefix(dst_parent, src))
		return -EINVAL;
	nextufs_stress_set_op(ctx, "rename %s -> %s", src, dst);
	rc = nextufs_path_rename(&ctx->write_ctx, ctx->image_path, src, dst);
	if (rc < 0)
		return rc;
	entry = nextufs_stress_find_path(ctx, src);
	if (entry == NULL)
		return -ENOENT;
	free(entry->path);
	entry->path = strdup(dst);
	if (entry->path == NULL)
		return -ENOMEM;
	src_len = strlen(src);
	for (i = 0; i < NEXTUFS_STRESS_MAX_PATHS; i++) {
		char new_path[NEXTUFS_STRESS_MAX_PATH];

		if (!ctx->paths[i].used || ctx->paths[i] .path == NULL)
			continue;
		if (strcmp(ctx->paths[i].path, dst) == 0)
			continue;
		if (!nextufs_stress_path_has_prefix(ctx->paths[i].path, src))
			continue;
		snprintf(new_path, sizeof(new_path), "%s%s", dst,
		    ctx->paths[i].path + src_len);
		free(ctx->paths[i].path);
		ctx->paths[i].path = strdup(new_path);
		if (ctx->paths[i].path == NULL)
			return -ENOMEM;
	}
	return nextufs_stress_refresh_root(ctx);
}

static int
nextufs_stress_unlink_op(struct nextufs_stress_ctx *ctx)
{
	char path[NEXTUFS_STRESS_MAX_PATH];
	int rc;

	rc = nextufs_stress_pick_nondir_path(ctx, path, sizeof(path));
	if (rc < 0)
		return rc;
	nextufs_stress_set_op(ctx, "unlink %s", path);
	rc = nextufs_path_unlink(&ctx->write_ctx, ctx->image_path, path);
	if (rc < 0)
		return rc;
	return nextufs_stress_remove_path(ctx, path);
}

static int
nextufs_stress_rmdir_op(struct nextufs_stress_ctx *ctx)
{
	char path[NEXTUFS_STRESS_MAX_PATH];
	int rc;

	rc = nextufs_stress_pick_dir_path(ctx, 1, path, sizeof(path));
	if (rc < 0)
		return rc;
	if (strcmp(path, ctx->root_path) == 0)
		return -EINVAL;
	nextufs_stress_set_op(ctx, "rmdir %s", path);
	rc = nextufs_path_rmdir(&ctx->write_ctx, ctx->image_path, path);
	if (rc < 0)
		return rc;
	rc = nextufs_stress_remove_path(ctx, path);
	if (rc < 0)
		return rc;
	return nextufs_stress_refresh_root(ctx);
}

static int
nextufs_stress_link_op(struct nextufs_stress_ctx *ctx)
{
	char src[NEXTUFS_STRESS_MAX_PATH];
	char parent[NEXTUFS_STRESS_MAX_PATH];
	char dst[NEXTUFS_STRESS_MAX_PATH];
	struct nextufs_stress_path *src_entry;
	struct nextufs_stress_obj *obj;
	int rc;

	rc = nextufs_stress_pick_linkable_file_path(ctx, src, sizeof(src));
	if (rc < 0)
		return rc;
	rc = nextufs_stress_pick_dir_path(ctx, 0, parent, sizeof(parent));
	if (rc < 0)
		return rc;
	rc = nextufs_stress_make_unique_child(ctx, parent, dst, sizeof(dst));
	if (rc < 0)
		return rc;
	nextufs_stress_set_op(ctx, "link %s -> %s", src, dst);
	rc = nextufs_path_link(&ctx->write_ctx, ctx->image_path, src, dst);
	if (rc < 0)
		return rc;
	src_entry = nextufs_stress_find_path(ctx, src);
	if (src_entry == NULL)
		return -ENOENT;
	obj = nextufs_stress_find_obj(ctx, src_entry->obj_id);
	if (obj == NULL)
		return -ENOENT;
	rc = nextufs_stress_add_path(ctx, dst, src_entry->obj_id);
	if (rc < 0)
		return rc;
	obj->refcount++;
	return 0;
}

static int
nextufs_stress_apply_op(struct nextufs_stress_ctx *ctx)
{
	int choice;
	int rc;
	int have_files = nextufs_stress_object_count(ctx, NEXTUFS_STRESS_REG) > 0;
	int have_nondirs = nextufs_stress_path_count(ctx) >
	    nextufs_stress_object_count(ctx, NEXTUFS_STRESS_DIR);
	int have_empty_dirs = 0;

	{
		char dummy[NEXTUFS_STRESS_MAX_PATH];

		have_empty_dirs = nextufs_stress_pick_dir_path(ctx, 1, dummy,
		    sizeof(dummy)) == 0;
	}
	for (choice = 0; choice < 64; choice++) {
		switch (nextufs_stress_rand_range(ctx, 15)) {
		case 0:
			return nextufs_stress_create_file_op(ctx);
		case 1:
			return nextufs_stress_mkdir_op(ctx);
		case 2:
			if (have_files)
				return nextufs_stress_overwrite_op(ctx);
			break;
		case 3:
			if (have_files)
				return nextufs_stress_append_op(ctx);
			break;
		case 4:
			if (have_files)
				return nextufs_stress_pwrite_op(ctx);
			break;
		case 5:
			if (have_files)
				return nextufs_stress_truncate_op(ctx);
			break;
		case 6:
			return nextufs_stress_symlink_op(ctx);
		case 7:
			return nextufs_stress_mknod_op(ctx);
		case 8:
			if (have_nondirs)
				return nextufs_stress_chmod_op(ctx);
			break;
		case 9:
			if (have_nondirs)
				return nextufs_stress_chown_op(ctx);
			break;
		case 10:
			if (have_nondirs)
				return nextufs_stress_utimes_op(ctx);
			break;
		case 11:
			if (nextufs_stress_path_count(ctx) > 1)
				return nextufs_stress_rename_op(ctx);
			break;
		case 12:
			if (have_nondirs)
				return nextufs_stress_unlink_op(ctx);
			break;
		case 13:
			if (have_empty_dirs && nextufs_stress_object_count(ctx,
			    NEXTUFS_STRESS_DIR) > 1)
				return nextufs_stress_rmdir_op(ctx);
			break;
		case 14:
			if (have_files && nextufs_stress_path_count(ctx) <
			    NEXTUFS_STRESS_MAX_PATHS - 1)
				return nextufs_stress_link_op(ctx);
			break;
		}
	}
	rc = nextufs_stress_create_file_op(ctx);
	return rc < 0 ? nextufs_stress_mkdir_op(ctx) : rc;
}

static void
nextufs_stress_cleanup(struct nextufs_stress_ctx *ctx)
{
	size_t i;

	for (i = 0; i < NEXTUFS_STRESS_MAX_PATHS; i++)
		free(ctx->paths[i].path);
	for (i = 0; i < NEXTUFS_STRESS_MAX_OBJECTS; i++)
		nextufs_stress_drop_object(&ctx->objects[i]);
}

static int
nextufs_stress_run(struct nextufs_stress_ctx *ctx)
{
	unsigned i;
	int rc;

	rc = nextufs_stress_create_root(ctx);
	if (rc < 0)
		return rc;
	rc = nextufs_stress_validate(ctx);
	if (rc < 0)
		return rc;
	for (i = 0; i < ctx->op_count; i++) {
		ctx->current_op = i + 1;
		rc = nextufs_stress_apply_op(ctx);
		if (rc < 0) {
			fprintf(stderr, "nextufs_stress: op failed seed=%" PRIu64
			    " step=%u rc=%d op=%s\n", ctx->seed, ctx->current_op, rc,
			    ctx->last_op[0] != '\0' ? ctx->last_op : "<unset>");
			return rc;
		}
		rc = nextufs_stress_validate(ctx);
		if (rc < 0) {
			fprintf(stderr, "nextufs_stress: validation failed seed=%" PRIu64
			    " step=%u rc=%d op=%s\n", ctx->seed, ctx->current_op, rc,
			    ctx->last_op);
			return rc;
		}
	}
	return 0;
}

static int
nextufs_stress_parse_u64(const char *s, uint64_t *out)
{
	char *end;
	unsigned long long v;

	errno = 0;
	v = strtoull(s, &end, 0);
	if (errno != 0 || end == s || *end != '\0')
		return -1;
	*out = (uint64_t)v;
	return 0;
}

static int
nextufs_stress_parse_u32(const char *s, unsigned *out)
{
	char *end;
	unsigned long v;

	errno = 0;
	v = strtoul(s, &end, 0);
	if (errno != 0 || end == s || *end != '\0' || v > UINT_MAX)
		return -1;
	*out = (unsigned)v;
	return 0;
}

int
main(int argc, char **argv)
{
	struct nextufs_stress_ctx ctx;
	const char *image_path = NULL;
	int i;
	int rc;

	memset(&ctx, 0, sizeof(ctx));
	ctx.seed = 0x4e455854554653ULL;
	ctx.op_count = 200;
	ctx.verbose = 1;
	ctx.write_ctx.policy = NEXTUFS_WRITE_EDITOR;
	ctx.write_ctx.uid = 0;
	ctx.write_ctx.gid = 0;
	ctx.write_ctx.groups = NULL;
	ctx.write_ctx.group_count = 0;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--seed") == 0) {
			if (i + 1 >= argc || nextufs_stress_parse_u64(argv[i + 1],
			    &ctx.seed) < 0) {
				fprintf(stderr, "nextufs_stress: invalid seed\n");
				return 2;
			}
			i++;
			continue;
		}
		if (strcmp(argv[i], "--ops") == 0) {
			if (i + 1 >= argc || nextufs_stress_parse_u32(argv[i + 1],
			    &ctx.op_count) < 0) {
				fprintf(stderr, "nextufs_stress: invalid op count\n");
				return 2;
			}
			i++;
			continue;
		}
		if (strcmp(argv[i], "--root") == 0) {
			if (i + 1 >= argc ||
			    strlen(argv[i + 1]) + 1 > sizeof(ctx.root_path) ||
			    argv[i + 1][0] != '/') {
				fprintf(stderr, "nextufs_stress: invalid root path\n");
				return 2;
			}
			memcpy(ctx.root_path, argv[i + 1], strlen(argv[i + 1]) + 1);
			i++;
			continue;
		}
		if (strcmp(argv[i], "--quiet") == 0) {
			ctx.verbose = 0;
			continue;
		}
		if (strcmp(argv[i], "--policy") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "nextufs_stress: missing policy\n");
				return 2;
			}
			if (strcmp(argv[i + 1], "editor") == 0)
				ctx.write_ctx.policy = NEXTUFS_WRITE_EDITOR;
			else if (strcmp(argv[i + 1], "permissions") == 0)
				ctx.write_ctx.policy = NEXTUFS_WRITE_PERMISSIONS;
			else {
				fprintf(stderr, "nextufs_stress: invalid policy\n");
				return 2;
			}
			i++;
			continue;
		}
		if (image_path == NULL) {
			image_path = argv[i];
			continue;
		}
		fprintf(stderr,
		    "usage: %s [--seed n] [--ops n] [--root /path] [--policy editor|permissions] [--quiet] <image>\n",
		    argv[0]);
		return 2;
	}
	if (image_path == NULL) {
		fprintf(stderr,
		    "usage: %s [--seed n] [--ops n] [--root /path] [--policy editor|permissions] [--quiet] <image>\n",
		    argv[0]);
		return 2;
	}
	if (ctx.root_path[0] == '\0') {
		snprintf(ctx.root_path, sizeof(ctx.root_path), "/nextufs-stress-%08x",
		    (unsigned)ctx.seed);
	}
	ctx.image_path = image_path;
	rc = nextufs_stress_run(&ctx);
	if (rc < 0) {
		fprintf(stderr,
		    "nextufs_stress: FAILED seed=%" PRIu64 " steps=%u root=%s last-op=%s rc=%d\n",
		    ctx.seed, ctx.current_op, ctx.root_path,
		    ctx.last_op[0] != '\0' ? ctx.last_op : "<unset>", rc);
		nextufs_stress_cleanup(&ctx);
		return 1;
	}
	printf("nextufs_stress: ok seed=%" PRIu64 " ops=%u root=%s\n",
	    ctx.seed, ctx.op_count, ctx.root_path);
	nextufs_stress_cleanup(&ctx);
	return 0;
}
