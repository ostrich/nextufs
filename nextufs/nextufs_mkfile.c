#include "nextufs_write.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
main(int argc, char **argv)
{
	int rc;

	if (argc == 4 && strcmp(argv[1], "--unlink") == 0) {
		rc = nextufs_unlink_path_rw(argv[2], argv[3]);
		if (rc < 0) {
			fprintf(stderr, "nextufs_mkfile: failed: %d\n", rc);
			return 1;
		}
		return 0;
	}
	if (argc == 4 && strcmp(argv[1], "--mkdir") == 0) {
		rc = nextufs_mkdir_path_rw(argv[2], argv[3], 0777);
		if (rc < 0) {
			fprintf(stderr, "nextufs_mkfile: failed: %d\n", rc);
			return 1;
		}
		return 0;
	}
	if (argc == 5 && strcmp(argv[1], "--overwrite") == 0) {
		rc = nextufs_overwrite_file_rw(argv[2], argv[3], argv[4],
		    strlen(argv[4]));
		if (rc < 0) {
			fprintf(stderr, "nextufs_mkfile: failed: %d\n", rc);
			return 1;
		}
		return 0;
	}
	if (argc == 5 && strcmp(argv[1], "--append") == 0) {
		rc = nextufs_append_file_rw(argv[2], argv[3], argv[4],
		    strlen(argv[4]));
		if (rc < 0) {
			fprintf(stderr, "nextufs_mkfile: failed: %d\n", rc);
			return 1;
		}
		return 0;
	}
	if (argc == 4 && strcmp(argv[1], "--rmdir") == 0) {
		rc = nextufs_rmdir_path_rw(argv[2], argv[3]);
		if (rc < 0) {
			fprintf(stderr, "nextufs_mkfile: failed: %d\n", rc);
			return 1;
		}
		return 0;
	}
	if (argc == 5 && strcmp(argv[1], "--link") == 0) {
		rc = nextufs_link_path_rw(argv[2], argv[3], argv[4]);
		if (rc < 0) {
			fprintf(stderr, "nextufs_mkfile: failed: %d\n", rc);
			return 1;
		}
		return 0;
	}
	if (argc == 5 && strcmp(argv[1], "--symlink") == 0) {
		rc = nextufs_symlink_path_rw(argv[2], argv[3], argv[4]);
		if (rc < 0) {
			fprintf(stderr, "nextufs_mkfile: failed: %d\n", rc);
			return 1;
		}
		return 0;
	}
	if (argc == 5 && strcmp(argv[1], "--chmod") == 0) {
		rc = nextufs_chmod_path_rw(argv[2], argv[3],
		    (uint16_t)strtoul(argv[4], NULL, 8));
		if (rc < 0) {
			fprintf(stderr, "nextufs_mkfile: failed: %d\n", rc);
			return 1;
		}
		return 0;
	}
	if (argc == 6 && strcmp(argv[1], "--chown") == 0) {
		rc = nextufs_chown_path_rw(argv[2], argv[3],
		    (uint16_t)strtoul(argv[4], NULL, 0),
		    (uint16_t)strtoul(argv[5], NULL, 0));
		if (rc < 0) {
			fprintf(stderr, "nextufs_mkfile: failed: %d\n", rc);
			return 1;
		}
		return 0;
	}
	if (argc == 6 && strcmp(argv[1], "--utimes") == 0) {
		rc = nextufs_utimes_path_rw(argv[2], argv[3],
		    (uint32_t)strtoul(argv[4], NULL, 0),
		    (uint32_t)strtoul(argv[5], NULL, 0));
		if (rc < 0) {
			fprintf(stderr, "nextufs_mkfile: failed: %d\n", rc);
			return 1;
		}
		return 0;
	}
	if (argc == 5 && strcmp(argv[1], "--from-file") == 0) {
		rc = nextufs_create_file_from_hostfile_rw(argv[2], argv[3], argv[4]);
		if (rc < 0) {
			fprintf(stderr, "nextufs_mkfile: failed: %d\n", rc);
			return 1;
		}
		return 0;
	}
	if (argc != 4) {
		fprintf(stderr,
		    "usage: %s <raw-image> <path> <contents>\n"
		    "       %s --unlink <raw-image> <path>\n"
		    "       %s --mkdir <raw-image> <path>\n"
		    "       %s --overwrite <raw-image> <path> <contents>\n"
		    "       %s --append <raw-image> <path> <contents>\n"
		    "       %s --rmdir <raw-image> <path>\n"
		    "       %s --link <raw-image> <source-path> <target-path>\n"
		    "       %s --symlink <raw-image> <target> <link-path>\n"
		    "       %s --chmod <raw-image> <path> <octal-mode>\n"
		    "       %s --chown <raw-image> <path> <uid> <gid>\n"
		    "       %s --utimes <raw-image> <path> <atime> <mtime>\n"
		    "       %s --from-file <raw-image> <path> <host-file>\n",
		    argv[0], argv[0], argv[0], argv[0], argv[0], argv[0],
		    argv[0], argv[0], argv[0], argv[0], argv[0], argv[0]);
		return 2;
	}
	rc = nextufs_create_small_file_rw(argv[1], argv[2], argv[3],
	    strlen(argv[3]));
	if (rc < 0) {
		fprintf(stderr, "nextufs_mkfile: failed: %d\n", rc);
		return 1;
	}
	return 0;
}
