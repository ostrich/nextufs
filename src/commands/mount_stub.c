#include <stdio.h>
#include <string.h>

int
nextufs_mount_main(int argc, char **argv)
{
	if (argc == 2 &&
	    (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h"))) {
		puts("usage: nextufs mount <source> <mountpoint> [fuse "
		     "options]\nMount support is unavailable in this offline "
		     "build.");
		return 0;
	}
	(void)argv;
	fprintf(stderr, "nextufs: mount support is unavailable in this offline build\n");
	return 2;
}
