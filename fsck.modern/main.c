/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

#ifndef lint
static const char copyright[] __attribute__((unused)) =
"@(#) Copyright (c) 1980 Regents of the University of California.\n\
 All rights reserved.\n";
#endif

#ifndef lint
static const char sccsid[] __attribute__((unused)) =
	"@(#)main.c	1.6 88/05/23 4.0NFSSRC SMI"; /* from UCB 5.4 3/5/86 */
						/* @(#) from SUN 1.32   */
#endif

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

int	returntosingle;

int
main(int argc, char *argv[])
{
	int pid, passno, anygtr, sumstatus;
	char *name;
	sync();
	while (--argc > 0 && **++argv == '-') {
		switch (*++*argv) {
		case 'p':
			fsck_runtime_opts.opt_preen++;
			break;
#if	NeXT
		case 'P':
		    	fsck_runtime_opts.opt_Pflag++;
			fsck_runtime_opts.opt_preen++;
			break;
#endif
		case 'b':
			if (argv[0][1] != '\0') {
				fsck_runtime_opts.opt_bflag = atoi(argv[0]+1);
			} else {
				fsck_runtime_opts.opt_bflag = atoi(*++argv);
				argc--;
			}
			printf("Alternate super block location: %d\n",
			    fsck_runtime_opts.opt_bflag);
			break;

		case 'd':
			fsck_runtime_opts.opt_debug++;
			break;

		case 'n':	/* default no answer flag */
		case 'N':
			fsck_runtime_opts.opt_nflag++;
			fsck_runtime_opts.opt_yflag = 0;
			break;

		case 'y':	/* default yes answer flag */
		case 'Y':
			fsck_runtime_opts.opt_yflag++;
			fsck_runtime_opts.opt_nflag = 0;
			break;

		default:
			errexit("%c option?\n", **argv);
		}
	}
	if (signal(SIGINT, SIG_IGN) != SIG_IGN)
		(void)signal(SIGINT, catch);
	if (fsck_runtime_opts.opt_preen)
		(void)signal(SIGQUIT, catchquit);
	if (argc) {
		while (argc-- > 0) {
			checkfilesys(*argv);
			argv++;
		}
		exit(fsck_process_exitstat);
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
		if ((fstab = setmntent(MNTTAB, "r")) == NULL)
			errexit("Can't open checklist file: %s\n", MNTTAB);
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
			if (fsck_runtime_opts.opt_preen == 0 ||
			    (passno == 1 && mnt->mnt_passno == passno)) {
				name = blockcheck(mnt->mnt_fsname);
				if (name != NULL)
					checkfilesys(name);
				else if (fsck_runtime_opts.opt_preen)
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
					checkfilesys(name);
					exit(fsck_process_exitstat);
				}
			}
		}
		endmntent(fstab);
		if (fsck_runtime_opts.opt_preen) {
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
		exit(8);
	if (returntosingle)
		exit(2);
	exit(fsck_process_exitstat);
}
