#include "nextufs_image.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <stdint.h>
#include <unistd.h>

int
nextufs_target_open(const char *path, enum nextufs_open_policy policy,
    mode_t mode, int *fd_out)
{
	int flags;
	int fd = -1;

	if (fd_out == NULL)
		return -EINVAL;
	flags = O_RDWR | O_CREAT;
	switch (policy) {
	case NEXTUFS_OPEN_CREATE:
		flags |= O_EXCL;
		break;
	case NEXTUFS_OPEN_OVERWRITE:
		/* Truncate only after acquiring exclusive ownership. */
		break;
	default:
		return -EINVAL;
	}
	fd = open(path, flags, mode);
	if (fd < 0)
		return -errno;
	if (flock(fd, LOCK_EX | LOCK_NB) < 0 ||
	    (policy == NEXTUFS_OPEN_OVERWRITE && ftruncate(fd, 0) < 0)) {
		int rc = -errno;
		close(fd);
		return rc;
	}
	*fd_out = fd;
	return 0;
}

int
nextufs_target_create_sized(const char *path, enum nextufs_open_policy policy,
    mode_t mode, uint64_t bytes)
{
	int fd = -1;
	int rc;

	rc = nextufs_target_open(path, policy, mode, &fd);
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
