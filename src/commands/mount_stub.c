#include <stdio.h>

int
nextufs_mount_main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	fprintf(stderr, "nextufs: mount support is unavailable in this offline build\n");
	return 2;
}
