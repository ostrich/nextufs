#include "nextufs.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>

int
nextufs_target_open(const char *path, enum nextufs_target_access access,
    mode_t mode, int *fd_out)
{
	int flags;
	int fd = -1;

	if (fd_out == NULL)
		return -EINVAL;
	flags = O_RDWR | O_CREAT;
	switch (access) {
	case NEXTUFS_TARGET_CREATE_NEW:
		flags |= O_EXCL;
		break;
	case NEXTUFS_TARGET_OVERWRITE:
		flags |= O_TRUNC;
		break;
	default:
		return -EINVAL;
	}
	fd = open(path, flags, mode);
	if (fd < 0)
		return -errno;
	*fd_out = fd;
	return 0;
}

int
nextufs_target_create_sized(const char *path, enum nextufs_target_access access,
    mode_t mode, uint64_t bytes)
{
	int fd = -1;
	int rc;

	rc = nextufs_target_open(path, access, mode, &fd);
	if (rc < 0)
		return rc;
	if (ftruncate(fd, (off_t)bytes) < 0) {
		rc = -errno;
		close(fd);
		return rc;
	}
	if (close(fd) < 0)
		return -errno;
	return 0;
}
