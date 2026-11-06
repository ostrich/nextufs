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
