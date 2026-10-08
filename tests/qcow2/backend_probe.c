/* Exercises backend byte I/O independently of UFS; fault wrappers are test-only. */
#include "nextufs_internal.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

ssize_t __real_pwrite(int, const void *, size_t, off_t);
ssize_t __real_pread(int, void *, size_t, off_t);
int __real_fsync(int);
int __real_ftruncate(int, off_t);

static int
fail_call(const char *name, unsigned *calls)
{
	const char *value = getenv(name);
	(*calls)++;
	if (value && *calls == strtoul(value, NULL, 10)) {
		errno = EIO;
		return 1;
	}
	return 0;
}

ssize_t
__wrap_pwrite(int fd, const void *buf, size_t size, off_t off)
{
	static unsigned calls;
	if (fail_call("QCOW_FAIL_PWRITE", &calls))
		return -1;
	return __real_pwrite(fd, buf, size, off);
}

ssize_t
__wrap_pread(int fd, void *buf, size_t size, off_t off)
{
	static unsigned calls;
	if (fail_call("QCOW_FAIL_PREAD", &calls))
		return -1;
	return __real_pread(fd, buf, size, off);
}

int
__wrap_fsync(int fd)
{
	static unsigned calls;
	if (fail_call("QCOW_FAIL_FSYNC", &calls))
		return -1;
	return __real_fsync(fd);
}

int
__wrap_ftruncate(int fd, off_t length)
{
	static unsigned calls;
	if (fail_call("QCOW_FAIL_FTRUNCATE", &calls))
		return -1;
	return __real_ftruncate(fd, length);
}

int
main(int argc, char **argv)
{
	struct nextufs_image img = { .fd = -1 };
	uint8_t *buf;
	size_t bytes, i;
	off_t off;
	int writing, rc;
	if (argc != 5)
		return 2;
	writing = strcmp(argv[1], "read") != 0;
	off = (off_t)strtoll(argv[3], NULL, 0);
	bytes = (size_t)strtoull(argv[4], NULL, 0);
	buf = malloc(bytes ? bytes : 1);
	if (!buf)
		return 2;
	rc = nextufs__qcow2_open(&img, argv[2], writing);
	if (rc < 0)
		goto out;
	if (writing) {
		for (i = 0; i < bytes; i++)
			buf[i] = (uint8_t)((i * 17 + 31) % 251);
		rc = nextufs_image_pwrite(&img, buf, bytes, off);
		if (!rc)
			rc = nextufs_image_fsync(&img);
	} else {
		rc = nextufs_image_pread(&img, buf, bytes, off);
		if (!rc && fwrite(buf, 1, bytes, stdout) != bytes)
			rc = -EIO;
	}
	if (!rc && fflush(stdout) == EOF)
		rc = -EIO;
out:
	if (rc < 0)
		fprintf(stderr, "backend probe: %s\n", strerror(-rc));
	nextufs_image_close(&img);
	free(buf);
	return rc < 0;
}
