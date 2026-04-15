/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

#ifndef lint
static const char sccsid[] __attribute__((unused)) =
	"@(#)utilities.c	1.4 88/05/23 4.0NFSSRC SMI"; /* from UCB 5.2 9/10/85 */
						/* @(#) from SUN 1.8     */
#endif

#include <stdio.h>
#include <ctype.h>
#include <stdarg.h>
#include <sys/param.h>
#include <ufs/fs.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include <ufs/inode.h>
#if	NeXT
#include <sys/file.h>
#include <sys/errno.h>
#endif
#define KERNEL
#include <ufs/fsdir.h>
#undef KERNEL
#include <mntent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <strings.h>
#include "fsck.h"

#define getline fsck_getline

char *
ftypeok(DINODE *dp)
{
	switch (dp->di_mode & IFMT) {

	case IFDIR:
	case IFREG:
	case IFBLK:
	case IFCHR:
	case IFLNK:
	case IFSOCK:
	case IFIFO:
		return ((char *)1);

	default:
		if (debug)
			printf("bad file type 0%o\n", dp->di_mode);
		return ((char *)0);
	}
}

int
reply(char *s)
{
	char line[80];

	if (preen)
		pfatal("INTERNAL ERROR: GOT TO reply()");
	printf("\n%s? ", s);
	if (nflag || dfile.wfdes < 0) {
		printf(" no\n\n");
		exitstat = 8;		/* remember there's still an error */
		return (0);
	}
	if (yflag) {
		printf(" yes\n\n");
		return (1);
	}
	if (getline(stdin, line, sizeof(line)) == EOF)
		errexit("\n");
	printf("\n");
	if (line[0] == 'y' || line[0] == 'Y')
		return (1);
	else {
		exitstat = 8;		/* remember there's still an error */
		return (0);
	}
}

int
getline(FILE *fp, char *loc, int maxlen)
{
	register int n;
	register char *p, *lastloc;

	p = loc;
	lastloc = &p[maxlen-1];
	while ((n = getc(fp)) != '\n') {
		if (n == EOF)
			return (EOF);
		if (!isspace(n) && p < lastloc)
			*p++ = n;
	}
	*p = 0;
	return (p - loc);
}

BUFAREA *
getblk(bp, blk, size)
	register BUFAREA *bp;
	daddr_t blk;
	long size;
{
	register struct filecntl *fcp;
	daddr_t dblk;

	fcp = &dfile;
	dblk = fsbtodb(&sblock, blk);
	if (bp->b_bno == dblk)
		return (bp);
	flush(fcp, bp);
	bp->b_errs = bread(fcp, bp->b_unp->b_buf, dblk, size);
	bp->b_bno = dblk;
	bp->b_size = size;
	bp->b_swapped = 0;
	if (needswap && bp == &cgblk && bp->b_errs == 0) {
		swap_cgblock(&cgrp, &sblock);
		bp->b_swapped = 1;
	}
	return (bp);
}

void
flush(struct filecntl *fcp, BUFAREA *bp)
{
	register int i, j;

	if (!bp->b_dirty)
		return;
	if (bp->b_errs != 0)
		pfatal("WRITING ZERO'ED BLOCK %d TO DISK\n", bp->b_bno);
	bp->b_dirty = 0;
	bp->b_errs = 0;
	bwrite(fcp, bp->b_unp->b_buf, bp->b_bno, (long)bp->b_size);
	if (bp != &sblk)
		return;
	for (i = 0, j = 0; i < sblock.fs_cssize; i += sblock.fs_bsize, j++) {
		bwrite(&dfile, (char *)fsck_fs_csp[j],
		    fsbtodb(&sblock, sblock.fs_csaddr + j * sblock.fs_frag),
		    sblock.fs_cssize - i < sblock.fs_bsize ?
		    sblock.fs_cssize - i : sblock.fs_bsize);
	}
}

void
rwerr(char *s, daddr_t blk)
{

	if (preen == 0)
		printf("\n");
	pfatal("CANNOT %s: BLK %ld", s, blk);
	if (reply("CONTINUE") == 0)
		errexit("Program terminated\n");
}

void
ckfini(void)
{

	flush(&dfile, &fileblk);
	flush(&dfile, &sblk);
	if (sblk.b_bno != SBLOCK) {
		sblk.b_bno = SBLOCK;
		sbdirty();
		flush(&dfile, &sblk);
	}
	flush(&dfile, &inoblk);
	flush(&dfile, &cgblk);
	(void)close(dfile.rfdes);
	(void)close(dfile.wfdes);
}

int
bread(struct filecntl *fcp, char *buf, daddr_t blk, long size)
{
	char *cp;
	int i, errs;

	if (lseek(fcp->rfdes, (long)dbtob(blk), 0) < 0)
		rwerr("SEEK", blk);
	else if (read(fcp->rfdes, buf, (int)size) == size)
		return (0);
	rwerr("READ", blk);
	if (lseek(fcp->rfdes, (long)dbtob(blk), 0) < 0)
		rwerr("SEEK", blk);
	errs = 0;
	pfatal("THE FOLLOWING SECTORS COULD NOT BE READ:");
	for (cp = buf, i = 0; i < size; i += DEV_BSIZE, cp += DEV_BSIZE) {
		if (read(fcp->rfdes, cp, DEV_BSIZE) < 0) {
			printf(" %ld,", (long)(blk + i / DEV_BSIZE));
			bzero(cp, DEV_BSIZE);
			errs++;
		}
	}
	printf("\n");
	return (errs);
}

void
bwrite(struct filecntl *fcp, char *buf, daddr_t blk, int size)
{
	int i;
	char *cp;

	if (fcp->wfdes < 0)
		return;
	if (lseek(fcp->wfdes, (long)dbtob(blk), 0) < 0)
		rwerr("SEEK", blk);
	else if (write(fcp->wfdes, buf, (int)size) == size) {
		fcp->mod = 1;
		return;
	}
	rwerr("WRITE", blk);
	if (lseek(fcp->wfdes, (long)dbtob(blk), 0) < 0)
		rwerr("SEEK", blk);
	pfatal("THE FOLLOWING SECTORS COULD NOT BE WRITTEN:");
	for (cp = buf, i = 0; i < size; i += DEV_BSIZE, cp += DEV_BSIZE)
		if (write(fcp->wfdes, cp, DEV_BSIZE) < 0)
			printf(" %ld,", (long)(blk + i / DEV_BSIZE));
	printf("\n");
	return;
}

/*
 * allocate a data block with the specified number of fragments
 */
daddr_t
allocblk(int frags)
{
	register int i, j, k;

	if (frags <= 0 || frags > sblock.fs_frag)
		return (0);
	for (i = 0; i < fmax - sblock.fs_frag; i += sblock.fs_frag) {
		for (j = 0; j <= sblock.fs_frag - frags; j++) {
			if (getbmap(i + j))
				continue;
			for (k = 1; k < frags; k++)
				if (getbmap(i + j + k))
					break;
			if (k < frags) {
				j += k;
				continue;
			}
			for (k = 0; k < frags; k++)
				setbmap(i + j + k);
			n_blks += frags;
			return (i + j);
		}
	}
	return (0);
}

/*
 * Free a previously allocated block
 */
void
freeblk(daddr_t blkno, int frags)
{
	struct inodesc idesc;

	idesc.id_blkno = blkno;
	idesc.id_numfrags = frags;
	pass4check(&idesc);
}

/*
 * Find a pathname
 */
void
getpathname(char *namebuf, ino_t curdir, ino_t ino)
{
	int len;
	register char *cp;
	struct inodesc idesc;

	if (statemap[ino] != DSTATE && statemap[ino] != DFOUND) {
		strcpy(namebuf, "?");
		return;
	}
	bzero(&idesc, sizeof(struct inodesc));
	idesc.id_type = DATA;
	cp = &namebuf[BUFSIZ - 1];
	*cp-- = '\0';
	if (curdir != ino) {
		idesc.id_parent = curdir;
		goto namelookup;
	}
	while (ino != ROOTINO) {
		idesc.id_number = ino;
		idesc.id_func = findino;
		idesc.id_name = "..";
		if ((ckinode(ginode(ino), &idesc) & STOP) == 0)
			break;
	namelookup:
		idesc.id_number = idesc.id_parent;
		idesc.id_parent = ino;
		idesc.id_func = findname;
		idesc.id_name = namebuf;
		if ((ckinode(ginode(idesc.id_number), &idesc) & STOP) == 0)
			break;
		len = strlen(namebuf);
		cp -= len;
		if (cp < &namebuf[MAXNAMLEN])
			break;
		bcopy(namebuf, cp, len);
		*--cp = '/';
		ino = idesc.id_number;
	}
	if (ino != ROOTINO) {
		strcpy(namebuf, "?");
		return;
	}
	bcopy(cp, namebuf, &namebuf[BUFSIZ] - cp);
}

void
catch(int signo)
{
	(void)signo;

	ckfini();
	exit(12);
}

/*
 * When preening, allow a single quit to signal
 * a special exit after filesystem checks complete
 * so that reboot sequence may be interrupted.
 */
void
catchquit(int signo)
{
	extern int returntosingle;
	(void)signo;

	printf("returning to single-user after filesystem check\n");
	returntosingle = 1;
	(void)signal(SIGQUIT, SIG_DFL);
}

/*
 * Ignore a single quit signal; wait and flush just in case.
 * Used by child processes in preen.
 */
void
voidquit(int signo)
{
	(void)signo;

	sleep(1);
	(void)signal(SIGQUIT, SIG_IGN);
	(void)signal(SIGQUIT, SIG_DFL);
}

/*
 * determine whether an inode should be fixed.
 */
int
dofix(struct inodesc *idesc, char *msg)
{

	switch (idesc->id_fix) {

	case DONTKNOW:
		if (idesc->id_type == DATA)
			direrr(idesc->id_number, msg);
		else
			pwarn(msg);
		if (preen) {
			printf(" (SALVAGED)\n");
			idesc->id_fix = FIX;
			return (ALTERED);
		}
		if (reply("SALVAGE") == 0) {
			idesc->id_fix = NOFIX;
			return (0);
		}
		idesc->id_fix = FIX;
		return (ALTERED);

	case FIX:
		return (ALTERED);

	case NOFIX:
		return (0);

	default:
		errexit("UNKNOWN INODESC FIX MODE %d\n", idesc->id_fix);
	}
	return (0);
}

static void
vreport(prefix, fmt, ap)
	char *prefix;
	char *fmt;
	va_list ap;
{
	if (prefix != 0)
		printf("%s", prefix);
	vprintf(fmt, ap);
}

void
errexit(char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vreport(0, fmt, ap);
	va_end(ap);
	exit(8);
}

/*
 * An inconsistency occured which shouldn't during normal operations.
 * Die if preening, otherwise just printf.
 */
void
pfatal(char *fmt, ...)
{
	va_list ap;

#if	NeXT
	error_count++;
#endif

	if (preen) {
		printf("%s: ", devname);
		va_start(ap, fmt);
		vprintf(fmt, ap);
		va_end(ap);
		printf("\n");
		printf("%s: UNEXPECTED INCONSISTENCY; RUN fsck MANUALLY.\n",
			devname);
		exit(8);
	}
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

/*
 * Pwarn is like printf when not preening,
 * or a warning (preceded by filename) when preening.
 */
void
pwarn(char *fmt, ...)
{
	va_list ap;

#if	NeXT
	error_count++;
#endif

	if (preen)
		printf("%s: ", devname);
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

#if	NeXT
/*
 * Pinfo is like printf when not preening,
 * or a info (preceded by filename) when preening.
 */
void
pinfo(char *fmt, ...)
{
	va_list ap;

	if (preen)
		printf("%s: ", devname);
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}
#endif

#ifndef lint
/*
 * Stub for routines from kernel.
 */
void
panic(const char *s)
{

	pfatal("INTERNAL INCONSISTENCY:");
	errexit((char *)s);
}
#endif

/*
 * Check to see if unraw version of name is already mounted.  
 * Since we do not believe /etc/mtab, we stat the mount point 
 * to see if it is really mounted.
 */
int
mounted(char *name)
{
	int found = 0;
	struct mntent *mnt;
	FILE *mnttab;
	char *blkname, *unrawname();
	static char buf[MAXPATHLEN];

	(void) strcpy(buf, name);
	blkname = unrawname(buf);

#if	NeXT
	mountedfs = rootfs = readonlyfs = 0;
#endif

	if (is_mounted_on("/", blkname)) {
#if	NeXT
		mountedfs = rootfs = 1;
		readonlyfs = (access("/", W_OK) < 0 && errno == EROFS);
#endif
		return (1);
	}

	mnttab = setmntent(MOUNTED, "r");
	if (mnttab == NULL) {
		printf("can't open %s\n", MOUNTED);
		return (0);
	}
	while (! found && (mnt = getmntent(mnttab)) != NULL) {
		if (strcmp(mnt->mnt_type, MNTTYPE_43) != 0)
			continue;
		if (strcmp(blkname, mnt->mnt_fsname) != 0)
			continue;
		found = is_mounted_on(mnt->mnt_dir, mnt->mnt_fsname);
	}
	endmntent(mnttab);
#if	NeXT
	mountedfs = found;
	if (found)
		readonlyfs = (access(mnt->mnt_dir, W_OK) < 0 && errno == EROFS);
#endif
	return (found);
}

int
is_mounted_on(char *dir, char *dev)
{
	struct stat device_stat, mount_stat;

	if (stat(dir, &mount_stat) < 0)
		return (0);
	if (stat(dev, &device_stat) < 0)
		return (0);
	return (device_stat.st_rdev == mount_stat.st_dev);
}

void *
xmalloc(unsigned long size)
{
	void *ret;
	
	if ((ret = (char *)malloc(size)) == NULL) {
		errexit("ran out of memory!\n");
	}
	return (ret);
}

struct mntent *
mntdup(struct mntent *mnt)
{
	struct mntent *new;

	new = (struct mntent *)xmalloc(sizeof(*new));

	new->mnt_fsname = (char *)xmalloc(strlen(mnt->mnt_fsname) + 1);
	strcpy(new->mnt_fsname, mnt->mnt_fsname);

	new->mnt_dir = (char *)xmalloc(strlen(mnt->mnt_dir) + 1);
	strcpy(new->mnt_dir, mnt->mnt_dir);

	new->mnt_type = (char *)xmalloc(strlen(mnt->mnt_type) + 1);
	strcpy(new->mnt_type, mnt->mnt_type);

	new->mnt_opts = (char *)xmalloc(strlen(mnt->mnt_opts) + 1);
	strcpy(new->mnt_opts, mnt->mnt_opts);

	new->mnt_freq = mnt->mnt_freq;
	new->mnt_passno = mnt->mnt_passno;

	return (new);
}
