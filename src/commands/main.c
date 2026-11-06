#include <stdio.h>
#include <string.h>

#include "commands.h"

#include "build_version.h"

static void
usage(FILE *out)
{
	fprintf(out, "usage: nextufs [global-options] <command> [args...]\n\n");
	fprintf(out, "Global options:\n");
	fprintf(out, "  -h, --help     show this help\n");
	fprintf(out, "  --version      show version\n");
	fprintf(out, "\n");
	fprintf(out, "Commands:\n");
	fprintf(out, "  browse   browse paths inside a filesystem\n");
	fprintf(out, "  fsck     check and repair filesystems\n");
	fprintf(out, "  info     show source and filesystem information\n");
	fprintf(out, "  mount    mount a source image with FUSE\n");
	fprintf(out, "  mkfile   apply offline file/directory mutations\n");
	fprintf(out, "  mkimg    create raw or labeled UFS images\n");
	fprintf(out, "  resize   grow images offline\n");
	fprintf(out, "\n");
	fprintf(out, "Run 'nextufs <command> --help' for command-specific usage.\n");
}

static void
version(FILE *out)
{
	fprintf(out, "nextufs %s\n", NEXTUFS_VERSION);
}

static int
is_help_arg(const char *arg)
{
	return strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0;
}

static int
dispatch_command(int argc, char **argv)
{
	const char *cmd = argv[0];

	if (strcmp(cmd, "browse") == 0)
		return nextufs_browse_main(argc, argv);
	if (strcmp(cmd, "mount") == 0)
		return nextufs_mount_main(argc, argv);
	if (strcmp(cmd, "fsck") == 0)
		return nextufs_fsck_main(argc, argv);
	if (strcmp(cmd, "info") == 0)
		return nextufs_info_main(argc, argv);
	if (strcmp(cmd, "mkfile") == 0)
		return nextufs_mkfile_main(argc, argv);
	if (strcmp(cmd, "mkimg") == 0)
		return nextufs_mkimg_main(argc, argv);
	if (strcmp(cmd, "resize") == 0)
		return nextufs_resize_main(argc, argv);
	fprintf(stderr, "nextufs: unknown command '%s'\n", cmd);
	usage(stderr);
	return 2;
}

int
main(int argc, char **argv)
{
	int argi = 1;

	if (argc < 2) {
		usage(argc < 2 ? stderr : stdout);
		return argc < 2 ? 2 : 0;
	}
	while (argi < argc && argv[argi][0] == '-') {
		if (is_help_arg(argv[argi])) {
			usage(stdout);
			return 0;
		}
		if (strcmp(argv[argi], "--version") == 0) {
			version(stdout);
			return 0;
		}
		fprintf(stderr, "nextufs: unknown global option '%s'\n",
		    argv[argi]);
		usage(stderr);
		return 2;
	}
	if (argi >= argc) {
		usage(stderr);
		return 2;
	}
	return dispatch_command(argc - argi, argv + argi);
}
