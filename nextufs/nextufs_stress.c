#include "nextufs.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define NEXTUFS_STRESS_MAX_OBJECTS 512
#define NEXTUFS_STRESS_MAX_PATHS 1024
#define NEXTUFS_STRESS_MAX_NAME 240
#define NEXTUFS_STRESS_MAX_PATH 4096
#define NEXTUFS_STRESS_MAX_FILE (256U * 1024U)
#define NEXTUFS_STRESS_MAX_OP_DESC 512
#define NEXTUFS_STRESS_WAIT_TRIES 20
#define NEXTUFS_STRESS_BATCH_DEFAULT 1

enum nextufs_stress_type {
	NEXTUFS_STRESS_DIR = 0,
	NEXTUFS_STRESS_REG = 1,
	NEXTUFS_STRESS_LNK = 2,
	NEXTUFS_STRESS_FIFO = 3,
	NEXTUFS_STRESS_CHR = 4,
	NEXTUFS_STRESS_BLK = 5,
};

enum nextufs_stress_backend {
	NEXTUFS_STRESS_BACKEND_OFFLINE = 0,
	NEXTUFS_STRESS_BACKEND_FUSE = 1,
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

struct nextufs_stress_log {
	char **items;
	size_t count;
	size_t cap;
};

struct nextufs_stress_snapshot {
	struct nextufs_stress_obj objects[NEXTUFS_STRESS_MAX_OBJECTS];
	struct nextufs_stress_path paths[NEXTUFS_STRESS_MAX_PATHS];
	unsigned next_obj_id;
};

struct nextufs_stress_path_vec {
	char **items;
	size_t count;
	size_t cap;
};

struct nextufs_stress_ctx {
	const char *image_path;
	char image_path_buf[NEXTUFS_STRESS_MAX_PATH];
	const char *template_image_path;
	char root_path[NEXTUFS_STRESS_MAX_PATH];
	char exe_dir[NEXTUFS_STRESS_MAX_PATH];
	char fail_dir[NEXTUFS_STRESS_MAX_PATH];
	char mountpoint[NEXTUFS_STRESS_MAX_PATH];
	char last_op[NEXTUFS_STRESS_MAX_OP_DESC];
	struct nextufs_write_ctx write_ctx;
	enum nextufs_stress_backend backend;
	struct nextufs_stress_obj objects[NEXTUFS_STRESS_MAX_OBJECTS];
	struct nextufs_stress_path paths[NEXTUFS_STRESS_MAX_PATHS];
	struct nextufs_stress_log op_log;
	uint64_t seed;
	uint64_t rng;
	unsigned op_count;
	unsigned current_op;
	unsigned batch_count;
	unsigned next_obj_id;
	uint32_t block_size;
	uint32_t frag_size;
	uid_t create_uid;
	gid_t create_gid;
	pid_t fuse_pid;
	int verbose;
	int run_fsck;
	int keep_work_image;
};

struct nextufs_stress_walk_ctx {
	const struct nextufs_image *img;
	struct nextufs_stress_path_vec *vec;
	char parent[NEXTUFS_STRESS_MAX_PATH];
};

static uint64_t
nextufs_stress_rand64(struct nextufs_stress_ctx *ctx)
{
	uint64_t x = ctx->rng;

	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	ctx->rng = x;
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
nextufs_stress_log_free(struct nextufs_stress_log *log)
{
	size_t i;

	for (i = 0; i < log->count; i++)
		free(log->items[i]);
	free(log->items);
	memset(log, 0, sizeof(*log));
}

static int
nextufs_stress_log_push(struct nextufs_stress_log *log, const char *line)
{
	char **new_items;

	if (log->count == log->cap) {
		size_t new_cap = log->cap == 0 ? 32 : log->cap * 2;

		new_items = realloc(log->items, new_cap * sizeof(log->items[0]));
		if (new_items == NULL)
			return -ENOMEM;
		log->items = new_items;
		log->cap = new_cap;
	}
	log->items[log->count] = strdup(line);
	if (log->items[log->count] == NULL)
		return -ENOMEM;
	log->count++;
	return 0;
}

static int
nextufs_stress_write_log_file(const struct nextufs_stress_log *log,
    const char *path)
{
	FILE *fp;
	size_t i;

	fp = fopen(path, "w");
	if (fp == NULL)
		return -errno;
	for (i = 0; i < log->count; i++) {
		if (fprintf(fp, "%s\n", log->items[i]) < 0) {
			fclose(fp);
			return -EIO;
		}
	}
	if (fclose(fp) != 0)
		return -errno;
	return 0;
}

static int
nextufs_stress_set_op(struct nextufs_stress_ctx *ctx, const char *fmt, ...)
{
	va_list ap;
	int rc;

	va_start(ap, fmt);
	vsnprintf(ctx->last_op, sizeof(ctx->last_op), fmt, ap);
	va_end(ap);
	if (ctx->verbose)
		printf("[%03u] %s\n", ctx->current_op, ctx->last_op);
	rc = nextufs_stress_log_push(&ctx->op_log, ctx->last_op);
	return rc;
}

static void
nextufs_stress_drop_object(struct nextufs_stress_obj *obj)
{
	free(obj->data);
	free(obj->link_target);
	memset(obj, 0, sizeof(*obj));
}

static void
nextufs_stress_cleanup(struct nextufs_stress_ctx *ctx)
{
	size_t i;

	for (i = 0; i < NEXTUFS_STRESS_MAX_PATHS; i++)
		free(ctx->paths[i].path);
	for (i = 0; i < NEXTUFS_STRESS_MAX_OBJECTS; i++)
		nextufs_stress_drop_object(&ctx->objects[i]);
	nextufs_stress_log_free(&ctx->op_log);
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
nextufs_stress_join_host_path(const char *mountpoint, const char *path,
    char *out, size_t out_size)
{
	int n;

	n = snprintf(out, out_size, "%s%s", mountpoint, path);
	if (n < 0 || (size_t)n >= out_size)
		return -ENAMETOOLONG;
	return 0;
}

static int
nextufs_stress_join_suffix(const char *prefix, const char *suffix,
    char *out, size_t out_size)
{
	int n;

	n = snprintf(out, out_size, "%s%s", prefix, suffix);
	if (n < 0 || (size_t)n >= out_size)
		return -ENAMETOOLONG;
	return 0;
}

static int
nextufs_stress_copy_file(const char *src, const char *dst)
{
	int src_fd;
	int dst_fd;
	uint8_t buf[65536];
	int rc = 0;

	src_fd = open(src, O_RDONLY);
	if (src_fd < 0)
		return -errno;
	dst_fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (dst_fd < 0) {
		close(src_fd);
		return -errno;
	}
	for (;;) {
		ssize_t nread;

		nread = read(src_fd, buf, sizeof(buf));
		if (nread < 0) {
			rc = -errno;
			break;
		}
		if (nread == 0)
			break;
		{
			ssize_t off = 0;

			while (off < nread) {
				ssize_t nw = write(dst_fd, buf + off,
				    (size_t)(nread - off));
				if (nw < 0) {
					rc = -errno;
					break;
				}
				off += nw;
			}
			if (rc < 0)
				break;
		}
	}
	if (close(dst_fd) != 0 && rc == 0)
		rc = -errno;
	if (close(src_fd) != 0 && rc == 0)
		rc = -errno;
	return rc;
}

static int
nextufs_stress_ensure_dir(const char *path)
{
	struct stat st;

	if (stat(path, &st) == 0)
		return S_ISDIR(st.st_mode) ? 0 : -ENOTDIR;
	if (errno != ENOENT)
		return -errno;
	if (mkdir(path, 0777) < 0)
		return -errno;
	return 0;
}

static int
nextufs_stress_exec_wait_timeout(char *const argv[], unsigned timeout_ms)
{
	pid_t pid;
	int status;
	unsigned waited = 0;

	pid = fork();
	if (pid < 0)
		return -errno;
	if (pid == 0) {
		execv(argv[0], argv);
		_exit(127);
	}
	while (waited < timeout_ms) {
		pid_t got = waitpid(pid, &status, WNOHANG);

		if (got < 0)
			return -errno;
		if (got == pid) {
			if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
				return 0;
			return -EIO;
		}
		usleep(100000);
		waited += 100;
	}
	(void)kill(pid, SIGKILL);
	(void)waitpid(pid, &status, 0);
	return -ETIMEDOUT;
}

static int
nextufs_stress_exec_wait_redirect(char *const argv[], const char *log_path)
{
	pid_t pid;
	int status;

	pid = fork();
	if (pid < 0)
		return -errno;
	if (pid == 0) {
		int fd = open(log_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);

		if (fd < 0)
			_exit(127);
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);
		close(fd);
		execv(argv[0], argv);
		_exit(127);
	}
	if (waitpid(pid, &status, 0) < 0)
		return -errno;
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
		return 0;
	return -EIO;
}

static int
nextufs_stress_mount_active(const char *mountpoint)
{
	FILE *fp;
	char line[8192];
	char resolved[PATH_MAX];

	if (realpath(mountpoint, resolved) == NULL)
		return 0;
	fp = fopen("/proc/self/mountinfo", "r");
	if (fp == NULL)
		return 0;
	while (fgets(line, sizeof(line), fp) != NULL) {
		char *sep;
		char *field;
		unsigned field_no = 0;

		sep = strstr(line, " - ");
		if (sep == NULL)
			continue;
		*sep = '\0';
		field = strtok(line, " ");
		while (field != NULL) {
			field_no++;
			if (field_no == 5 && strcmp(field, resolved) == 0) {
				fclose(fp);
				return 1;
			}
			field = strtok(NULL, " ");
		}
	}
	fclose(fp);
	return 0;
}

static int
nextufs_stress_resolve_existing_path(const char *path, char *out, size_t out_size)
{
	char *resolved;

	resolved = realpath(path, out);
	if (resolved == NULL)
		return -errno;
	if (strlen(out) + 1 > out_size)
		return -ENAMETOOLONG;
	return 0;
}

static int
nextufs_stress_find_tools(struct nextufs_stress_ctx *ctx)
{
	ssize_t n;
	char exe_path[NEXTUFS_STRESS_MAX_PATH];
	char *slash;

	n = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
	if (n < 0)
		return -errno;
	exe_path[n] = '\0';
	slash = strrchr(exe_path, '/');
	if (slash == NULL)
		return -EINVAL;
	*slash = '\0';
	if (strlen(exe_path) + 1 > sizeof(ctx->exe_dir))
		return -ENAMETOOLONG;
	memcpy(ctx->exe_dir, exe_path, strlen(exe_path) + 1);
	return 0;
}

static int
nextufs_stress_fuse_start(struct nextufs_stress_ctx *ctx)
{
	char fuse_path[NEXTUFS_STRESS_MAX_PATH];
	char scratch_path[NEXTUFS_STRESS_MAX_PATH];
	char template[NEXTUFS_STRESS_MAX_PATH];
	char *argv[9];
	unsigned tries;

	if (snprintf(fuse_path, sizeof(fuse_path), "%s/nextufs",
	    ctx->exe_dir) >= (int)sizeof(fuse_path))
		return -ENAMETOOLONG;
	if (snprintf(scratch_path, sizeof(scratch_path), "%s/.scratch",
	    ctx->exe_dir) >= (int)sizeof(scratch_path))
		return -ENAMETOOLONG;
	if (nextufs_stress_ensure_dir(scratch_path) < 0)
		return -errno;
	if (snprintf(template, sizeof(template), "%s/.scratch/stress-mnt-XXXXXX",
	    ctx->exe_dir) >= (int)sizeof(template))
		return -ENAMETOOLONG;
	if (mkdtemp(template) == NULL)
		return -errno;
	if (strlen(template) + 1 > sizeof(ctx->mountpoint))
		return -ENAMETOOLONG;
	memcpy(ctx->mountpoint, template, strlen(template) + 1);
	ctx->fuse_pid = fork();
	if (ctx->fuse_pid < 0)
		return -errno;
	if (ctx->fuse_pid == 0) {
		argv[0] = fuse_path;
		argv[1] = "mount";
		argv[2] = (char *)ctx->image_path;
		argv[3] = ctx->mountpoint;
		argv[4] = "-f";
		argv[5] = "-s";
		argv[6] = "-o";
		argv[7] = ctx->write_ctx.policy == NEXTUFS_WRITE_SU ?
		    "rw,mode=su" : "rw,mode=user";
		argv[8] = NULL;
		execv(argv[0], argv);
		_exit(127);
	}
	for (tries = 0; tries < NEXTUFS_STRESS_WAIT_TRIES; tries++) {
		if (nextufs_stress_mount_active(ctx->mountpoint)) {
			usleep(100000);
			return 0;
		}
		usleep(100000);
	}
	return -EIO;
}

static int
nextufs_stress_fuse_stop(struct nextufs_stress_ctx *ctx)
{
	char *argv[4];
	int status;

	if (ctx->mountpoint[0] != '\0') {
		argv[0] = "fusermount3";
		argv[1] = "-uz";
		argv[2] = ctx->mountpoint;
		argv[3] = NULL;
		(void)nextufs_stress_exec_wait_timeout(argv, 3000);
	}
	if (ctx->fuse_pid > 0)
		(void)kill(ctx->fuse_pid, SIGTERM);
	if (ctx->fuse_pid > 0)
		(void)waitpid(ctx->fuse_pid, &status, 0);
	if (ctx->mountpoint[0] != '\0')
		(void)rmdir(ctx->mountpoint);
	ctx->mountpoint[0] = '\0';
	ctx->fuse_pid = 0;
	return 0;
}

static int
nextufs_stress_write_all(int fd, const void *buf, size_t size)
{
	const uint8_t *p = buf;
	size_t done = 0;

	while (done < size) {
		ssize_t nw = write(fd, p + done, size - done);

		if (nw < 0)
			return -errno;
		done += (size_t)nw;
	}
	return 0;
}

static int
nextufs_stress_pwrite_all(int fd, const void *buf, size_t size, off_t off)
{
	const uint8_t *p = buf;
	size_t done = 0;

	while (done < size) {
		ssize_t nw = pwrite(fd, p + done, size - done, off + (off_t)done);

		if (nw < 0)
			return -errno;
		done += (size_t)nw;
	}
	return 0;
}

static int
nextufs_stress_backend_create_file(struct nextufs_stress_ctx *ctx,
    const char *path, const void *data, size_t data_len)
{
	if (ctx->backend == NEXTUFS_STRESS_BACKEND_OFFLINE)
		return nextufs_path_create_file(&ctx->write_ctx, ctx->image_path, path,
		    data, data_len);
	else {
		char host_path[NEXTUFS_STRESS_MAX_PATH];
		int fd;
		int rc;

		rc = nextufs_stress_join_host_path(ctx->mountpoint, path, host_path,
		    sizeof(host_path));
		if (rc < 0)
			return rc;
		fd = open(host_path, O_CREAT | O_EXCL | O_WRONLY, 0644);
		if (fd < 0)
			return -errno;
		rc = nextufs_stress_write_all(fd, data, data_len);
		if (close(fd) != 0 && rc == 0)
			rc = -errno;
		return rc;
	}
}

static int
nextufs_stress_backend_mkdir(struct nextufs_stress_ctx *ctx, const char *path,
    uint16_t mode)
{
	if (ctx->backend == NEXTUFS_STRESS_BACKEND_OFFLINE)
		return nextufs_path_mkdir(&ctx->write_ctx, ctx->image_path, path, mode);
	else {
		char host_path[NEXTUFS_STRESS_MAX_PATH];
		int rc;

		rc = nextufs_stress_join_host_path(ctx->mountpoint, path, host_path,
		    sizeof(host_path));
		if (rc < 0)
			return rc;
		if (mkdir(host_path, mode & 0777) < 0)
			return -errno;
		return 0;
	}
}

static int
nextufs_stress_backend_symlink(struct nextufs_stress_ctx *ctx, const char *target,
    const char *link_path)
{
	if (ctx->backend == NEXTUFS_STRESS_BACKEND_OFFLINE)
		return nextufs_path_symlink(&ctx->write_ctx, ctx->image_path, target,
		    link_path);
	else {
		char host_path[NEXTUFS_STRESS_MAX_PATH];
		int rc;

		rc = nextufs_stress_join_host_path(ctx->mountpoint, link_path, host_path,
		    sizeof(host_path));
		if (rc < 0)
			return rc;
		if (symlink(target, host_path) < 0)
			return -errno;
		return 0;
	}
}

static int
nextufs_stress_backend_mknod(struct nextufs_stress_ctx *ctx, const char *path,
    uint16_t mode, uint32_t rdev)
{
	if (ctx->backend == NEXTUFS_STRESS_BACKEND_OFFLINE)
		return nextufs_path_mknod(&ctx->write_ctx, ctx->image_path, path, mode,
		    rdev);
	else {
		char host_path[NEXTUFS_STRESS_MAX_PATH];
		int rc;

		rc = nextufs_stress_join_host_path(ctx->mountpoint, path, host_path,
		    sizeof(host_path));
		if (rc < 0)
			return rc;
		if ((mode & NEXTUFS_IFMT) == NEXTUFS_IFIFO) {
			if (mkfifo(host_path, mode & 0777) < 0)
				return -errno;
			return 0;
		}
		if (mknod(host_path, mode, (dev_t)rdev) < 0)
			return -errno;
		return 0;
	}
}

static int
nextufs_stress_backend_overwrite(struct nextufs_stress_ctx *ctx, const char *path,
    const void *data, size_t data_len)
{
	if (ctx->backend == NEXTUFS_STRESS_BACKEND_OFFLINE)
		return nextufs_path_overwrite_file(&ctx->write_ctx, ctx->image_path,
		    path, data, data_len);
	else {
		char host_path[NEXTUFS_STRESS_MAX_PATH];
		int fd;
		int rc;

		rc = nextufs_stress_join_host_path(ctx->mountpoint, path, host_path,
		    sizeof(host_path));
		if (rc < 0)
			return rc;
		fd = open(host_path, O_WRONLY | O_TRUNC);
		if (fd < 0)
			return -errno;
		rc = nextufs_stress_write_all(fd, data, data_len);
		if (close(fd) != 0 && rc == 0)
			rc = -errno;
		return rc;
	}
}

static int
nextufs_stress_backend_append(struct nextufs_stress_ctx *ctx, const char *path,
    const void *data, size_t data_len)
{
	if (ctx->backend == NEXTUFS_STRESS_BACKEND_OFFLINE)
		return nextufs_path_append_file(&ctx->write_ctx, ctx->image_path, path,
		    data, data_len);
	else {
		char host_path[NEXTUFS_STRESS_MAX_PATH];
		struct stat st;
		int fd;
		int rc;

		rc = nextufs_stress_join_host_path(ctx->mountpoint, path, host_path,
		    sizeof(host_path));
		if (rc < 0)
			return rc;
		if (stat(host_path, &st) < 0)
			return -errno;
		fd = open(host_path, O_WRONLY);
		if (fd < 0)
			return -errno;
		rc = nextufs_stress_pwrite_all(fd, data, data_len, st.st_size);
		if (close(fd) != 0 && rc == 0)
			rc = -errno;
		return rc;
	}
}

static int
nextufs_stress_backend_pwrite(struct nextufs_stress_ctx *ctx, const char *path,
    const void *data, size_t data_len, uint64_t offset)
{
	if (ctx->backend == NEXTUFS_STRESS_BACKEND_OFFLINE)
		return nextufs_path_pwrite(&ctx->write_ctx, ctx->image_path, path, data,
		    data_len, offset);
	else {
		char host_path[NEXTUFS_STRESS_MAX_PATH];
		int fd;
		int rc;

		rc = nextufs_stress_join_host_path(ctx->mountpoint, path, host_path,
		    sizeof(host_path));
		if (rc < 0)
			return rc;
		fd = open(host_path, O_WRONLY);
		if (fd < 0)
			return -errno;
		rc = nextufs_stress_pwrite_all(fd, data, data_len, (off_t)offset);
		if (close(fd) != 0 && rc == 0)
			rc = -errno;
		return rc;
	}
}

static int
nextufs_stress_backend_truncate(struct nextufs_stress_ctx *ctx, const char *path,
    uint64_t size)
{
	if (ctx->backend == NEXTUFS_STRESS_BACKEND_OFFLINE)
		return nextufs_path_truncate(&ctx->write_ctx, ctx->image_path, path,
		    size);
	else {
		char host_path[NEXTUFS_STRESS_MAX_PATH];
		int rc;

		rc = nextufs_stress_join_host_path(ctx->mountpoint, path, host_path,
		    sizeof(host_path));
		if (rc < 0)
			return rc;
		if (truncate(host_path, (off_t)size) < 0)
			return -errno;
		return 0;
	}
}

static int
nextufs_stress_backend_chmod(struct nextufs_stress_ctx *ctx, const char *path,
    uint16_t mode)
{
	if (ctx->backend == NEXTUFS_STRESS_BACKEND_OFFLINE)
		return nextufs_path_chmod(&ctx->write_ctx, ctx->image_path, path, mode);
	else {
		char host_path[NEXTUFS_STRESS_MAX_PATH];
		int rc;

		rc = nextufs_stress_join_host_path(ctx->mountpoint, path, host_path,
		    sizeof(host_path));
		if (rc < 0)
			return rc;
		if (chmod(host_path, mode & 07777) < 0)
			return -errno;
		return 0;
	}
}

static int
nextufs_stress_backend_chown(struct nextufs_stress_ctx *ctx, const char *path,
    uid_t uid, gid_t gid)
{
	if (ctx->backend == NEXTUFS_STRESS_BACKEND_OFFLINE)
		return nextufs_path_chown(&ctx->write_ctx, ctx->image_path, path, uid,
		    gid);
	else {
		char host_path[NEXTUFS_STRESS_MAX_PATH];
		int rc;

		rc = nextufs_stress_join_host_path(ctx->mountpoint, path, host_path,
		    sizeof(host_path));
		if (rc < 0)
			return rc;
		if (chown(host_path, uid, gid) < 0)
			return -errno;
		return 0;
	}
}

static int
nextufs_stress_backend_utimes(struct nextufs_stress_ctx *ctx, const char *path,
    uint32_t atime, uint32_t mtime)
{
	if (ctx->backend == NEXTUFS_STRESS_BACKEND_OFFLINE)
		return nextufs_path_utimes(&ctx->write_ctx, ctx->image_path, path,
		    atime, mtime);
	else {
		char host_path[NEXTUFS_STRESS_MAX_PATH];
		struct timespec ts[2];
		int rc;

		rc = nextufs_stress_join_host_path(ctx->mountpoint, path, host_path,
		    sizeof(host_path));
		if (rc < 0)
			return rc;
		ts[0].tv_sec = atime;
		ts[0].tv_nsec = 0;
		ts[1].tv_sec = mtime;
		ts[1].tv_nsec = 0;
		if (utimensat(AT_FDCWD, host_path, ts, 0) < 0)
			return -errno;
		return 0;
	}
}

static int
nextufs_stress_backend_rename(struct nextufs_stress_ctx *ctx, const char *src,
    const char *dst)
{
	if (ctx->backend == NEXTUFS_STRESS_BACKEND_OFFLINE)
		return nextufs_path_rename(&ctx->write_ctx, ctx->image_path, src, dst);
	else {
		char host_src[NEXTUFS_STRESS_MAX_PATH];
		char host_dst[NEXTUFS_STRESS_MAX_PATH];
		int rc;

		rc = nextufs_stress_join_host_path(ctx->mountpoint, src, host_src,
		    sizeof(host_src));
		if (rc < 0)
			return rc;
		rc = nextufs_stress_join_host_path(ctx->mountpoint, dst, host_dst,
		    sizeof(host_dst));
		if (rc < 0)
			return rc;
		if (rename(host_src, host_dst) < 0)
			return -errno;
		return 0;
	}
}

static int
nextufs_stress_backend_unlink(struct nextufs_stress_ctx *ctx, const char *path)
{
	if (ctx->backend == NEXTUFS_STRESS_BACKEND_OFFLINE)
		return nextufs_path_unlink(&ctx->write_ctx, ctx->image_path, path);
	else {
		char host_path[NEXTUFS_STRESS_MAX_PATH];
		int rc;

		rc = nextufs_stress_join_host_path(ctx->mountpoint, path, host_path,
		    sizeof(host_path));
		if (rc < 0)
			return rc;
		if (unlink(host_path) < 0)
			return -errno;
		return 0;
	}
}

static int
nextufs_stress_backend_rmdir(struct nextufs_stress_ctx *ctx, const char *path)
{
	if (ctx->backend == NEXTUFS_STRESS_BACKEND_OFFLINE)
		return nextufs_path_rmdir(&ctx->write_ctx, ctx->image_path, path);
	else {
		char host_path[NEXTUFS_STRESS_MAX_PATH];
		int rc;

		rc = nextufs_stress_join_host_path(ctx->mountpoint, path, host_path,
		    sizeof(host_path));
		if (rc < 0)
			return rc;
		if (rmdir(host_path) < 0)
			return -errno;
		return 0;
	}
}

static int
nextufs_stress_backend_link(struct nextufs_stress_ctx *ctx, const char *src,
    const char *dst)
{
	if (ctx->backend == NEXTUFS_STRESS_BACKEND_OFFLINE)
		return nextufs_path_link(&ctx->write_ctx, ctx->image_path, src, dst);
	else {
		char host_src[NEXTUFS_STRESS_MAX_PATH];
		char host_dst[NEXTUFS_STRESS_MAX_PATH];
		int rc;

		rc = nextufs_stress_join_host_path(ctx->mountpoint, src, host_src,
		    sizeof(host_src));
		if (rc < 0)
			return rc;
		rc = nextufs_stress_join_host_path(ctx->mountpoint, dst, host_dst,
		    sizeof(host_dst));
		if (rc < 0)
			return rc;
		if (link(host_src, host_dst) < 0)
			return -errno;
		return 0;
	}
}

static int
nextufs_stress_pick_boundary(struct nextufs_stress_ctx *ctx, size_t current_size,
    size_t *value_out)
{
	size_t candidates[18];
	size_t count = 0;
	size_t direct_boundary;
	size_t v;

	candidates[count++] = 0;
	candidates[count++] = 1;
	if (ctx->frag_size > 0) {
		candidates[count++] = ctx->frag_size - 1;
		candidates[count++] = ctx->frag_size;
		candidates[count++] = ctx->frag_size + 1;
	}
	if (ctx->block_size > 0) {
		candidates[count++] = ctx->block_size - 1;
		candidates[count++] = ctx->block_size;
		candidates[count++] = ctx->block_size + 1;
		direct_boundary = (size_t)ctx->block_size * 12U;
		candidates[count++] = direct_boundary > 0 ? direct_boundary - 1 : 0;
		candidates[count++] = direct_boundary;
		candidates[count++] = direct_boundary + 1;
		candidates[count++] = direct_boundary + ctx->block_size - 1;
		candidates[count++] = direct_boundary + ctx->block_size;
		candidates[count++] = direct_boundary + ctx->block_size + 1;
	}
	candidates[count++] = current_size;
	candidates[count++] = current_size > 0 ? current_size - 1 : 0;
	candidates[count++] = current_size + 1;
	candidates[count++] = NEXTUFS_STRESS_MAX_FILE - 1;
	for (;;) {
		v = candidates[nextufs_stress_rand_range(ctx, (unsigned)count)];
		if (v <= NEXTUFS_STRESS_MAX_FILE) {
			*value_out = v;
			return 0;
		}
	}
}

static int
nextufs_stress_random_name(struct nextufs_stress_ctx *ctx, char *name,
    size_t name_size)
{
	static const char alphabet[] =
	    "abcdefghijklmnopqrstuvwxyz0123456789_-";
	size_t i;
	size_t len;
	static const uint16_t biased_lengths[] = { 1, 2, 3, 4, 7, 15, 31, 63, 127, 239 };

	if (nextufs_stress_one_in(ctx, 3))
		len = biased_lengths[nextufs_stress_rand_range(ctx,
		    (unsigned)(sizeof(biased_lengths) / sizeof(biased_lengths[0])))];
	else
		len = 1 + nextufs_stress_rand_range(ctx, 48);
	if (len + 1 > name_size)
		len = name_size - 1;
	for (i = 0; i < len; i++)
		name[i] = alphabet[nextufs_stress_rand_range(ctx,
		    (unsigned)(sizeof(alphabet) - 1))];
	name[len] = '\0';
	return 0;
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
	static const uint16_t special[] = {
		0000, 0001, 0644, 0666, 0700, 0755, 0777, 01777, 02755, 04755
	};

	if (nextufs_stress_one_in(ctx, 3))
		return special[nextufs_stress_rand_range(ctx,
		    (unsigned)(sizeof(special) / sizeof(special[0])))];
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
	static const uid_t ids[] = { 0, 1, 2, 10, 100, 1000, 2000, 8192, 16000, 32000 };

	if (nextufs_stress_one_in(ctx, 3))
		return ids[nextufs_stress_rand_range(ctx,
		    (unsigned)(sizeof(ids) / sizeof(ids[0])))];
	return (uid_t)nextufs_stress_rand_range(ctx, 32000);
}

static int
nextufs_stress_pick_dir_path(struct nextufs_stress_ctx *ctx, int empty_only,
    int exclude_root, char *path_out, size_t path_out_size)
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
nextufs_stress_pick_meta_path(struct nextufs_stress_ctx *ctx, char *path_out,
    size_t path_out_size)
{
	unsigned indices[NEXTUFS_STRESS_MAX_PATHS];
	size_t i;
	unsigned count = 0;

	for (i = 0; i < NEXTUFS_STRESS_MAX_PATHS; i++) {
		struct nextufs_stress_obj *obj;

		if (!ctx->paths[i].used)
			continue;
		obj = nextufs_stress_find_obj(ctx, ctx->paths[i].obj_id);
		if (obj == NULL || obj->type == NEXTUFS_STRESS_LNK)
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
nextufs_stress_make_unique_child(struct nextufs_stress_ctx *ctx,
    const char *parent, char *path_out, size_t path_out_size)
{
	char name[NEXTUFS_STRESS_MAX_NAME];
	unsigned tries;

	for (tries = 0; tries < 256; tries++) {
		nextufs_stress_random_name(ctx, name, sizeof(name));
		if (nextufs_stress_make_child_path(path_out, path_out_size, parent,
		    name) < 0)
			return -ENAMETOOLONG;
		if (nextufs_stress_find_path(ctx, path_out) == NULL)
			return 0;
	}
	return -EEXIST;
}

static int
nextufs_stress_refresh_geometry(struct nextufs_stress_ctx *ctx)
{
	struct nextufs_image img;
	int rc;

	rc = nextufs_image_open(&img, ctx->image_path);
	if (rc < 0)
		return rc;
	ctx->block_size = img.sb.block_size;
	ctx->frag_size = img.sb.frag_size;
	nextufs_image_close(&img);
	return 0;
}

static int
nextufs_stress_refresh_path(struct nextufs_stress_ctx *ctx, const char *path)
{
	struct nextufs_image img;
	struct nextufs_node node;
	struct nextufs_stress_path *path_entry;
	struct nextufs_stress_obj *obj;
	int rc;

	rc = nextufs_image_open(&img, ctx->image_path);
	if (rc < 0)
		return rc;
	rc = nextufs_node_lookup(&img, path, 0, &node);
	nextufs_image_close(&img);
	if (rc < 0)
		return rc;
	path_entry = nextufs_stress_find_path(ctx, path);
	if (path_entry == NULL)
		return -ENOENT;
	obj = nextufs_stress_find_obj(ctx, path_entry->obj_id);
	if (obj == NULL)
		return -ENOENT;
	obj->mode = node.inode.mode;
	obj->uid = node.inode.uid;
	obj->gid = node.inode.gid;
	if (obj->type == NEXTUFS_STRESS_REG || obj->type == NEXTUFS_STRESS_LNK)
		obj->size = node.inode.size;
	if (obj->type == NEXTUFS_STRESS_CHR || obj->type == NEXTUFS_STRESS_BLK)
		obj->rdev = (uint32_t)node.inode.db[0];
	return 0;
}

static int
nextufs_stress_refresh_regular(struct nextufs_stress_ctx *ctx, const char *path)
{
	struct nextufs_image img;
	struct nextufs_node node;
	struct nextufs_stress_path *path_entry;
	struct nextufs_stress_obj *obj;
	uint8_t *data = NULL;
	size_t got = 0;
	int rc;

	rc = nextufs_image_open(&img, ctx->image_path);
	if (rc < 0)
		return rc;
	rc = nextufs_node_lookup(&img, path, 0, &node);
	if (rc < 0) {
		nextufs_image_close(&img);
		return rc;
	}
	if (!nextufs_node_is_reg(&node)) {
		nextufs_image_close(&img);
		return -EINVAL;
	}
	if (node.inode.size != 0) {
		data = malloc(node.inode.size);
		if (data == NULL) {
			nextufs_image_close(&img);
			return -ENOMEM;
		}
	}
	rc = nextufs_path_read(&img, path, 0, data, node.inode.size, &got);
	nextufs_image_close(&img);
	if (rc < 0) {
		free(data);
		return rc;
	}
	if (got != node.inode.size) {
		free(data);
		return -EIO;
	}
	path_entry = nextufs_stress_find_path(ctx, path);
	if (path_entry == NULL) {
		free(data);
		return -ENOENT;
	}
	obj = nextufs_stress_find_obj(ctx, path_entry->obj_id);
	if (obj == NULL) {
		free(data);
		return -ENOENT;
	}
	free(obj->data);
	obj->data = data;
	obj->size = node.inode.size;
	obj->mode = node.inode.mode;
	obj->uid = node.inode.uid;
	obj->gid = node.inode.gid;
	return 0;
}

static int
nextufs_stress_refresh_root(struct nextufs_stress_ctx *ctx)
{
	return nextufs_stress_refresh_path(ctx, ctx->root_path);
}

static int
nextufs_stress_vec_push(struct nextufs_stress_path_vec *vec, const char *path)
{
	char **new_items;

	if (vec->count == vec->cap) {
		size_t new_cap = vec->cap == 0 ? 32 : vec->cap * 2;

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
		if (ctx->paths[i].used)
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
			if ((size_t)st.st_size != obj->size ||
			    (obj->refcount != 0 && st.st_nlink != obj->refcount)) {
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
		} else if ((obj->type == NEXTUFS_STRESS_CHR ||
		    obj->type == NEXTUFS_STRESS_BLK) &&
		    (uint32_t)st.st_rdev != obj->rdev) {
			nextufs_image_close(&img);
			return -EINVAL;
		}
	}
	nextufs_image_close(&img);
	return 0;
}

static int
nextufs_stress_save_snapshot(struct nextufs_stress_ctx *ctx,
    struct nextufs_stress_snapshot *snap)
{
	size_t i;

	memset(snap, 0, sizeof(*snap));
	snap->next_obj_id = ctx->next_obj_id;
	for (i = 0; i < NEXTUFS_STRESS_MAX_OBJECTS; i++) {
		snap->objects[i] = ctx->objects[i];
		if (ctx->objects[i].data != NULL) {
			snap->objects[i].data = malloc(ctx->objects[i].size);
			if (snap->objects[i].data == NULL)
				return -ENOMEM;
			memcpy(snap->objects[i].data, ctx->objects[i].data,
			    ctx->objects[i].size);
		}
		if (ctx->objects[i].link_target != NULL) {
			snap->objects[i].link_target =
			    strdup(ctx->objects[i].link_target);
			if (snap->objects[i].link_target == NULL)
				return -ENOMEM;
		}
	}
	for (i = 0; i < NEXTUFS_STRESS_MAX_PATHS; i++) {
		snap->paths[i] = ctx->paths[i];
		if (ctx->paths[i].path != NULL) {
			snap->paths[i].path = strdup(ctx->paths[i].path);
			if (snap->paths[i].path == NULL)
				return -ENOMEM;
		}
	}
	return 0;
}

static void
nextufs_stress_free_snapshot(struct nextufs_stress_snapshot *snap)
{
	size_t i;

	for (i = 0; i < NEXTUFS_STRESS_MAX_PATHS; i++)
		free(snap->paths[i].path);
	for (i = 0; i < NEXTUFS_STRESS_MAX_OBJECTS; i++) {
		free(snap->objects[i].data);
		free(snap->objects[i].link_target);
	}
	memset(snap, 0, sizeof(*snap));
}

static int
nextufs_stress_compare_snapshot(struct nextufs_stress_ctx *ctx,
    const struct nextufs_stress_snapshot *snap)
{
	size_t i;

	if (ctx->next_obj_id != snap->next_obj_id)
		return -EINVAL;
	for (i = 0; i < NEXTUFS_STRESS_MAX_OBJECTS; i++) {
		const struct nextufs_stress_obj *a = &ctx->objects[i];
		const struct nextufs_stress_obj *b = &snap->objects[i];

		if (a->used != b->used || a->id != b->id || a->type != b->type ||
		    a->mode != b->mode || a->uid != b->uid || a->gid != b->gid ||
		    a->rdev != b->rdev || a->size != b->size ||
		    a->refcount != b->refcount)
			return -EINVAL;
		if (((a->link_target == NULL) != (b->link_target == NULL)) ||
		    (a->link_target != NULL &&
		    strcmp(a->link_target, b->link_target) != 0))
			return -EINVAL;
		if (((a->data == NULL) != (b->data == NULL)) ||
		    (a->data != NULL && memcmp(a->data, b->data, a->size) != 0))
			return -EINVAL;
	}
	for (i = 0; i < NEXTUFS_STRESS_MAX_PATHS; i++) {
		const struct nextufs_stress_path *a = &ctx->paths[i];
		const struct nextufs_stress_path *b = &snap->paths[i];

		if (a->used != b->used || a->obj_id != b->obj_id)
			return -EINVAL;
		if (((a->path == NULL) != (b->path == NULL)) ||
		    (a->path != NULL && strcmp(a->path, b->path) != 0))
			return -EINVAL;
	}
	return 0;
}

static int
nextufs_stress_run_fsck(struct nextufs_stress_ctx *ctx, const char *image_path,
    const char *log_path)
{
	char fsck_path[NEXTUFS_STRESS_MAX_PATH];
	char *argv[4];

	if (snprintf(fsck_path, sizeof(fsck_path), "%s/../nextufs.fsck/nextufs.fsck",
	    ctx->exe_dir) >= (int)sizeof(fsck_path))
		return -ENAMETOOLONG;
	argv[0] = fsck_path;
	argv[1] = "-n";
	argv[2] = (char *)image_path;
	argv[3] = NULL;
	return nextufs_stress_exec_wait_redirect(argv, log_path);
}

static int
nextufs_stress_assert_failed_op(struct nextufs_stress_ctx *ctx, int rc,
    struct nextufs_stress_snapshot *snap)
{
	int check_rc;

	if (rc >= 0)
		return -EINVAL;
	check_rc = nextufs_stress_validate(ctx);
	if (check_rc < 0)
		return check_rc;
	return nextufs_stress_compare_snapshot(ctx, snap);
}

static int
nextufs_stress_create_root(struct nextufs_stress_ctx *ctx)
{
	struct nextufs_image img;
	struct nextufs_node node;
	unsigned obj_id;
	int rc;

	rc = nextufs_stress_backend_mkdir(ctx, ctx->root_path, 0777);
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

	rc = nextufs_stress_pick_dir_path(ctx, 0, 0, parent, sizeof(parent));
	if (rc < 0)
		return rc;
	rc = nextufs_stress_make_unique_child(ctx, parent, path, sizeof(path));
	if (rc < 0)
		return rc;
	if (nextufs_stress_one_in(ctx, 2))
		rc = nextufs_stress_pick_boundary(ctx, 0, &size);
	else
		size = nextufs_stress_rand_range(ctx, 4096);
	if (rc < 0)
		return rc;
	if (size != 0) {
		data = malloc(size);
		if (data == NULL)
			return -ENOMEM;
		nextufs_stress_fill_bytes(ctx, data, size);
	}
	rc = nextufs_stress_set_op(ctx, "create-file %s size=%zu", path, size);
	if (rc < 0) {
		free(data);
		return rc;
	}
	rc = nextufs_stress_backend_create_file(ctx, path, data, size);
	if (rc < 0) {
		free(data);
		return rc;
	}
	rc = nextufs_stress_add_object(ctx, NEXTUFS_STRESS_REG,
	    NEXTUFS_IFREG | 0644, ctx->create_uid, ctx->create_gid, 0, data, size,
	    NULL, &obj_id);
	free(data);
	if (rc < 0)
		return rc;
	rc = nextufs_stress_add_path(ctx, path, obj_id);
	if (rc < 0)
		return rc;
	nextufs_stress_find_obj(ctx, obj_id)->refcount = 1;
	if (ctx->backend == NEXTUFS_STRESS_BACKEND_FUSE)
		return nextufs_stress_refresh_regular(ctx, path);
	return nextufs_stress_refresh_path(ctx, path);
}

static int
nextufs_stress_mkdir_op(struct nextufs_stress_ctx *ctx)
{
	char parent[NEXTUFS_STRESS_MAX_PATH];
	char path[NEXTUFS_STRESS_MAX_PATH];
	uint16_t perms;
	unsigned obj_id;
	int rc;

	rc = nextufs_stress_pick_dir_path(ctx, 0, 0, parent, sizeof(parent));
	if (rc < 0)
		return rc;
	rc = nextufs_stress_make_unique_child(ctx, parent, path, sizeof(path));
	if (rc < 0)
		return rc;
	perms = nextufs_stress_random_perms(ctx) & 0777;
	rc = nextufs_stress_set_op(ctx, "mkdir %s mode=%04o", path, perms);
	if (rc < 0)
		return rc;
	rc = nextufs_stress_backend_mkdir(ctx, path, perms);
	if (rc < 0)
		return rc;
	rc = nextufs_stress_add_object(ctx, NEXTUFS_STRESS_DIR,
	    NEXTUFS_IFDIR | perms, ctx->create_uid, ctx->create_gid, 0, NULL, 0,
	    NULL, &obj_id);
	if (rc < 0)
		return rc;
	rc = nextufs_stress_add_path(ctx, path, obj_id);
	if (rc < 0)
		return rc;
	nextufs_stress_find_obj(ctx, obj_id)->refcount = 1;
	rc = nextufs_stress_refresh_path(ctx, path);
	if (rc < 0)
		return rc;
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

	rc = nextufs_stress_pick_dir_path(ctx, 0, 0, parent, sizeof(parent));
	if (rc < 0)
		return rc;
	rc = nextufs_stress_make_unique_child(ctx, parent, path, sizeof(path));
	if (rc < 0)
		return rc;
	if (nextufs_stress_one_in(ctx, 2) && nextufs_stress_pick_nondir_path(ctx,
	    target, sizeof(target)) == 0) {
		if (nextufs_stress_one_in(ctx, 2)) {
			const char *base = strrchr(target, '/');

			if (base != NULL)
				memmove(target, base + 1, strlen(base + 1) + 1);
		}
	} else {
		snprintf(target, sizeof(target), "dangling-%08x",
		    nextufs_stress_rand_range(ctx, UINT_MAX));
	}
	rc = nextufs_stress_set_op(ctx, "symlink %s -> %s", path, target);
	if (rc < 0)
		return rc;
	rc = nextufs_stress_backend_symlink(ctx, target, path);
	if (rc < 0)
		return rc;
	rc = nextufs_stress_add_object(ctx, NEXTUFS_STRESS_LNK,
	    NEXTUFS_IFLNK | 0777, ctx->create_uid, ctx->create_gid, 0, NULL, 0,
	    target, &obj_id);
	if (rc < 0)
		return rc;
	rc = nextufs_stress_add_path(ctx, path, obj_id);
	if (rc < 0)
		return rc;
	nextufs_stress_find_obj(ctx, obj_id)->refcount = 1;
	return nextufs_stress_refresh_path(ctx, path);
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

	rc = nextufs_stress_pick_dir_path(ctx, 0, 0, parent, sizeof(parent));
	if (rc < 0)
		return rc;
	rc = nextufs_stress_make_unique_child(ctx, parent, path, sizeof(path));
	if (rc < 0)
		return rc;
	if (ctx->backend == NEXTUFS_STRESS_BACKEND_FUSE || nextufs_stress_one_in(ctx, 2)) {
		type = NEXTUFS_STRESS_FIFO;
		mode = NEXTUFS_IFIFO | (nextufs_stress_random_perms(ctx) & 0777);
	} else {
		type = nextufs_stress_one_in(ctx, 2) ? NEXTUFS_STRESS_CHR :
		    NEXTUFS_STRESS_BLK;
		mode = (type == NEXTUFS_STRESS_CHR ? NEXTUFS_IFCHR : NEXTUFS_IFBLK) |
		    (nextufs_stress_random_perms(ctx) & 0777);
		rdev = (uint32_t)nextufs_stress_rand_range(ctx, 256);
	}
	rc = nextufs_stress_set_op(ctx, "mknod %s mode=%06o rdev=%" PRIu32, path,
	    mode, rdev);
	if (rc < 0)
		return rc;
	rc = nextufs_stress_backend_mknod(ctx, path, mode, rdev);
	if (rc < 0)
		return rc;
	rc = nextufs_stress_add_object(ctx, type, mode, ctx->create_uid,
	    ctx->create_gid, rdev, NULL, 0, NULL, &obj_id);
	if (rc < 0)
		return rc;
	rc = nextufs_stress_add_path(ctx, path, obj_id);
	if (rc < 0)
		return rc;
	nextufs_stress_find_obj(ctx, obj_id)->refcount = 1;
	return nextufs_stress_refresh_path(ctx, path);
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

	rc = nextufs_stress_pick_path_of_type(ctx, NEXTUFS_STRESS_REG, 0, path,
	    sizeof(path));
	if (rc < 0)
		return rc;
	entry = nextufs_stress_find_path(ctx, path);
	obj = nextufs_stress_find_obj(ctx, entry->obj_id);
	rc = nextufs_stress_pick_boundary(ctx, obj->size, &size);
	if (rc < 0)
		return rc;
	if (size != 0) {
		data = malloc(size);
		if (data == NULL)
			return -ENOMEM;
		nextufs_stress_fill_bytes(ctx, data, size);
	}
	rc = nextufs_stress_set_op(ctx, "overwrite %s size=%zu", path, size);
	if (rc < 0) {
		free(data);
		return rc;
	}
	rc = nextufs_stress_backend_overwrite(ctx, path, data, size);
	if (rc < 0) {
		free(data);
		return rc;
	}
	free(obj->data);
	obj->data = data;
	obj->size = size;
	if (ctx->backend == NEXTUFS_STRESS_BACKEND_FUSE)
		return nextufs_stress_refresh_regular(ctx, path);
	return 0;
}

static int
nextufs_stress_append_op(struct nextufs_stress_ctx *ctx)
{
	char path[NEXTUFS_STRESS_MAX_PATH];
	struct nextufs_stress_path *entry;
	struct nextufs_stress_obj *obj;
	uint8_t *tail;
	size_t tail_size;
	int rc;

	rc = nextufs_stress_pick_path_of_type(ctx, NEXTUFS_STRESS_REG, 0, path,
	    sizeof(path));
	if (rc < 0)
		return rc;
	entry = nextufs_stress_find_path(ctx, path);
	obj = nextufs_stress_find_obj(ctx, entry->obj_id);
	rc = nextufs_stress_pick_boundary(ctx, obj->size, &tail_size);
	if (rc < 0)
		return rc;
	if (tail_size == 0 || tail_size > NEXTUFS_STRESS_MAX_FILE - obj->size)
		tail_size = 1 + nextufs_stress_rand_range(ctx, 4096);
	if (tail_size > NEXTUFS_STRESS_MAX_FILE - obj->size)
		tail_size = NEXTUFS_STRESS_MAX_FILE - obj->size;
	if (tail_size == 0)
		return -EFBIG;
	tail = malloc(tail_size);
	if (tail == NULL)
		return -ENOMEM;
	nextufs_stress_fill_bytes(ctx, tail, tail_size);
	rc = nextufs_stress_set_op(ctx, "append %s size=%zu", path, tail_size);
	if (rc < 0) {
		free(tail);
		return rc;
	}
	rc = nextufs_stress_backend_append(ctx, path, tail, tail_size);
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
	if (ctx->backend == NEXTUFS_STRESS_BACKEND_FUSE)
		return nextufs_stress_refresh_regular(ctx, path);
	return 0;
}

static int
nextufs_stress_pwrite_op(struct nextufs_stress_ctx *ctx)
{
	char path[NEXTUFS_STRESS_MAX_PATH];
	struct nextufs_stress_path *entry;
	struct nextufs_stress_obj *obj;
	uint8_t *buf;
	size_t size;
	uint64_t offset;
	int rc;

	rc = nextufs_stress_pick_path_of_type(ctx, NEXTUFS_STRESS_REG, 0, path,
	    sizeof(path));
	if (rc < 0)
		return rc;
	entry = nextufs_stress_find_path(ctx, path);
	obj = nextufs_stress_find_obj(ctx, entry->obj_id);
	rc = nextufs_stress_pick_boundary(ctx, obj->size, &size);
	if (rc < 0)
		return rc;
	if (size == 0)
		size = 1;
	if (nextufs_stress_one_in(ctx, 2))
		rc = nextufs_stress_pick_boundary(ctx, obj->size, (size_t *)&offset);
	else
		offset = obj->size == 0 ? 0 :
		    nextufs_stress_rand_range(ctx, (unsigned)(obj->size + 1));
	if (rc < 0)
		return rc;
	if (offset > NEXTUFS_STRESS_MAX_FILE)
		offset = NEXTUFS_STRESS_MAX_FILE;
	if (offset + size > NEXTUFS_STRESS_MAX_FILE)
		size = NEXTUFS_STRESS_MAX_FILE - (size_t)offset;
	if (size == 0)
		return -EFBIG;
	buf = malloc(size);
	if (buf == NULL)
		return -ENOMEM;
	nextufs_stress_fill_bytes(ctx, buf, size);
	rc = nextufs_stress_set_op(ctx, "pwrite %s off=%" PRIu64 " size=%zu", path,
	    offset, size);
	if (rc < 0) {
		free(buf);
		return rc;
	}
	rc = nextufs_stress_backend_pwrite(ctx, path, buf, size, offset);
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
	if (ctx->backend == NEXTUFS_STRESS_BACKEND_FUSE)
		return nextufs_stress_refresh_regular(ctx, path);
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

	rc = nextufs_stress_pick_path_of_type(ctx, NEXTUFS_STRESS_REG, 0, path,
	    sizeof(path));
	if (rc < 0)
		return rc;
	entry = nextufs_stress_find_path(ctx, path);
	obj = nextufs_stress_find_obj(ctx, entry->obj_id);
	rc = nextufs_stress_pick_boundary(ctx, obj->size, &new_size);
	if (rc < 0)
		return rc;
	rc = nextufs_stress_set_op(ctx, "truncate %s size=%zu", path, new_size);
	if (rc < 0)
		return rc;
	rc = nextufs_stress_backend_truncate(ctx, path, new_size);
	if (rc < 0)
		return rc;
	rc = nextufs_stress_resize_data(obj, new_size);
	if (rc < 0)
		return rc;
	if (ctx->backend == NEXTUFS_STRESS_BACKEND_FUSE)
		return nextufs_stress_refresh_regular(ctx, path);
	return 0;
}

static int
nextufs_stress_chmod_op(struct nextufs_stress_ctx *ctx)
{
	char path[NEXTUFS_STRESS_MAX_PATH];
	uint16_t perms;
	int rc;

	rc = nextufs_stress_pick_meta_path(ctx, path, sizeof(path));
	if (rc < 0)
		return rc;
	perms = nextufs_stress_random_perms(ctx);
	rc = nextufs_stress_set_op(ctx, "chmod %s mode=%04o", path, perms);
	if (rc < 0)
		return rc;
	rc = nextufs_stress_backend_chmod(ctx, path, perms);
	if (rc < 0)
		return rc;
	return nextufs_stress_refresh_path(ctx, path);
}

static int
nextufs_stress_chown_op(struct nextufs_stress_ctx *ctx)
{
	char path[NEXTUFS_STRESS_MAX_PATH];
	uid_t uid;
	gid_t gid;
	int rc;

	rc = nextufs_stress_pick_meta_path(ctx, path, sizeof(path));
	if (rc < 0)
		return rc;
	uid = nextufs_stress_random_id(ctx);
	gid = nextufs_stress_random_id(ctx);
	rc = nextufs_stress_set_op(ctx, "chown %s uid=%u gid=%u", path,
	    (unsigned)uid, (unsigned)gid);
	if (rc < 0)
		return rc;
	rc = nextufs_stress_backend_chown(ctx, path, uid, gid);
	if (rc < 0)
		return rc;
	return nextufs_stress_refresh_path(ctx, path);
}

static int
nextufs_stress_utimes_op(struct nextufs_stress_ctx *ctx)
{
	char path[NEXTUFS_STRESS_MAX_PATH];
	uint32_t atime;
	uint32_t mtime;
	int rc;

	rc = nextufs_stress_pick_meta_path(ctx, path, sizeof(path));
	if (rc < 0)
		return rc;
	atime = 1000000000U + (uint32_t)nextufs_stress_rand_range(ctx, 500000000U);
	mtime = atime + (uint32_t)nextufs_stress_rand_range(ctx, 500000000U);
	rc = nextufs_stress_set_op(ctx, "utimes %s atime=%" PRIu32 " mtime=%" PRIu32,
	    path, atime, mtime);
	if (rc < 0)
		return rc;
	rc = nextufs_stress_backend_utimes(ctx, path, atime, mtime);
	if (rc < 0)
		return rc;
	return nextufs_stress_refresh_path(ctx, path);
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
	if (rc < 0 && nextufs_stress_pick_dir_path(ctx, 0, 1, src, sizeof(src)) < 0)
		return rc;
	rc = nextufs_stress_pick_dir_path(ctx, 0, 0, dst_parent, sizeof(dst_parent));
	if (rc < 0)
		return rc;
	rc = nextufs_stress_make_unique_child(ctx, dst_parent, dst, sizeof(dst));
	if (rc < 0)
		return rc;
	if (nextufs_stress_path_has_prefix(dst_parent, src))
		return -EINVAL;
	rc = nextufs_stress_set_op(ctx, "rename %s -> %s", src, dst);
	if (rc < 0)
		return rc;
	rc = nextufs_stress_backend_rename(ctx, src, dst);
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

		if (!ctx->paths[i].used || ctx->paths[i].path == NULL)
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
	rc = nextufs_stress_set_op(ctx, "unlink %s", path);
	if (rc < 0)
		return rc;
	rc = nextufs_stress_backend_unlink(ctx, path);
	if (rc < 0)
		return rc;
	return nextufs_stress_remove_path(ctx, path);
}

static int
nextufs_stress_rmdir_op(struct nextufs_stress_ctx *ctx)
{
	char path[NEXTUFS_STRESS_MAX_PATH];
	int rc;

	rc = nextufs_stress_pick_dir_path(ctx, 1, 1, path, sizeof(path));
	if (rc < 0)
		return rc;
	rc = nextufs_stress_set_op(ctx, "rmdir %s", path);
	if (rc < 0)
		return rc;
	rc = nextufs_stress_backend_rmdir(ctx, path);
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

	rc = nextufs_stress_pick_path_of_type(ctx, NEXTUFS_STRESS_REG, 0, src,
	    sizeof(src));
	if (rc < 0)
		return rc;
	rc = nextufs_stress_pick_dir_path(ctx, 0, 0, parent, sizeof(parent));
	if (rc < 0)
		return rc;
	rc = nextufs_stress_make_unique_child(ctx, parent, dst, sizeof(dst));
	if (rc < 0)
		return rc;
	rc = nextufs_stress_set_op(ctx, "link %s -> %s", src, dst);
	if (rc < 0)
		return rc;
	rc = nextufs_stress_backend_link(ctx, src, dst);
	if (rc < 0)
		return rc;
	src_entry = nextufs_stress_find_path(ctx, src);
	obj = nextufs_stress_find_obj(ctx, src_entry->obj_id);
	rc = nextufs_stress_add_path(ctx, dst, src_entry->obj_id);
	if (rc < 0)
		return rc;
	obj->refcount++;
	return 0;
}

static int
nextufs_stress_fail_create_existing(struct nextufs_stress_ctx *ctx)
{
	char path[NEXTUFS_STRESS_MAX_PATH];
	uint8_t b = 0x42;
	struct nextufs_stress_snapshot snap;
	int rc;

	rc = nextufs_stress_pick_nondir_path(ctx, path, sizeof(path));
	if (rc < 0)
		return rc;
	rc = nextufs_stress_save_snapshot(ctx, &snap);
	if (rc < 0)
		return rc;
	rc = nextufs_stress_set_op(ctx, "fail-create-existing %s", path);
	if (rc == 0)
		rc = nextufs_stress_backend_create_file(ctx, path, &b, 1);
	if (rc == 0)
		rc = nextufs_stress_assert_failed_op(ctx, 0, &snap);
	else
		rc = nextufs_stress_assert_failed_op(ctx, rc, &snap);
	nextufs_stress_free_snapshot(&snap);
	return rc;
}

static int
nextufs_stress_fail_unlink_missing(struct nextufs_stress_ctx *ctx)
{
	char parent[NEXTUFS_STRESS_MAX_PATH];
	char path[NEXTUFS_STRESS_MAX_PATH];
	struct nextufs_stress_snapshot snap;
	int rc;

	rc = nextufs_stress_pick_dir_path(ctx, 0, 0, parent, sizeof(parent));
	if (rc < 0)
		return rc;
	rc = nextufs_stress_make_unique_child(ctx, parent, path, sizeof(path));
	if (rc < 0)
		return rc;
	rc = nextufs_stress_save_snapshot(ctx, &snap);
	if (rc < 0)
		return rc;
	rc = nextufs_stress_set_op(ctx, "fail-unlink-missing %s", path);
	if (rc == 0)
		rc = nextufs_stress_backend_unlink(ctx, path);
	rc = nextufs_stress_assert_failed_op(ctx, rc, &snap);
	nextufs_stress_free_snapshot(&snap);
	return rc;
}

static int
nextufs_stress_fail_rmdir_nonempty(struct nextufs_stress_ctx *ctx)
{
	char path[NEXTUFS_STRESS_MAX_PATH];
	struct nextufs_stress_snapshot snap;
	int rc;

	rc = nextufs_stress_pick_dir_path(ctx, 0, 1, path, sizeof(path));
	if (rc < 0)
		return rc;
	if (nextufs_stress_dir_empty(ctx, path))
		return -ENOENT;
	rc = nextufs_stress_save_snapshot(ctx, &snap);
	if (rc < 0)
		return rc;
	rc = nextufs_stress_set_op(ctx, "fail-rmdir-nonempty %s", path);
	if (rc == 0)
		rc = nextufs_stress_backend_rmdir(ctx, path);
	rc = nextufs_stress_assert_failed_op(ctx, rc, &snap);
	nextufs_stress_free_snapshot(&snap);
	return rc;
}

static int
nextufs_stress_fail_link_dir(struct nextufs_stress_ctx *ctx)
{
	char src[NEXTUFS_STRESS_MAX_PATH];
	char parent[NEXTUFS_STRESS_MAX_PATH];
	char dst[NEXTUFS_STRESS_MAX_PATH];
	struct nextufs_stress_snapshot snap;
	int rc;

	rc = nextufs_stress_pick_dir_path(ctx, 0, 1, src, sizeof(src));
	if (rc < 0)
		return rc;
	rc = nextufs_stress_pick_dir_path(ctx, 0, 0, parent, sizeof(parent));
	if (rc < 0)
		return rc;
	rc = nextufs_stress_make_unique_child(ctx, parent, dst, sizeof(dst));
	if (rc < 0)
		return rc;
	rc = nextufs_stress_save_snapshot(ctx, &snap);
	if (rc < 0)
		return rc;
	rc = nextufs_stress_set_op(ctx, "fail-link-dir %s -> %s", src, dst);
	if (rc == 0)
		rc = nextufs_stress_backend_link(ctx, src, dst);
	rc = nextufs_stress_assert_failed_op(ctx, rc, &snap);
	nextufs_stress_free_snapshot(&snap);
	return rc;
}

static int
nextufs_stress_fail_rename_into_descendant(struct nextufs_stress_ctx *ctx)
{
	size_t i;
	char src[NEXTUFS_STRESS_MAX_PATH];
	char dst[NEXTUFS_STRESS_MAX_PATH];
	struct nextufs_stress_snapshot snap;
	int found = 0;
	int rc;

	for (i = 0; i < NEXTUFS_STRESS_MAX_PATHS; i++) {
		struct nextufs_stress_obj *obj;

		if (!ctx->paths[i].used)
			continue;
		obj = nextufs_stress_find_obj(ctx, ctx->paths[i].obj_id);
		if (obj == NULL || obj->type != NEXTUFS_STRESS_DIR)
			continue;
		if (strcmp(ctx->paths[i].path, ctx->root_path) == 0)
			continue;
		snprintf(dst, sizeof(dst), "%s/child", ctx->paths[i].path);
		if (nextufs_stress_path_has_prefix(dst, ctx->paths[i].path)) {
			memcpy(src, ctx->paths[i].path, strlen(ctx->paths[i].path) + 1);
			found = 1;
			break;
		}
	}
	if (!found)
		return -ENOENT;
	rc = nextufs_stress_save_snapshot(ctx, &snap);
	if (rc < 0)
		return rc;
	rc = nextufs_stress_set_op(ctx, "fail-rename-descendant %s -> %s", src, dst);
	if (rc == 0)
		rc = nextufs_stress_backend_rename(ctx, src, dst);
	rc = nextufs_stress_assert_failed_op(ctx, rc, &snap);
	nextufs_stress_free_snapshot(&snap);
	return rc;
}

static int
nextufs_stress_apply_negative_op(struct nextufs_stress_ctx *ctx)
{
	unsigned attempt;

	for (attempt = 0; attempt < 64; attempt++) {
		size_t prior_log_count = ctx->op_log.count;
		int rc;

		switch (nextufs_stress_rand_range(ctx, 5)) {
		case 0:
			rc = nextufs_stress_fail_create_existing(ctx);
			break;
		case 1:
			rc = nextufs_stress_fail_unlink_missing(ctx);
			break;
		case 2:
			rc = nextufs_stress_fail_rmdir_nonempty(ctx);
			break;
		case 3:
			rc = nextufs_stress_fail_link_dir(ctx);
			break;
		default:
			rc = nextufs_stress_fail_rename_into_descendant(ctx);
			break;
		}
		if (rc >= 0 || ctx->op_log.count != prior_log_count)
			return rc;
	}
	return -ENOENT;
}

static int
nextufs_stress_have_type(struct nextufs_stress_ctx *ctx,
    enum nextufs_stress_type type)
{
	char scratch[NEXTUFS_STRESS_MAX_PATH];

	return nextufs_stress_pick_path_of_type(ctx, type, 0, scratch,
	    sizeof(scratch)) == 0;
}

static int
nextufs_stress_have_nondir(struct nextufs_stress_ctx *ctx)
{
	char scratch[NEXTUFS_STRESS_MAX_PATH];

	return nextufs_stress_pick_nondir_path(ctx, scratch, sizeof(scratch)) == 0;
}

static int
nextufs_stress_have_dir(struct nextufs_stress_ctx *ctx, int require_empty)
{
	char scratch[NEXTUFS_STRESS_MAX_PATH];

	return nextufs_stress_pick_dir_path(ctx, require_empty, 1, scratch,
	    sizeof(scratch)) == 0;
}

static int
nextufs_stress_apply_positive_op(struct nextufs_stress_ctx *ctx)
{
	int have_files = nextufs_stress_have_type(ctx, NEXTUFS_STRESS_REG);
	int have_nondirs = nextufs_stress_have_nondir(ctx);
	int have_dirs = nextufs_stress_have_dir(ctx, 0);
	unsigned attempt;

	for (attempt = 0; attempt < 64; attempt++) {
		size_t prior_log_count = ctx->op_log.count;
		int rc = -ENOENT;

		switch (nextufs_stress_rand_range(ctx, 15)) {
		case 0:
			rc = nextufs_stress_create_file_op(ctx);
			break;
		case 1:
			rc = nextufs_stress_mkdir_op(ctx);
			break;
		case 2:
			if (have_files)
				rc = nextufs_stress_overwrite_op(ctx);
			break;
		case 3:
			if (have_files)
				rc = nextufs_stress_append_op(ctx);
			break;
		case 4:
			if (have_files)
				rc = nextufs_stress_pwrite_op(ctx);
			break;
		case 5:
			if (have_files)
				rc = nextufs_stress_truncate_op(ctx);
			break;
		case 6:
			rc = nextufs_stress_symlink_op(ctx);
			break;
		case 7:
			rc = nextufs_stress_mknod_op(ctx);
			break;
		case 8:
			if (have_nondirs)
				rc = nextufs_stress_chmod_op(ctx);
			break;
		case 9:
			if (have_nondirs)
				rc = nextufs_stress_chown_op(ctx);
			break;
		case 10:
			if (have_nondirs)
				rc = nextufs_stress_utimes_op(ctx);
			break;
		case 11:
			if (have_nondirs || have_dirs)
				rc = nextufs_stress_rename_op(ctx);
			break;
		case 12:
			if (have_nondirs)
				rc = nextufs_stress_unlink_op(ctx);
			break;
		case 13:
			if (nextufs_stress_have_dir(ctx, 1))
				rc = nextufs_stress_rmdir_op(ctx);
			break;
		case 14:
			if (have_files)
				rc = nextufs_stress_link_op(ctx);
			break;
		}
		if (rc >= 0 || ctx->op_log.count != prior_log_count)
			return rc;
	}
	return -ENOENT;
}

static int
nextufs_stress_run_single(struct nextufs_stress_ctx *ctx)
{
	unsigned i;
	int rc;

	rc = nextufs_stress_refresh_geometry(ctx);
	if (rc < 0)
		return rc;
	if (ctx->backend == NEXTUFS_STRESS_BACKEND_FUSE) {
		rc = nextufs_stress_fuse_start(ctx);
		if (rc < 0)
			return rc;
	}
	rc = nextufs_stress_create_root(ctx);
	if (rc < 0)
		goto out;
	rc = nextufs_stress_validate(ctx);
	if (rc < 0)
		goto out;
	for (i = 0; i < ctx->op_count; i++) {
		ctx->current_op = i + 1;
		if (nextufs_stress_one_in(ctx, 5))
			rc = nextufs_stress_apply_negative_op(ctx);
		else
			rc = nextufs_stress_apply_positive_op(ctx);
		if (rc < 0)
			goto out;
		rc = nextufs_stress_validate(ctx);
		if (rc < 0)
			goto out;
	}
	if (ctx->run_fsck) {
		char log_path[NEXTUFS_STRESS_MAX_PATH];

		rc = nextufs_stress_join_suffix(ctx->fail_dir, "/fsck.log",
		    log_path, sizeof(log_path));
		if (rc < 0)
			goto out;
		rc = nextufs_stress_run_fsck(ctx, ctx->image_path, log_path);
		if (rc < 0)
			goto out;
	}
out:
	if (ctx->backend == NEXTUFS_STRESS_BACKEND_FUSE)
		(void)nextufs_stress_fuse_stop(ctx);
	return rc;
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

static int
nextufs_stress_init_ctx(struct nextufs_stress_ctx *ctx)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->seed = 0x4e455854554653ULL;
	ctx->rng = ctx->seed;
	ctx->op_count = 200;
	ctx->batch_count = NEXTUFS_STRESS_BATCH_DEFAULT;
	ctx->verbose = 1;
	ctx->write_ctx.policy = NEXTUFS_WRITE_SU;
	ctx->write_ctx.uid = 0;
	ctx->write_ctx.gid = 0;
	ctx->create_uid = 0;
	ctx->create_gid = 0;
	ctx->backend = NEXTUFS_STRESS_BACKEND_OFFLINE;
	return nextufs_stress_find_tools(ctx);
}

static int
nextufs_stress_prepare_fail_dir(struct nextufs_stress_ctx *ctx)
{
	if (ctx->fail_dir[0] == '\0') {
		if (snprintf(ctx->fail_dir, sizeof(ctx->fail_dir),
		    "%s/.scratch/stress-failures",
		    ctx->exe_dir) >= (int)sizeof(ctx->fail_dir))
			return -ENAMETOOLONG;
	}
	return nextufs_stress_ensure_dir(ctx->fail_dir);
}

static int
nextufs_stress_batch(struct nextufs_stress_ctx *base)
{
	unsigned i;
	int rc;

	rc = nextufs_stress_prepare_fail_dir(base);
	if (rc < 0)
		return rc;
	for (i = 0; i < base->batch_count; i++) {
		struct nextufs_stress_ctx run;
		char image_path[NEXTUFS_STRESS_MAX_PATH];
		char log_path[NEXTUFS_STRESS_MAX_PATH];
		char seed_name[32];
		char fail_name[32];
		uint64_t run_seed = base->seed + (uint64_t)i * 0x9e3779b97f4a7c15ULL;
		int run_rc;

		rc = nextufs_stress_init_ctx(&run);
		if (rc < 0)
			return rc;
		run = *base;
		memset(run.objects, 0, sizeof(run.objects));
		memset(run.paths, 0, sizeof(run.paths));
		memset(&run.op_log, 0, sizeof(run.op_log));
		run.seed = run_seed;
		run.rng = run_seed;
		run.current_op = 0;
		run.next_obj_id = 0;
		run.mountpoint[0] = '\0';
		run.fuse_pid = 0;
		snprintf(seed_name, sizeof(seed_name), "work-%08x.raw",
		    (unsigned)run_seed);
		rc = nextufs_stress_make_child_path(image_path, sizeof(image_path),
		    base->fail_dir, seed_name);
		if (rc < 0) {
			nextufs_stress_cleanup(&run);
			return rc;
		}
		rc = nextufs_stress_copy_file(base->template_image_path, image_path);
		if (rc < 0) {
			nextufs_stress_cleanup(&run);
			return rc;
		}
		run.image_path = image_path;
		run_rc = nextufs_stress_run_single(&run);
		rc = nextufs_stress_join_suffix(image_path, ".log", log_path,
		    sizeof(log_path));
		if (rc < 0) {
			nextufs_stress_cleanup(&run);
			return rc;
		}
		(void)nextufs_stress_write_log_file(&run.op_log, log_path);
		if (run_rc < 0) {
			char fail_image[NEXTUFS_STRESS_MAX_PATH];
			char fail_log[NEXTUFS_STRESS_MAX_PATH];

			snprintf(fail_name, sizeof(fail_name), "fail-%08x.raw",
			    (unsigned)run_seed);
			rc = nextufs_stress_make_child_path(fail_image,
			    sizeof(fail_image), base->fail_dir, fail_name);
			if (rc < 0) {
				nextufs_stress_cleanup(&run);
				return rc;
			}
			snprintf(fail_name, sizeof(fail_name), "fail-%08x.log",
			    (unsigned)run_seed);
			rc = nextufs_stress_make_child_path(fail_log,
			    sizeof(fail_log), base->fail_dir, fail_name);
			if (rc < 0) {
				nextufs_stress_cleanup(&run);
				return rc;
			}
			rename(image_path, fail_image);
			rename(log_path, fail_log);
			fprintf(stderr,
			    "nextufs_stress: FAILED seed=%" PRIu64 " step=%u backend=%s saved=%s\n",
			    run.seed, run.current_op,
			    run.backend == NEXTUFS_STRESS_BACKEND_FUSE ? "fuse" : "offline",
			    fail_image);
			nextufs_stress_cleanup(&run);
			return run_rc;
		}
		unlink(image_path);
		unlink(log_path);
		nextufs_stress_cleanup(&run);
	}
	printf("nextufs_stress: batch ok seeds=%u backend=%s\n", base->batch_count,
	    base->backend == NEXTUFS_STRESS_BACKEND_FUSE ? "fuse" : "offline");
	return 0;
}

int
nextufs_stress_main(int argc, char **argv)
{
	struct nextufs_stress_ctx ctx;
	const char *image_path = NULL;
	int i;
	int rc;

	umask(0);
	rc = nextufs_stress_init_ctx(&ctx);
	if (rc < 0) {
		fprintf(stderr, "nextufs_stress: init failed: %d\n", rc);
		return 1;
	}
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--seed") == 0) {
			if (i + 1 >= argc || nextufs_stress_parse_u64(argv[i + 1],
			    &ctx.seed) < 0) {
				fprintf(stderr, "nextufs_stress: invalid seed\n");
				return 2;
			}
			ctx.rng = ctx.seed;
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
		if (strcmp(argv[i], "--batch") == 0) {
			if (i + 1 >= argc || nextufs_stress_parse_u32(argv[i + 1],
			    &ctx.batch_count) < 0) {
				fprintf(stderr, "nextufs_stress: invalid batch count\n");
				return 2;
			}
			i++;
			continue;
		}
		if (strcmp(argv[i], "--save-fail-dir") == 0) {
			if (i + 1 >= argc || strlen(argv[i + 1]) + 1 >
			    sizeof(ctx.fail_dir)) {
				fprintf(stderr, "nextufs_stress: invalid failure dir\n");
				return 2;
			}
			memcpy(ctx.fail_dir, argv[i + 1], strlen(argv[i + 1]) + 1);
			i++;
			continue;
		}
		if (strcmp(argv[i], "--root") == 0) {
			if (i + 1 >= argc || strlen(argv[i + 1]) + 1 >
			    sizeof(ctx.root_path) || argv[i + 1][0] != '/') {
				fprintf(stderr, "nextufs_stress: invalid root path\n");
				return 2;
			}
			memcpy(ctx.root_path, argv[i + 1], strlen(argv[i + 1]) + 1);
			i++;
			continue;
		}
		if (strcmp(argv[i], "--backend") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "nextufs_stress: missing backend\n");
				return 2;
			}
			if (strcmp(argv[i + 1], "offline") == 0)
				ctx.backend = NEXTUFS_STRESS_BACKEND_OFFLINE;
			else if (strcmp(argv[i + 1], "fuse") == 0)
				ctx.backend = NEXTUFS_STRESS_BACKEND_FUSE;
			else {
				fprintf(stderr, "nextufs_stress: invalid backend\n");
				return 2;
			}
			i++;
			continue;
		}
		if (strcmp(argv[i], "--quiet") == 0) {
			ctx.verbose = 0;
			continue;
		}
		if (strcmp(argv[i], "--fsck") == 0) {
			ctx.run_fsck = 1;
			continue;
		}
		if (strcmp(argv[i], "--policy") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "nextufs_stress: missing policy\n");
				return 2;
			}
			if (strcmp(argv[i + 1], "su") == 0)
				ctx.write_ctx.policy = NEXTUFS_WRITE_SU;
			else if (strcmp(argv[i + 1], "user") == 0)
				ctx.write_ctx.policy = NEXTUFS_WRITE_USER;
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
		    "usage: %s [--seed n] [--ops n] [--batch n] [--backend offline|fuse] [--policy su|user] [--fsck] [--save-fail-dir dir] [--root /path] [--quiet] <source>\n",
		    argv[0]);
		return 2;
	}
	if (image_path == NULL) {
		fprintf(stderr,
		    "usage: %s [--seed n] [--ops n] [--batch n] [--backend offline|fuse] [--policy su|user] [--fsck] [--save-fail-dir dir] [--root /path] [--quiet] <source>\n",
		    argv[0]);
		return 2;
	}
	if (ctx.root_path[0] == '\0') {
		snprintf(ctx.root_path, sizeof(ctx.root_path), "/nextufs-stress-%08x",
		    (unsigned)ctx.seed);
	}
	if (ctx.backend == NEXTUFS_STRESS_BACKEND_FUSE) {
		ctx.create_uid = geteuid();
		ctx.create_gid = getegid();
	}
	if (ctx.batch_count > 1) {
		rc = nextufs_stress_resolve_existing_path(image_path,
		    ctx.image_path_buf, sizeof(ctx.image_path_buf));
		if (rc < 0) {
			fprintf(stderr, "nextufs_stress: image path resolve failed: %d\n",
			    rc);
			return 1;
		}
		ctx.template_image_path = ctx.image_path_buf;
		rc = nextufs_stress_batch(&ctx);
		return rc < 0 ? 1 : 0;
	}
	rc = nextufs_stress_resolve_existing_path(image_path, ctx.image_path_buf,
	    sizeof(ctx.image_path_buf));
	if (rc < 0) {
		fprintf(stderr, "nextufs_stress: image path resolve failed: %d\n", rc);
		return 1;
	}
	ctx.image_path = ctx.image_path_buf;
	rc = nextufs_stress_prepare_fail_dir(&ctx);
	if (rc < 0) {
		fprintf(stderr, "nextufs_stress: failure dir setup failed: %d\n", rc);
		return 1;
	}
	rc = nextufs_stress_run_single(&ctx);
	if (rc < 0) {
		char log_path[NEXTUFS_STRESS_MAX_PATH];
		int fail_rc = rc;

		rc = nextufs_stress_join_suffix(ctx.fail_dir, "/fail.log", log_path,
		    sizeof(log_path));
		if (rc < 0) {
			nextufs_stress_cleanup(&ctx);
			return 1;
		}
		(void)nextufs_stress_write_log_file(&ctx.op_log, log_path);
		fprintf(stderr,
		    "nextufs_stress: FAILED seed=%" PRIu64 " step=%u backend=%s root=%s last-op=%s rc=%d log=%s\n",
		    ctx.seed, ctx.current_op,
		    ctx.backend == NEXTUFS_STRESS_BACKEND_FUSE ? "fuse" : "offline",
		    ctx.root_path,
		    ctx.last_op[0] != '\0' ? ctx.last_op : "<unset>", fail_rc,
		    log_path);
		nextufs_stress_cleanup(&ctx);
		return 1;
	}
	printf("nextufs_stress: ok seed=%" PRIu64 " ops=%u backend=%s root=%s\n",
	    ctx.seed, ctx.op_count,
	    ctx.backend == NEXTUFS_STRESS_BACKEND_FUSE ? "fuse" : "offline",
	    ctx.root_path);
	nextufs_stress_cleanup(&ctx);
	return 0;
}

#ifndef NEXTUFS_NO_STANDALONE
int
main(int argc, char **argv)
{
	return nextufs_stress_main(argc, argv);
}
#endif
