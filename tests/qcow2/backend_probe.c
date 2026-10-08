/* Exercises backend byte I/O independently of UFS; fault wrappers are test-only. */
#include "nextufs_internal.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern int qcow_probe_opened;

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
	qcow_probe_opened = 1;
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
