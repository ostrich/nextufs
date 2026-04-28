#ifndef NEXTUFS_FSCK_H
#define NEXTUFS_FSCK_H

struct nextufs_fsck_options {
	int opt_preen;
	int opt_force_preen;
	int opt_no;
	int opt_yes;
	int opt_alternate_superblock;
	int opt_debug;
};

struct nextufs_fsck_request {
	struct nextufs_fsck_options options;
	int source_count;
	char **sources;
};

int nextufs_fsck_run(const struct nextufs_fsck_request *request);

#endif
