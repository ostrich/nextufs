#include <stdio.h>
#include <string.h>

int nextufs_mount_main(int argc, char **argv);
int nextufs_mkfile_main(int argc, char **argv);
int nextufs_mkimg_main(int argc, char **argv);
int nextufs_probe_main(int argc, char **argv);
int nextufs_resize_main(int argc, char **argv);
int nextufs_stress_main(int argc, char **argv);

static void
usage(FILE *out)
{
	fprintf(out, "usage: nextufs <command> [args...]\n\n");
	fprintf(out, "Commands:\n");
	fprintf(out, "  mount   mount a source image with FUSE\n");
	fprintf(out, "  probe   inspect a source image or path inside it\n");
	fprintf(out, "  mkfile  apply offline file/directory mutations\n");
	fprintf(out, "  mkimg   create raw or labeled UFS images\n");
	fprintf(out, "  resize  analyze or grow images offline\n");
	fprintf(out, "  stress  run mutation stress tests\n");
	fprintf(out, "\n");
	fprintf(out, "Run a command without required arguments to see its usage.\n");
}

int
main(int argc, char **argv)
{
	if (argc < 2 || strcmp(argv[1], "-h") == 0 ||
	    strcmp(argv[1], "--help") == 0) {
		usage(argc < 2 ? stderr : stdout);
		return argc < 2 ? 2 : 0;
	}
	if (strcmp(argv[1], "mount") == 0)
		return nextufs_mount_main(argc - 1, argv + 1);
	if (strcmp(argv[1], "probe") == 0)
		return nextufs_probe_main(argc - 1, argv + 1);
	if (strcmp(argv[1], "mkfile") == 0)
		return nextufs_mkfile_main(argc - 1, argv + 1);
	if (strcmp(argv[1], "mkimg") == 0)
		return nextufs_mkimg_main(argc - 1, argv + 1);
	if (strcmp(argv[1], "resize") == 0)
		return nextufs_resize_main(argc - 1, argv + 1);
	if (strcmp(argv[1], "stress") == 0)
		return nextufs_stress_main(argc - 1, argv + 1);
	fprintf(stderr, "nextufs: unknown command '%s'\n", argv[1]);
	usage(stderr);
	return 2;
}
