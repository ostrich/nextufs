#include "nextufs.h"
#include "nextufs_inspect.h"
#include "nextufs_report.h"

#include <stdio.h>
#include <string.h>

int
nextufs_info_main(int argc, char **argv)
{
	struct nextufs_image img;
	struct nextufs_inspect_info info;
	int json = 0;
	int argi = 1;
	int rc;

	if (argc > argi && strcmp(argv[argi], "--json") == 0) {
		json = 1;
		argi++;
	}
	if (argc != argi + 1) {
		fprintf(stderr, "usage: %s [--json] <source>\n", argv[0]);
		return 1;
	}
	rc = nextufs_image_open_source(&img, argv[argi],
	    NEXTUFS_SOURCE_READ_ONLY);
	if (rc < 0) {
		fprintf(stderr, "failed to open source %s\n", argv[argi]);
		return 1;
	}
	nextufs_inspect_collect(&img, 0, &info);
	if (json) {
		nextufs_report_inspect_json(stdout, argv[argi], &info);
		nextufs_image_close(&img);
		return 0;
	}
	nextufs_report_inspect_text(stdout, argv[argi], &info);
	nextufs_image_close(&img);
	return 0;
}

#ifndef NEXTUFS_NO_STANDALONE
int
main(int argc, char **argv)
{
	return nextufs_info_main(argc, argv);
}
#endif
