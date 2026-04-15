/* Source preparation for raw files, raw disk images, and VDI containers. */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ufs/fs.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include <ufs/inode.h>
#define KERNEL
#include <ufs/fsdir.h>
#undef KERNEL
#include <mntent.h>
#include "../nextufs/nextufs.h"
#include "fsck.h"

static int
fsck_source_should_stage(const char *path)
{
	struct nextufs_image img;
	struct nextufs_probe_info probe;

	memset(&img, 0, sizeof(img));
	img.fd = -1;
	if (nextufs_image_open(&img, path) < 0)
		return (0);
	nextufs_probe_info_get(&img, &probe);
	nextufs_image_close(&img);
	return probe.source_is_container || probe.slice_base != 0;
}

int
fsck_source_prepare_path(const char *path, char *resolved_path,
    size_t resolved_size, int *force_readonly)
{
	struct stat st;
	int fd;
	const char *tmpdir;

	if (resolved_size == 0)
		return (-1);
	if (force_readonly != NULL)
		*force_readonly = 0;
	if (stat(path, &st) < 0)
		return (-1);
	if ((st.st_mode & S_IFMT) != S_IFREG || !fsck_source_should_stage(path)) {
		strncpy(resolved_path, path, resolved_size - 1);
		resolved_path[resolved_size - 1] = '\0';
		return (0);
	}
	tmpdir = getenv("TMPDIR");
	if (tmpdir == NULL || *tmpdir == '\0')
		tmpdir = "/tmp";
	snprintf(source_path, sizeof(source_path),
	    "%s/fsck.nextufs-source-XXXXXX", tmpdir);
	fd = mkstemp(source_path);
	if (fd < 0)
		return (-1);
	if (nextufs_source_extract_slice(path, fd) < 0) {
		(void)close(fd);
		(void)unlink(source_path);
		source_path[0] = '\0';
		return (-1);
	}
	(void)close(fd);
	source_is_temp = 1;
	source_force_readonly = 1;
	if (force_readonly != NULL)
		*force_readonly = 1;
	strncpy(resolved_path, source_path, resolved_size - 1);
	resolved_path[resolved_size - 1] = '\0';
	return (0);
}

void
fsck_source_cleanup(void)
{
	if (!source_is_temp || source_path[0] == '\0')
		return;
	(void)unlink(source_path);
	source_path[0] = '\0';
	source_is_temp = 0;
	source_force_readonly = 0;
}
