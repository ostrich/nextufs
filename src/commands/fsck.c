#include <stdio.h>
#include <stdlib.h>

#include "commands.h"
#include "nextufs_fsck.h"

static int
parse_fsck_args(int argc, char **argv, struct nextufs_fsck_request *request)
{
	int argi;

	*request = (struct nextufs_fsck_request){ 0 };
	for (argi = 1; argi < argc && argv[argi][0] == '-'; argi++) {
		char *opt;

		opt = argv[argi] + 1;
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
				request->options.opt_alternate_superblock =
				    atoi(opt + 1);
			} else {
				if (argi + 1 >= argc) {
					fprintf(stderr, "-b requires an argument\n");
					return 2;
				}
				request->options.opt_alternate_superblock =
				    atoi(argv[++argi]);
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
	struct nextufs_fsck_request request;
	int rc;

	rc = parse_fsck_args(argc, argv, &request);
	if (rc != 0)
		return rc;
	return nextufs_fsck_run(&request);
}
