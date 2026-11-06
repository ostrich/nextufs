#include "nextufs_mutate.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
main(int argc, char **argv)
{
	struct nextufs_image img;
	struct nextufs_write_session session;
	struct nextufs_write_ctx ctx = {.policy = NEXTUFS_WRITE_SU,
					.session = &session};
	int rc;
	if (argc == 5 && (!strcmp(argv[1], "bytes-read") ||
			 !strcmp(argv[1], "bytes-write"))) {
		size_t size = strtoull(argv[4], NULL, 0);
		off_t offset = strtoll(argv[3], NULL, 0);
		int writing = !strcmp(argv[1], "bytes-write");
		unsigned char *buf = malloc(size ? size : 1);
		if (!buf)
			return 1;
		rc = writing ? nextufs_image_open_rw(&img, argv[2]) :
		    nextufs_image_open(&img, argv[2]);
		if (!rc) {
			for (size_t i = 0; i < size; i++)
				buf[i] = (i * 17 + 31) % 251;
			rc = writing ? nextufs_image_pwrite(&img, buf, size, offset) :
			    nextufs_image_pread(&img, buf, size, offset);
			if (!rc && writing)
				rc = nextufs_image_fsync(&img);
			if (!rc && !writing && fwrite(buf, 1, size, stdout) != size)
				rc = -EIO;
			nextufs_image_close(&img);
		}
		free(buf);
		return rc < 0;
	}
	if (argc == 6 && !strcmp(argv[1], "read")) {
		unsigned char bytes[64];
		size_t got;
		rc = nextufs_image_open(&img, argv[2]);
		if (rc < 0)
			return 1;
		rc = nextufs_path_read(&img, argv[3],
				       strtoull(argv[4], NULL, 10), bytes,
				       sizeof(bytes), &got);
		nextufs_image_close(&img);
		if (rc < 0 || got != 64)
			return 1;
		for (size_t i = 0; i < got; i++)
			if (bytes[i])
				return 1;
		return 0;
	}
	if (argc != 3)
		return 2;
	rc = nextufs_write_session_open(&session, argv[2]);
	if (rc < 0)
		return 1;
	rc = nextufs_image_open_rw(&img, argv[2]);
	if (rc != -EWOULDBLOCK && rc != -EAGAIN)
		return 1;
	rc = nextufs_path_pwrite(&ctx, argv[2], "/file", "X", 1, 0);
	if (!strcmp(argv[1], "poison")) {
		if (rc != -EUCLEAN || !session.failed)
			return 1;
		if (nextufs_path_pwrite(&ctx, argv[2], "/file", "Y", 1, 0) !=
		    -EIO)
			return 1;
	} else {
		if (rc < 0 || session.failed)
			return 1;
		rc = nextufs_path_pwrite(&ctx, argv[2], "/file", "Y", 1, 1);
		if (rc < 0)
			return 1;
	}
	nextufs_write_session_close(&session);
	return 0;
}
