#include "nextufs_write.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct nextufs_write_ctx g_nextufs_editor_ctx = {
	.policy = NEXTUFS_WRITE_EDITOR,
	.uid = 0,
	.gid = 0,
	.groups = NULL,
	.group_count = 0,
};

static int
parse_ulong_arg(const char *s, unsigned long *out)
{
	char *end;
	unsigned long v;

	if (s == NULL || s[0] == '\0' || s[0] == '-')
		return -1;
	errno = 0;
	v = strtoul(s, &end, 0);
	if (errno != 0 || end == s || *end != '\0')
		return -1;
	*out = v;
	return 0;
}

static int
parse_id_arg(const char *s, uid_t *out)
{
	unsigned long v;

	if (strcmp(s, "-1") == 0 || strcmp(s, "keep") == 0) {
		*out = (uid_t)-1;
		return 0;
	}
	if (parse_ulong_arg(s, &v) < 0)
		return -1;
	if (v > (unsigned long)UINT_MAX)
		return -1;
	*out = (uid_t)v;
	return 0;
}

static int
parse_gid_arg(const char *s, gid_t *out)
{
	uid_t tmp;

	if (parse_id_arg(s, &tmp) < 0)
		return -1;
	*out = (gid_t)tmp;
	return 0;
}

static int
parse_global_ctx_args(int argc, char **argv, int *argi,
    struct nextufs_write_ctx *ctx)
{
	unsigned long v;

	*ctx = g_nextufs_editor_ctx;
	while (*argi < argc) {
		if (strcmp(argv[*argi], "--policy") == 0) {
			if (*argi + 1 >= argc)
				return -1;
			if (strcmp(argv[*argi + 1], "editor") == 0)
				ctx->policy = NEXTUFS_WRITE_EDITOR;
			else if (strcmp(argv[*argi + 1], "permissions") == 0)
				ctx->policy = NEXTUFS_WRITE_PERMISSIONS;
			else
				return -1;
			*argi += 2;
			continue;
		}
		if (strcmp(argv[*argi], "--uid") == 0) {
			if (*argi + 1 >= argc || parse_ulong_arg(argv[*argi + 1], &v) < 0)
				return -1;
			ctx->uid = (uid_t)v;
			*argi += 2;
			continue;
		}
		if (strcmp(argv[*argi], "--gid") == 0) {
			if (*argi + 1 >= argc || parse_ulong_arg(argv[*argi + 1], &v) < 0)
				return -1;
			ctx->gid = (gid_t)v;
			*argi += 2;
			continue;
		}
		break;
	}
	return 0;
}

int
main(int argc, char **argv)
{
	struct nextufs_write_ctx ctx;
	uid_t uid;
	gid_t gid;
	int rc;
	int argi = 1;

	if (parse_global_ctx_args(argc, argv, &argi, &ctx) < 0) {
		fprintf(stderr, "nextufs_mkfile: invalid global args\n");
		return 2;
	}

	if (argc - argi == 3 && strcmp(argv[argi], "--unlink") == 0) {
		rc = nextufs_unlink_path(&ctx, argv[argi + 1], argv[argi + 2]);
		if (rc < 0) {
			fprintf(stderr, "nextufs_mkfile: failed: %d\n", rc);
			return 1;
		}
		return 0;
	}
	if (argc - argi == 3 && strcmp(argv[argi], "--mkdir") == 0) {
		rc = nextufs_mkdir_path(&ctx, argv[argi + 1], argv[argi + 2], 0777);
		if (rc < 0) {
			fprintf(stderr, "nextufs_mkfile: failed: %d\n", rc);
			return 1;
		}
		return 0;
	}
	if (argc - argi == 4 && strcmp(argv[argi], "--overwrite") == 0) {
		rc = nextufs_overwrite_file(&ctx, argv[argi + 1], argv[argi + 2], argv[argi + 3],
		    strlen(argv[argi + 3]));
		if (rc < 0) {
			fprintf(stderr, "nextufs_mkfile: failed: %d\n", rc);
			return 1;
		}
		return 0;
	}
	if (argc - argi == 4 && strcmp(argv[argi], "--append") == 0) {
		rc = nextufs_append_file(&ctx, argv[argi + 1], argv[argi + 2], argv[argi + 3],
		    strlen(argv[argi + 3]));
		if (rc < 0) {
			fprintf(stderr, "nextufs_mkfile: failed: %d\n", rc);
			return 1;
		}
		return 0;
	}
	if (argc - argi == 3 && strcmp(argv[argi], "--rmdir") == 0) {
		rc = nextufs_rmdir_path(&ctx, argv[argi + 1], argv[argi + 2]);
		if (rc < 0) {
			fprintf(stderr, "nextufs_mkfile: failed: %d\n", rc);
			return 1;
		}
		return 0;
	}
	if (argc - argi == 4 && strcmp(argv[argi], "--link") == 0) {
		rc = nextufs_link_path(&ctx, argv[argi + 1], argv[argi + 2], argv[argi + 3]);
		if (rc < 0) {
			fprintf(stderr, "nextufs_mkfile: failed: %d\n", rc);
			return 1;
		}
		return 0;
	}
	if (argc - argi == 4 && strcmp(argv[argi], "--rename") == 0) {
		rc = nextufs_rename_path(&ctx, argv[argi + 1], argv[argi + 2], argv[argi + 3]);
		if (rc < 0) {
			fprintf(stderr, "nextufs_mkfile: failed: %d\n", rc);
			return 1;
		}
		return 0;
	}
	if (argc - argi == 4 && strcmp(argv[argi], "--symlink") == 0) {
		rc = nextufs_symlink_path(&ctx, argv[argi + 1], argv[argi + 2], argv[argi + 3]);
		if (rc < 0) {
			fprintf(stderr, "nextufs_mkfile: failed: %d\n", rc);
			return 1;
		}
		return 0;
	}
	if (argc - argi == 4 && strcmp(argv[argi], "--truncate") == 0) {
		rc = nextufs_truncate_path(&ctx, argv[argi + 1], argv[argi + 2],
		    (uint64_t)strtoull(argv[argi + 3], NULL, 0));
		if (rc < 0) {
			fprintf(stderr, "nextufs_mkfile: failed: %d\n", rc);
			return 1;
		}
		return 0;
	}
	if (argc - argi == 5 && strcmp(argv[argi], "--pwrite") == 0) {
		rc = nextufs_pwrite_path(&ctx, argv[argi + 1], argv[argi + 2], argv[argi + 4],
		    strlen(argv[argi + 4]), (uint64_t)strtoull(argv[argi + 3], NULL, 0));
		if (rc < 0) {
			fprintf(stderr, "nextufs_mkfile: failed: %d\n", rc);
			return 1;
		}
		return 0;
	}
	if (argc - argi == 5 && strcmp(argv[argi], "--mknod") == 0) {
		rc = nextufs_mknod_path(&ctx, argv[argi + 1], argv[argi + 2],
		    (uint16_t)strtoul(argv[argi + 3], NULL, 8),
		    (uint32_t)strtoul(argv[argi + 4], NULL, 0));
		if (rc < 0) {
			fprintf(stderr, "nextufs_mkfile: failed: %d\n", rc);
			return 1;
		}
		return 0;
	}
	if (argc - argi == 4 && strcmp(argv[argi], "--chmod") == 0) {
		rc = nextufs_chmod_path(&ctx, argv[argi + 1], argv[argi + 2],
		    (uint16_t)strtoul(argv[argi + 3], NULL, 8));
		if (rc < 0) {
			fprintf(stderr, "nextufs_mkfile: failed: %d\n", rc);
			return 1;
		}
		return 0;
	}
	if (argc - argi == 5 && strcmp(argv[argi], "--chown") == 0) {
		if (parse_id_arg(argv[argi + 3], &uid) < 0 ||
		    parse_gid_arg(argv[argi + 4], &gid) < 0) {
			fprintf(stderr, "nextufs_mkfile: invalid chown ids\n");
			return 2;
		}
		rc = nextufs_chown_path(&ctx, argv[argi + 1], argv[argi + 2], uid, gid);
		if (rc < 0) {
			fprintf(stderr, "nextufs_mkfile: failed: %d\n", rc);
			return 1;
		}
		return 0;
	}
	if (argc - argi == 5 && strcmp(argv[argi], "--utimes") == 0) {
		rc = nextufs_utimes_path(&ctx, argv[argi + 1], argv[argi + 2],
		    (uint32_t)strtoul(argv[argi + 3], NULL, 0),
		    (uint32_t)strtoul(argv[argi + 4], NULL, 0));
		if (rc < 0) {
			fprintf(stderr, "nextufs_mkfile: failed: %d\n", rc);
			return 1;
		}
		return 0;
	}
	if (argc - argi == 4 && strcmp(argv[argi], "--from-file") == 0) {
		rc = nextufs_create_file_from_hostfile(&ctx, argv[argi + 1], argv[argi + 2], argv[argi + 3]);
		if (rc < 0) {
			fprintf(stderr, "nextufs_mkfile: failed: %d\n", rc);
			return 1;
		}
		return 0;
	}
	if (argc - argi != 3) {
		fprintf(stderr,
		    "usage: %s [--policy editor|permissions] [--uid n] [--gid n] <raw-image> <path> <contents>\n"
		    "       %s [global-opts] --unlink <raw-image> <path>\n"
		    "       %s [global-opts] --mkdir <raw-image> <path>\n"
		    "       %s [global-opts] --overwrite <raw-image> <path> <contents>\n"
		    "       %s [global-opts] --append <raw-image> <path> <contents>\n"
		    "       %s [global-opts] --rmdir <raw-image> <path>\n"
		    "       %s [global-opts] --link <raw-image> <source-path> <target-path>\n"
		    "       %s [global-opts] --rename <raw-image> <source-path> <target-path>\n"
		    "       %s [global-opts] --symlink <raw-image> <target> <link-path>\n"
		    "       %s [global-opts] --truncate <raw-image> <path> <size>\n"
		    "       %s [global-opts] --pwrite <raw-image> <path> <offset> <contents>\n"
		    "       %s [global-opts] --mknod <raw-image> <path> <octal-mode> <rdev>\n"
		    "       %s [global-opts] --chmod <raw-image> <path> <octal-mode>\n"
		    "       %s [global-opts] --chown <raw-image> <path> <uid|keep> <gid|keep>\n"
		    "       %s [global-opts] --utimes <raw-image> <path> <atime> <mtime>\n"
		    "       %s [global-opts] --from-file <raw-image> <path> <host-file>\n",
		    argv[0], argv[0], argv[0], argv[0], argv[0], argv[0],
		    argv[0], argv[0], argv[0], argv[0], argv[0], argv[0],
		    argv[0], argv[0], argv[0], argv[0]);
		return 2;
	}
	rc = nextufs_create_small_file(&ctx, argv[argi], argv[argi + 1], argv[argi + 2],
	    strlen(argv[argi + 2]));
	if (rc < 0) {
		fprintf(stderr, "nextufs_mkfile: failed: %d\n", rc);
		return 1;
	}
	return 0;
}
