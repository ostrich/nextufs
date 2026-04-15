/*
 * Equivalence note:
 * This file contains the legacy inode reporting and mutation helpers moved out
 * of inode.c. Link-count adjustments, inode clearing, and block release timing
 * preserve the original behavior.
 */

#include <stdio.h>
#include <pwd.h>
#include <sys/param.h>
#include <ufs/fs.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include <ufs/inode.h>
#define KERNEL
#include <ufs/fsdir.h>
#undef KERNEL
#include <mntent.h>
#include "fsck.h"

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

void
clri(struct inodesc *idesc, char *s, int flg)
{
	register DINODE *dp;

	dp = ginode(idesc->id_number);
	if (flg == 1) {
		pwarn("%s %s", s, DIRCT(dp) ? "DIR" : "FILE");
		pinode(idesc->id_number);
	}
	if (preen || reply("CLEAR") == 1) {
		if (preen)
			printf(" (CLEARED)\n");
		n_files--;
		(void)ckinode(dp, idesc);
		zapino(dp);
		statemap[idesc->id_number] = USTATE;
		inodirty();
	}
}

void
pinode(ino_t ino)
{
	register DINODE *dp;
	struct passwd *pw;
	time_t mtime;
	char *p;

	printf(" I=%lu ", (unsigned long)ino);
#ifdef MORRIS
	if (ino < ROOTINO || ino > imax)  {
		printf("ino %lu out of bounds (ROOTINO,imax=%lu)\n",
		    (unsigned long)ino, (unsigned long)imax);
		return;
	}
#else
	if (ino < ROOTINO || ino > imax)
		return;
#endif
	dp = ginode(ino);
	printf(" OWNER=");
	if ((pw = getpwuid((int)dp->di_uid)) != 0)
		printf("%s ", pw->pw_name);
	else
		printf("%d ", dp->di_uid);
	printf("MODE=%o\n", dp->di_mode);
	if (preen)
		printf("%s: ", devname);
	printf("SIZE=%ld ", (long)dp->di_size);
	mtime = (time_t)dp->di_mtime;
	p = ctime(&mtime);
	printf("MTIME=%12.12s %4.4s ", p+4, p+20);
#ifdef MORRIS
	printf ("\nNLINK %d FLAGS %o\n", dp->di_nlink, dp->di_icflags);
#endif
}

void
blkerr(ino_t ino, char *s, daddr_t blk)
{
	pfatal("%ld %s I=%lu", (long)blk, s, (unsigned long)ino);
	printf("\n");
	switch (statemap[ino]) {
	case FSTATE:
		statemap[ino] = FCLEAR;
		return;
	case DSTATE:
		statemap[ino] = DCLEAR;
		return;
	case FCLEAR:
	case DCLEAR:
		return;
	default:
		errexit("BAD STATE %d TO BLKERR", statemap[ino]);
	}
}

ino_t
allocino(ino_t request, int type)
{
	register ino_t ino;
	register DINODE *dp;

	if (request == 0)
		request = ROOTINO;
	else if (statemap[request] != USTATE)
		return (0);
	for (ino = request; ino < imax; ino++)
		if (statemap[ino] == USTATE)
			break;
	if (ino == imax)
		return (0);
	switch (type & IFMT) {
	case IFDIR:
		statemap[ino] = DSTATE;
		break;
	case IFREG:
	case IFLNK:
		statemap[ino] = FSTATE;
		break;
	default:
		return (0);
	}
	dp = ginode(ino);
	dp->di_db[0] = allocblk(1);
	if (dp->di_db[0] == 0) {
		statemap[ino] = USTATE;
		return (0);
	}
	dp->di_mode = type;
	dp->di_atime = (int32_t)time((time_t *)0);
	dp->di_mtime = dp->di_ctime = dp->di_atime;
	dp->di_size = sblock.fs_fsize;
	dp->di_blocks = btodb(sblock.fs_fsize);
	n_files++;
	inodirty();
	return (ino);
}

void
freeino(ino_t ino)
{
	struct inodesc idesc;
	DINODE *dp;

	bzero((char *)&idesc, sizeof(struct inodesc));
	idesc.id_type = ADDR;
	idesc.id_func = pass4check;
	idesc.id_number = ino;
	dp = ginode(ino);
	(void)ckinode(dp, &idesc);
	zapino(dp);
	inodirty();
	statemap[ino] = USTATE;
	n_files--;
}
