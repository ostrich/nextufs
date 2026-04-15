#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/param.h>
#include <ufs/fs.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include <ufs/inode.h>
#define KERNEL
#include <ufs/fsdir.h>
#undef KERNEL
#include <sys/stat.h>

#include "fsck.h"
#include "nextufs.h"

static char g_prepared_source_path[MAXPATHLEN];

static int
fsck_dirname(char *out, size_t out_size, const char *path)
{
	const char *slash;
	size_t len;

	slash = strrchr(path, '/');
	if (slash == NULL) {
		if (snprintf(out, out_size, ".") >= (int)out_size)
			return -ENAMETOOLONG;
		return 0;
	}
	len = slash == path ? 1 : (size_t)(slash - path);
	if (len + 1 > out_size)
		return -ENAMETOOLONG;
	memcpy(out, path, len);
	out[len] = '\0';
	return 0;
}

int
fsck_prepare_source(const char *source, char *prepared_path,
    size_t prepared_path_size, int *forced_readonly_out)
{
	struct stat st;
	struct nextufs_image img;
	struct nextufs_probe_info probe;
	char repo_scratch[MAXPATHLEN];
	char cwd_scratch[MAXPATHLEN];
	char source_dir[MAXPATHLEN];
	char tmpl[PATH_MAX];
	const char *tmpdir;
	int fd;
	int rc;

	if (forced_readonly_out != NULL)
		*forced_readonly_out = 0;
	g_prepared_source_path[0] = '\0';
	preparedsource = 0;
	if (stat(source, &st) < 0)
		return -errno;
	if (!S_ISREG(st.st_mode)) {
		if (snprintf(prepared_path, prepared_path_size, "%s", source) >=
		    (int)prepared_path_size)
			return -ENAMETOOLONG;
		return 0;
	}
	rc = nextufs_image_open(&img, source);
	if (rc < 0) {
		if (snprintf(prepared_path, prepared_path_size, "%s", source) >=
		    (int)prepared_path_size)
			return -ENAMETOOLONG;
		return 0;
	}
	nextufs_probe_info_get(&img, &probe);
	nextufs_image_close(&img);
	if (!probe.source_is_container && probe.slice_base == 0 &&
	    probe.slice_size == probe.image_size) {
		if (snprintf(prepared_path, prepared_path_size, "%s", source) >=
		    (int)prepared_path_size)
			return -ENAMETOOLONG;
		return 0;
	}
	tmpdir = getenv("TMPDIR");
	if (tmpdir == NULL || tmpdir[0] == '\0')
		tmpdir = NULL;
	if (tmpdir != NULL &&
	    snprintf(tmpl, sizeof(tmpl), "%s/fsck.nextufs.XXXXXX", tmpdir) <
	    (int)sizeof(tmpl)) {
		fd = mkstemp(tmpl);
		if (fd < 0 && errno != ENOSPC && errno != EDQUOT)
			return -errno;
	} else {
		fd = -1;
	}
	if (fd < 0) {
		(void)mkdir("fsck/.scratch", 0700);
		if (snprintf(repo_scratch, sizeof(repo_scratch),
		    "fsck/.scratch/fsck.nextufs.XXXXXX") < (int)sizeof(repo_scratch)) {
			memcpy(tmpl, repo_scratch, strlen(repo_scratch) + 1);
			fd = mkstemp(tmpl);
			if (fd < 0 && errno != ENOSPC && errno != EDQUOT)
				return -errno;
		}
	}
	if (fd < 0) {
		(void)mkdir(".scratch", 0700);
		if (snprintf(cwd_scratch, sizeof(cwd_scratch),
		    ".scratch/fsck.nextufs.XXXXXX") < (int)sizeof(cwd_scratch)) {
			memcpy(tmpl, cwd_scratch, strlen(cwd_scratch) + 1);
			fd = mkstemp(tmpl);
			if (fd < 0 && errno != ENOSPC && errno != EDQUOT)
				return -errno;
		}
	}
	if (fd < 0) {
		rc = fsck_dirname(source_dir, sizeof(source_dir), source);
		if (rc < 0)
			return rc;
		if (snprintf(tmpl, sizeof(tmpl), "%s/.fsck.nextufs.XXXXXX",
		    source_dir) >= (int)sizeof(tmpl))
			return -ENAMETOOLONG;
		fd = mkstemp(tmpl);
		if (fd < 0)
			return -errno;
	}
	rc = nextufs_source_extract_slice(source, fd);
	if (rc < 0) {
		(void)close(fd);
		(void)unlink(tmpl);
		return rc;
	}
	if (fsync(fd) < 0) {
		rc = -errno;
		(void)close(fd);
		(void)unlink(tmpl);
		return rc;
	}
	if (close(fd) < 0) {
		rc = -errno;
		(void)unlink(tmpl);
		return rc;
	}
	if (snprintf(prepared_path, prepared_path_size, "%s", tmpl) >=
	    (int)prepared_path_size) {
		(void)unlink(tmpl);
		return -ENAMETOOLONG;
	}
	if (snprintf(g_prepared_source_path, sizeof(g_prepared_source_path), "%s",
	    tmpl) >= (int)sizeof(g_prepared_source_path)) {
		(void)unlink(tmpl);
		return -ENAMETOOLONG;
	}
	preparedsource = 1;
	if (forced_readonly_out != NULL)
		*forced_readonly_out = 1;
	return 0;
}

void
fsck_cleanup_prepared_source(void)
{
	if (g_prepared_source_path[0] != '\0')
		(void)unlink(g_prepared_source_path);
	g_prepared_source_path[0] = '\0';
	preparedsource = 0;
}
