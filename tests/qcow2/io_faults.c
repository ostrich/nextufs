/* Test-only linker wrappers, never linked into production nextufs. */
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

ssize_t __real_pwrite(int, const void *, size_t, off_t);
ssize_t __real_pread(int, void *, size_t, off_t);
int __real_fsync(int);
int __real_ftruncate(int, off_t);

int qcow_probe_opened;

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
	static unsigned calls, after_open;
	if ((qcow_probe_opened && fail_call("QCOW_FAIL_PREAD_AFTER_OPEN", &after_open)) ||
	    fail_call("QCOW_FAIL_PREAD", &calls))
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

