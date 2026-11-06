#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "commands.h"
#include "nextufs_size.h"
#include <limits.h>
#include "nextufs_fsck.h"

static int
parse_fsck_args(int argc, char **argv, struct nextufs_fsck_request *request)
{
	int argi;

	*request = (struct nextufs_fsck_request){ 0 };
	for (argi = 1; argi < argc && argv[argi][0] == '-'; argi++) {
		char *opt;
		uint64_t number;

		opt = argv[argi] + 1;
		if (*opt != 'b' && opt[1])
			return 2;
		switch (*opt) {
		case 'p':
			request->options.opt_preen++;
			break;
		case 'P':
			request->options.opt_force_preen++;
			request->options.opt_preen++;
			break;
		case 'b':
			if (opt[1] != '\0') {
				if (nextufs_parse_number(opt + 1, 10, INT_MAX,
							 &number) < 0 ||
				    !number)
					return 2;
				request->options.opt_alternate_superblock =
				    (int)number;
			} else {
				if (argi + 1 >= argc) {
					fprintf(stderr, "-b requires an argument\n");
					return 2;
				}
				if (nextufs_parse_number(argv[++argi], 10,
							 INT_MAX,
							 &number) < 0 ||
				    !number)
					return 2;
				request->options.opt_alternate_superblock =
				    (int)number;
			}
			printf("Alternate super block location: %d\n",
			    request->options.opt_alternate_superblock);
			break;
		case 'd':
			request->options.opt_debug++;
			break;
		case 'n':
		case 'N':
			request->options.opt_no++;
			request->options.opt_yes = 0;
			break;
		case 'y':
		case 'Y':
			request->options.opt_yes++;
			request->options.opt_no = 0;
			break;
		default:
			fprintf(stderr, "%c option?\n", *opt);
			return 2;
		}
	}
	request->source_count = argc - argi;
	request->sources = argv + argi;
	return 0;
}

int
nextufs_fsck_main(int argc, char **argv)
{
	if (argc == 2 &&
	    (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h"))) {
		fputs("usage: nextufs fsck [-n|-y] <source> [...]\n\n-n  check "
		      "without writing\n-y  repair with yes to prompts\n-p  "
		      "preen\n-P  force preen even if clean\n-b sector  use an "
		      "alternate superblock\n-d  debug output\n",
		      stdout);
		return 0;
	}
	struct nextufs_fsck_request request;
	int rc;

	rc = parse_fsck_args(argc, argv, &request);
	if (rc != 0)
		return rc;
	return nextufs_fsck_run(&request);
}
