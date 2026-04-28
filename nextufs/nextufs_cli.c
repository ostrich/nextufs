#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NEXTUFS_VERSION "0.1.0-dev"

int nextufs_fsck_main(int argc, char **argv);
int nextufs_mount_main(int argc, char **argv);
int nextufs_mkfile_main(int argc, char **argv);
int nextufs_mkimg_main(int argc, char **argv);
int nextufs_probe_main(int argc, char **argv);
int nextufs_resize_main(int argc, char **argv);
int nextufs_stress_main(int argc, char **argv);

static void
usage(FILE *out)
{
	fprintf(out, "usage: nextufs [global-options] <command> [args...]\n\n");
	fprintf(out, "Global options:\n");
	fprintf(out, "  -h, --help     show this help\n");
	fprintf(out, "  --version      show version\n");
	fprintf(out, "  --json         request JSON output from supported commands\n");
	fprintf(out, "\n");
	fprintf(out, "Commands:\n");
	fprintf(out, "  fsck     check and repair filesystems\n");
	fprintf(out, "  inspect  inspect a source image or path inside it\n");
	fprintf(out, "  mount    mount a source image with FUSE\n");
	fprintf(out, "  mkfile   apply offline file/directory mutations\n");
	fprintf(out, "  mkimg    create raw or labeled UFS images\n");
	fprintf(out, "  resize   grow images offline\n");
	fprintf(out, "  stress   run mutation stress tests\n");
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

static void
command_usage(FILE *out, const char *cmd)
{
	if (strcmp(cmd, "fsck") == 0) {
		fprintf(out, "usage: nextufs fsck [-n|-y] <source> [...]\n");
		return;
	}
	if (strcmp(cmd, "inspect") == 0) {
		fprintf(out, "usage: nextufs inspect [--json] <source> [path]\n");
		return;
	}
	if (strcmp(cmd, "mount") == 0) {
		fprintf(out,
		    "usage: nextufs mount <source> <mountpoint> [fuse options]\n");
		return;
	}
	if (strcmp(cmd, "mkfile") == 0) {
		fprintf(out,
		    "usage: nextufs mkfile [options] <source> <path> <contents>\n");
		fprintf(out,
		    "       nextufs mkfile [options] --<operation> <source> ...\n");
		return;
	}
	if (strcmp(cmd, "mkimg") == 0) {
		fprintf(out,
		    "usage: nextufs mkimg [options] <target> <size> [raw-geometry-args...]\n");
		return;
	}
	if (strcmp(cmd, "resize") == 0) {
		fprintf(out,
		    "usage: nextufs resize grow [--force-size] <source> <size>\n");
		return;
	}
	if (strcmp(cmd, "stress") == 0) {
		fprintf(out,
		    "usage: nextufs stress [--seed n] [--ops n] [--backend offline|fuse] <source>\n");
		return;
	}
	fprintf(out, "nextufs: unknown command '%s'\n", cmd);
	usage(out);
}

static int
dispatch_command(int argc, char **argv)
{
	const char *cmd = argv[0];

	if (argc >= 2 && is_help_arg(argv[1])) {
		command_usage(stdout, cmd);
		return 0;
	}
	if (strcmp(cmd, "mount") == 0)
		return nextufs_mount_main(argc, argv);
	if (strcmp(cmd, "fsck") == 0)
		return nextufs_fsck_main(argc, argv);
	if (strcmp(cmd, "inspect") == 0)
		return nextufs_probe_main(argc, argv);
	if (strcmp(cmd, "mkfile") == 0)
		return nextufs_mkfile_main(argc, argv);
	if (strcmp(cmd, "mkimg") == 0)
		return nextufs_mkimg_main(argc, argv);
	if (strcmp(cmd, "resize") == 0)
		return nextufs_resize_main(argc, argv);
	if (strcmp(cmd, "stress") == 0)
		return nextufs_stress_main(argc, argv);
	fprintf(stderr, "nextufs: unknown command '%s'\n", cmd);
	usage(stderr);
	return 2;
}

static int
dispatch_command_with_json(int argc, char **argv)
{
	char **json_argv;
	int i;
	int rc;

	if (strcmp(argv[0], "inspect") != 0) {
		fprintf(stderr, "nextufs: --json is only supported by inspect\n");
		return 2;
	}
	json_argv = calloc((size_t)argc + 2U, sizeof(*json_argv));
	if (json_argv == NULL) {
		fprintf(stderr, "nextufs: out of memory\n");
		return 1;
	}
	json_argv[0] = argv[0];
	json_argv[1] = "--json";
	for (i = 1; i < argc; i++)
		json_argv[i + 1] = argv[i];
	rc = dispatch_command(argc + 1, json_argv);
	free(json_argv);
	return rc;
}

int
main(int argc, char **argv)
{
	int argi = 1;
	int json = 0;

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
		if (strcmp(argv[argi], "--json") == 0) {
			json = 1;
			argi++;
			continue;
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
	if (json)
		return dispatch_command_with_json(argc - argi, argv + argi);
	return dispatch_command(argc - argi, argv + argi);
}
