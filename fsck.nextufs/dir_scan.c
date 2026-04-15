/*
 * Equivalence note:
 * This file contains the legacy directory traversal and validation logic moved
 * out of dir.c. The control flow, state transitions, and on-disk edits are
 * preserved.
 */

#include <stdio.h>
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

#define MINDIRSIZE	(sizeof (struct dirtemplate))

void
descend(struct inodesc *parentino, ino_t inumber)
{
	register DINODE *dp;
	struct inodesc curino;

	bzero((char *)&curino, sizeof(struct inodesc));
	if (statemap[inumber] != DSTATE)
		errexit("BAD INODE %d TO DESCEND", statemap[inumber]);
	statemap[inumber] = DFOUND;
	dp = ginode(inumber);
	if (dp->di_size == 0) {
#if	NeXT
		pwarn("ZERO LENGTH DIRECTORY ");
		pinode(inumber);
		printf("\n");
		if (inumber < ROOTINO || inumber > imax) {
			pfatal("NAME=%s\n", pathname);
			return;
		}
		pwarn("DIR=%s", pathname);
		if (preen)
			printf(" (REMOVED)\n");
		if (preen || reply("REMOVE") == 1)
			statemap[inumber] = DCLEAR;
#else
		direrr(inumber, "ZERO LENGTH DIRECTORY");
		if (reply("REMOVE") == 1)
			statemap[inumber] = DCLEAR;
#endif
		return;
	}
	if (dp->di_size < (long)MINDIRSIZE) {
		direrr(inumber, "DIRECTORY TOO SHORT");
		dp->di_size = MINDIRSIZE;
		if (reply("FIX") == 1)
			inodirty();
	}
	if ((dp->di_size & (DIRBLKSIZ - 1)) != 0) {
		pwarn("DIRECTORY %s: LENGTH %d NOT MULTIPLE OF %d",
			pathname, (long)dp->di_size, DIRBLKSIZ);
		dp->di_size = roundup(dp->di_size, DIRBLKSIZ);
		if (preen)
			printf(" (ADJUSTED)\n");
		if (preen || reply("ADJUST") == 1)
			inodirty();
	}
	curino.id_type = DATA;
	curino.id_func = parentino->id_func;
	curino.id_parent = parentino->id_number;
	curino.id_number = inumber;
	(void)ckinode(dp, &curino);
}

int
dirscan(struct inodesc *idesc)
{
	register DIRECT *dp;
	int dsize, n;
	long blksiz;
	char dbuf[DIRBLKSIZ];

	if (idesc->id_type != DATA)
		errexit("wrong type to dirscan %d\n", idesc->id_type);
	if (idesc->id_entryno == 0 &&
	    (idesc->id_filesize & (DIRBLKSIZ - 1)) != 0)
		idesc->id_filesize = roundup(idesc->id_filesize, DIRBLKSIZ);
	blksiz = idesc->id_numfrags * sblock.fs_fsize;
	if (outrange(idesc->id_blkno, idesc->id_numfrags)) {
		idesc->id_filesize -= blksiz;
		return (SKIP);
	}
	idesc->id_loc = 0;
	for (dp = fsck_readdir(idesc); dp != NULL; dp = fsck_readdir(idesc)) {
		dsize = dp->d_reclen;
		bcopy((char *)dp, dbuf, dsize);
		idesc->id_dirp = (DIRECT *)dbuf;
		if ((n = (*idesc->id_func)(idesc)) & ALTERED) {
			getblk(&fileblk, idesc->id_blkno, blksiz);
			if (fileblk.b_errs != 0) {
				n &= ~ALTERED;
			} else {
				bcopy(dbuf, (char *)dp, dsize);
				dirty(&fileblk);
				sbdirty();
			}
		}
		if (n & STOP)
			return (n);
	}
	return (idesc->id_filesize > 0 ? KEEPON : STOP);
}

DIRECT *
fsck_readdir(struct inodesc *idesc)
{
	register DIRECT *dp, *ndp;
	long size, blksiz;

	blksiz = idesc->id_numfrags * sblock.fs_fsize;
	getblk(&fileblk, idesc->id_blkno, blksiz);
	if (fileblk.b_errs != 0) {
		idesc->id_filesize -= blksiz - idesc->id_loc;
		return NULL;
	}
	if (idesc->id_loc % DIRBLKSIZ == 0 && idesc->id_filesize > 0 &&
	    idesc->id_loc < blksiz) {
		dp = (DIRECT *)(dirblk.b_buf + idesc->id_loc);
		if (dircheck(idesc, dp))
			goto dpok;
		idesc->id_loc += DIRBLKSIZ;
		idesc->id_filesize -= DIRBLKSIZ;
		dp->d_reclen = DIRBLKSIZ;
		dp->d_ino = 0;
		dp->d_namlen = 0;
		dp->d_name[0] = '\0';
		if (dofix(idesc, "DIRECTORY CORRUPTED"))
			dirty(&fileblk);
		return (dp);
	}
dpok:
	if (idesc->id_filesize <= 0 || idesc->id_loc >= blksiz)
		return NULL;
	dp = (DIRECT *)(dirblk.b_buf + idesc->id_loc);
	idesc->id_loc += dp->d_reclen;
	idesc->id_filesize -= dp->d_reclen;
	if ((idesc->id_loc % DIRBLKSIZ) == 0)
		return (dp);
	ndp = (DIRECT *)(dirblk.b_buf + idesc->id_loc);
	if (idesc->id_loc < blksiz && idesc->id_filesize > 0 &&
	    dircheck(idesc, ndp) == 0) {
		size = DIRBLKSIZ - (idesc->id_loc % DIRBLKSIZ);
		dp->d_reclen += size;
		idesc->id_loc += size;
		idesc->id_filesize -= size;
		if (dofix(idesc, "DIRECTORY CORRUPTED"))
			dirty(&fileblk);
	}
	return (dp);
}

int
dircheck(struct inodesc *idesc, DIRECT *dp)
{
	register int size;
	register char *cp;
	int spaceleft;

	size = DIRSIZ(dp);
	spaceleft = DIRBLKSIZ - (idesc->id_loc % DIRBLKSIZ);
	if (dp->d_ino < imax &&
	    dp->d_reclen != 0 &&
	    dp->d_reclen <= spaceleft &&
	    (dp->d_reclen & 0x3) == 0 &&
	    dp->d_reclen >= size &&
	    idesc->id_filesize >= size &&
	    dp->d_namlen <= MAXNAMLEN) {
		if (dp->d_ino == 0)
			return (1);
		for (cp = dp->d_name, size = 0; size < dp->d_namlen; size++)
			if (*cp == 0 || (*cp++ & 0200))
				return (0);
		if (*cp == 0)
			return (1);
	}
	return (0);
}

void
direrr(ino_t ino, char *s)
{
	register DINODE *dp;

	pwarn("%s ", s);
	pinode(ino);
	printf("\n");
	if (ino < ROOTINO || ino > imax) {
		pfatal("NAME=%s\n", pathname);
		return;
	}
	dp = ginode(ino);
	if (ftypeok(dp))
		pfatal("%s=%s\n", DIRCT(dp) ? "DIR" : "FILE", pathname);
	else
		pfatal("NAME=%s\n", pathname);
}

int
findname(struct inodesc *idesc)
{
	register DIRECT *dirp = idesc->id_dirp;

	if (dirp->d_ino != idesc->id_parent)
		return (KEEPON);
	bcopy(dirp->d_name, idesc->id_name, dirp->d_namlen + 1);
	return (STOP);
}

int
findino(struct inodesc *idesc)
{
	register DIRECT *dirp = idesc->id_dirp;

	if (dirp->d_ino == 0)
		return (KEEPON);
	if (strcmp(dirp->d_name, idesc->id_name) == 0 &&
	    dirp->d_ino >= ROOTINO && dirp->d_ino <= imax) {
		idesc->id_parent = dirp->d_ino;
		return (STOP);
	}
	return (KEEPON);
}

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
