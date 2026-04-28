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

int	fsck_return_to_single_user;

int
nextufs_fsck_run(int argc, char **argv)
{
	int pid, passno, anygtr, sumstatus;
	int process_exitstat;
	char *name;
	struct fsck_runtime_options opts;

	memset(&opts, 0, sizeof(opts));
	process_exitstat = 0;
	fsck_return_to_single_user = 0;
	sync();
	while (--argc > 0 && **++argv == '-') {
		switch (*++*argv) {
		case 'p':
			opts.opt_preen++;
			break;
#if	NeXT
		case 'P':
			opts.opt_Pflag++;
			opts.opt_preen++;
			break;
#endif
		case 'b':
			if (argv[0][1] != '\0') {
				opts.opt_bflag = atoi(argv[0]+1);
			} else {
				opts.opt_bflag = atoi(*++argv);
				argc--;
			}
			printf("Alternate super block location: %d\n",
			    opts.opt_bflag);
			break;

		case 'd':
			opts.opt_debug++;
			break;

		case 'n':	/* default no answer flag */
		case 'N':
			opts.opt_nflag++;
			opts.opt_yflag = 0;
			break;

		case 'y':	/* default yes answer flag */
		case 'Y':
			opts.opt_yflag++;
			opts.opt_nflag = 0;
			break;

		default:
			fprintf(stderr, "%c option?\n", **argv);
			return 2;
		}
	}
	if (signal(SIGINT, SIG_IGN) != SIG_IGN)
		(void)signal(SIGINT, catch);
	if (opts.opt_preen)
		(void)signal(SIGQUIT, catchquit);
	if (argc) {
		while (argc-- > 0) {
			process_exitstat |= checkfilesys(*argv, &opts);
			argv++;
		}
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
				name = blockcheck(mnt->mnt_fsname);
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
					(void)signal(SIGQUIT, voidquit);
					name = blockcheck(mnt->mnt_fsname);
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
	if (fsck_return_to_single_user)
		return 2;
	return process_exitstat;
}
