/* Multi-filesystem checker driver. */

#include <stdio.h>
#include <sys/param.h>
#include <ufs/fs.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include <ufs/inode.h>
#include <sys/stat.h>
#include <sys/wait.h>
#define KERNEL
#include <ufs/fsdir.h>
#undef KERNEL
#include <mntent.h>
#include <strings.h>
#include "fsck.h"
#include "nextufs_fsck.h"

int
nextufs_fsck_run(const struct nextufs_fsck_request *request)
{
	int pid, passno, anygtr, sumstatus;
	int process_exitstat;
	char *name;
	struct fsck_runtime_options opts;
	int i;

	memset(&opts, 0, sizeof(opts));
	opts.opt_preen = request->options.opt_preen;
#if	NeXT
	opts.opt_Pflag = request->options.opt_force_preen;
#endif
	opts.opt_nflag = request->options.opt_no;
	opts.opt_yflag = request->options.opt_yes;
	opts.opt_bflag = request->options.opt_alternate_superblock;
	opts.opt_debug = request->options.opt_debug;

	process_exitstat = 0;
	fsck_driver_reset_signal_state();
	sync();
	if (signal(SIGINT, SIG_IGN) != SIG_IGN)
		(void)signal(SIGINT, catch);
	if (opts.opt_preen)
		(void)signal(SIGQUIT, catchquit);
	if (request->source_count > 0) {
		for (i = 0; i < request->source_count; i++)
			process_exitstat |= checkfilesys(request->sources[i],
			    &opts);
		return process_exitstat;
	}
	sumstatus = 0;
	passno = 1;
	do {
		FILE *fstab;
		struct mntent *mnt;
		anygtr = 0;
		/*
		 *  This might not work.  
		 */
		if ((fstab = setmntent(MNTTAB, "r")) == NULL) {
			fprintf(stderr, "Can't open checklist file: %s\n",
			    MNTTAB);
			return 8;
		}
		while ((mnt = getmntent(fstab)) != 0) {
			if (strcmp(mnt->mnt_type, MNTTYPE_43)) {
				continue;
			}
			if (!hasmntopt(mnt,MNTOPT_RW) &&
			    !hasmntopt(mnt,MNTOPT_RO) &&
			    !hasmntopt(mnt,MNTOPT_QUOTA)){
				continue;
			}
			mnt = mntdup(mnt);
			if (opts.opt_preen == 0 ||
			    (passno == 1 && mnt->mnt_passno == passno)) {
				char blockcheck_name[MAXPATHLEN];

				name = blockcheck(mnt->mnt_fsname,
				    blockcheck_name, sizeof(blockcheck_name));
				if (name != NULL)
					process_exitstat |= checkfilesys(name, &opts);
				else if (opts.opt_preen)
					exit(8);
			} else if (mnt->mnt_passno > passno) 
				anygtr = 1;
			else if (mnt->mnt_passno == passno) {
				pid = fork();
				if (pid < 0) {
					perror("fork");
					exit(8);
				}
				if (pid == 0) {
					char blockcheck_name[MAXPATHLEN];

					(void)signal(SIGQUIT, voidquit);
					name = blockcheck(mnt->mnt_fsname,
					    blockcheck_name,
					    sizeof(blockcheck_name));
					if (name == NULL)
						exit(8);
					process_exitstat |= checkfilesys(name,
					    &opts);
					exit(process_exitstat);
				}
			}
		}
		endmntent(fstab);
		if (opts.opt_preen) {
			int status;
			while (wait(&status) != -1) {
				if (WIFEXITED(status))
					sumstatus |= WEXITSTATUS(status);
				else
					sumstatus |= 8;
			}
		}
		passno++;
	} while (anygtr);
	if (sumstatus)
		return 8;
	if (fsck_driver_should_return_to_single_user())
		return 2;
	return process_exitstat;
}
