/*
 * Equivalence note:
 * This file contains the legacy namespace repair helpers moved out of dir.c.
 * The lost+found flow, entry insertion, directory growth, and link-count
 * updates preserve the original sequencing.
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

void
adjust(struct inodesc *idesc, short lcnt)
{
	register DINODE *dp;

	dp = ginode(idesc->id_number);
	if (dp->di_nlink == lcnt) {
		if (linkup(idesc->id_number, (ino_t)0) == 0)
			clri(idesc, "UNREF", 0);
	} else {
		pwarn("LINK COUNT %s", (lfdir == idesc->id_number) ? lfname :
			(DIRCT(dp) ? "DIR" : "FILE"));
		pinode(idesc->id_number);
		printf(" COUNT %d SHOULD BE %d",
			dp->di_nlink, dp->di_nlink-lcnt);
		if (preen) {
			if (lcnt < 0) {
				printf("\n");
				pfatal("LINK COUNT INCREASING");
			}
			printf(" (ADJUSTED)\n");
		}
		if (preen || reply("ADJUST") == 1) {
			dp->di_nlink -= lcnt;
			inodirty();
		}
	}
}

int
mkentry(struct inodesc *idesc)
{
	register DIRECT *dirp = idesc->id_dirp;
	DIRECT newent;
	int newlen, oldlen;

	newent.d_namlen = 11;
	newlen = DIRSIZ(&newent);
	if (dirp->d_ino != 0)
		oldlen = DIRSIZ(dirp);
	else
		oldlen = 0;
	if (dirp->d_reclen - oldlen < newlen)
		return (KEEPON);
	newent.d_reclen = dirp->d_reclen - oldlen;
	dirp->d_reclen = oldlen;
	dirp = (struct direct *)(((char *)dirp) + oldlen);
	dirp->d_ino = idesc->id_parent;
	dirp->d_reclen = newent.d_reclen;
	dirp->d_namlen = strlen(idesc->id_name);
	bcopy(idesc->id_name, dirp->d_name, dirp->d_namlen + 1);
	return (ALTERED|STOP);
}

int
chgino(struct inodesc *idesc)
{
	register DIRECT *dirp = idesc->id_dirp;

	if (bcmp(dirp->d_name, idesc->id_name, dirp->d_namlen + 1))
		return (KEEPON);
	dirp->d_ino = idesc->id_parent;
	return (ALTERED|STOP);
}

int
linkup(ino_t orphan, ino_t pdir)
{
	register DINODE *dp;
	int lostdir, len;
	ino_t oldlfdir;
	struct inodesc idesc;
	char tempname[BUFSIZ];

	bzero((char *)&idesc, sizeof(struct inodesc));
	dp = ginode(orphan);
	lostdir = DIRCT(dp);
	pwarn("UNREF %s ", lostdir ? "DIR" : "FILE");
	pinode(orphan);
	if (preen && dp->di_size == 0)
		return (0);
	if (preen)
		printf(" (RECONNECTED)\n");
	else if (reply("RECONNECT") == 0)
		return (0);
	pathp = pathname;
	*pathp++ = '/';
	*pathp = '\0';
	if (lfdir == 0) {
		dp = ginode(ROOTINO);
		idesc.id_name = lfname;
		idesc.id_type = DATA;
		idesc.id_func = findino;
		idesc.id_number = ROOTINO;
		(void)ckinode(dp, &idesc);
		if (idesc.id_parent >= ROOTINO && idesc.id_parent < imax) {
			lfdir = idesc.id_parent;
		} else {
			pwarn("NO lost+found DIRECTORY");
			if (preen || reply("CREATE")) {
				lfdir = allocdir(ROOTINO, 0);
				if (lfdir != 0) {
					if (makeentry(ROOTINO, lfdir, lfname) != 0) {
						if (preen)
							printf(" (CREATED)\n");
					} else {
						freedir(lfdir, ROOTINO);
						lfdir = 0;
						if (preen)
							printf("\n");
					}
				}
			}
		}
		if (lfdir == 0) {
			pfatal("SORRY. CANNOT CREATE lost+found DIRECTORY");
			printf("\n\n");
			return (0);
		}
	}
	dp = ginode(lfdir);
	if (!DIRCT(dp)) {
		pfatal("lost+found IS NOT A DIRECTORY");
		if (reply("REALLOCATE") == 0)
			return (0);
		oldlfdir = lfdir;
		if ((lfdir = allocdir(ROOTINO, 0)) == 0) {
			pfatal("SORRY. CANNOT CREATE lost+found DIRECTORY\n\n");
			return (0);
		}
		idesc.id_type = DATA;
		idesc.id_func = chgino;
		idesc.id_number = ROOTINO;
		idesc.id_parent = lfdir;
		idesc.id_name = lfname;
		if ((ckinode(ginode(ROOTINO), &idesc) & ALTERED) == 0) {
			pfatal("SORRY. CANNOT CREATE lost+found DIRECTORY\n\n");
			return (0);
		}
		inodirty();
		idesc.id_type = ADDR;
		idesc.id_func = pass4check;
		idesc.id_number = oldlfdir;
		adjust(&idesc, lncntp[oldlfdir] + 1);
		lncntp[oldlfdir] = 0;
		dp = ginode(lfdir);
	}
	if (statemap[lfdir] != DFOUND) {
		pfatal("SORRY. NO lost+found DIRECTORY\n\n");
		return (0);
	}
	len = strlen(lfname);
	bcopy(lfname, pathp, len + 1);
	pathp += len;
	len = lftempname(tempname, orphan);
	if (makeentry(lfdir, orphan, tempname) == 0) {
		pfatal("SORRY. NO SPACE IN lost+found DIRECTORY");
		printf("\n\n");
		return (0);
	}
	lncntp[orphan]--;
	*pathp++ = '/';
	if (idesc.id_name)
		bcopy(idesc.id_name, pathp, len + 1);
	else
		*pathp = '\0';
	pathp += len;
	if (lostdir) {
		dp = ginode(orphan);
		idesc.id_type = DATA;
		idesc.id_func = chgino;
		idesc.id_number = orphan;
		idesc.id_fix = DONTKNOW;
		idesc.id_name = "..";
		idesc.id_parent = lfdir;
		(void)ckinode(dp, &idesc);
		dp = ginode(lfdir);
		dp->di_nlink++;
		inodirty();
		lncntp[lfdir]++;
		pwarn("DIR I=%lu CONNECTED. ", (unsigned long)orphan);
		printf("PARENT WAS I=%lu\n", (unsigned long)pdir);
		if (preen == 0)
			printf("\n");
	}
	return (1);
}

int
makeentry(ino_t parent, ino_t ino, char *name)
{
	DINODE *dp;
	struct inodesc idesc;

	if (parent < ROOTINO || parent >= imax || ino < ROOTINO || ino >= imax)
		return (0);
	bzero(&idesc, sizeof(struct inodesc));
	idesc.id_type = DATA;
	idesc.id_func = mkentry;
	idesc.id_number = parent;
	idesc.id_parent = ino;
	idesc.id_fix = DONTKNOW;
	idesc.id_name = name;
	dp = ginode(parent);
	if (dp->di_size % DIRBLKSIZ) {
		dp->di_size = roundup(dp->di_size, DIRBLKSIZ);
		inodirty();
	}
	if ((ckinode(dp, &idesc) & ALTERED) != 0)
		return (1);
	if (expanddir(dp) == 0)
		return (0);
	return (ckinode(dp, &idesc) & ALTERED);
}

int
expanddir(DINODE *dp)
{
	daddr_t lastbn, newblk;
	char *cp, firstblk[DIRBLKSIZ];

	lastbn = lblkno(&sblock, dp->di_size);
	if (lastbn >= NDADDR - 1)
		return (0);
	if ((newblk = allocblk(sblock.fs_frag)) == 0)
		return (0);
	dp->di_db[lastbn + 1] = dp->di_db[lastbn];
	dp->di_db[lastbn] = newblk;
	dp->di_size += sblock.fs_bsize;
	dp->di_blocks += btodb(sblock.fs_bsize);
	getblk(&fileblk, dp->di_db[lastbn + 1],
	    dblksize(&sblock, dp, lastbn + 1));
	if (fileblk.b_errs != 0)
		goto bad;
	bcopy(dirblk.b_buf, firstblk, DIRBLKSIZ);
	getblk(&fileblk, newblk, sblock.fs_bsize);
	if (fileblk.b_errs != 0)
		goto bad;
	bcopy(firstblk, dirblk.b_buf, DIRBLKSIZ);
	for (cp = &dirblk.b_buf[DIRBLKSIZ];
	     cp < &dirblk.b_buf[sblock.fs_bsize];
	     cp += DIRBLKSIZ)
		bcopy((char *)&emptydir, cp, sizeof emptydir);
	dirty(&fileblk);
	getblk(&fileblk, dp->di_db[lastbn + 1],
	    dblksize(&sblock, dp, lastbn + 1));
	if (fileblk.b_errs != 0)
		goto bad;
	bcopy((char *)&emptydir, dirblk.b_buf, sizeof emptydir);
	pwarn("NO SPACE LEFT IN %s", pathname);
	if (preen)
		printf(" (EXPANDED)\n");
	else if (reply("EXPAND") == 0)
		goto bad;
	dirty(&fileblk);
	inodirty();
	return (1);
bad:
	dp->di_db[lastbn] = dp->di_db[lastbn + 1];
	dp->di_db[lastbn + 1] = 0;
	dp->di_size -= sblock.fs_bsize;
	dp->di_blocks -= btodb(sblock.fs_bsize);
	freeblk(newblk, sblock.fs_frag);
	return (0);
}

ino_t
allocdir(ino_t parent, ino_t request)
{
	ino_t ino;
	char *cp;
	DINODE *dp;

	ino = allocino(request, IFDIR|0755);
	dirhead.dot_ino = ino;
	dirhead.dotdot_ino = parent;
	dp = ginode(ino);
	getblk(&fileblk, dp->di_db[0], sblock.fs_fsize);
	if (fileblk.b_errs != 0) {
		freeino(ino);
		return (0);
	}
	bcopy((char *)&dirhead, dirblk.b_buf, sizeof dirhead);
	for (cp = &dirblk.b_buf[DIRBLKSIZ];
	     cp < &dirblk.b_buf[sblock.fs_fsize];
	     cp += DIRBLKSIZ)
		bcopy((char *)&emptydir, cp, sizeof emptydir);
	dirty(&fileblk);
	dp->di_nlink = 2;
	inodirty();
	if (ino == ROOTINO) {
		lncntp[ino] = dp->di_nlink;
		return(ino);
	}
	if (statemap[parent] != DSTATE && statemap[parent] != DFOUND) {
		freeino(ino);
		return (0);
	}
	statemap[ino] = statemap[parent];
	if (statemap[ino] == DSTATE) {
		lncntp[ino] = dp->di_nlink;
		lncntp[parent]++;
	}
	dp = ginode(parent);
	dp->di_nlink++;
	inodirty();
	return (ino);
}

void
freedir(ino_t ino, ino_t parent)
{
	DINODE *dp;

	if (ino != parent) {
		dp = ginode(parent);
		dp->di_nlink--;
		inodirty();
	}
	freeino(ino);
}

int
lftempname(char *bufp, ino_t ino)
{
	register ino_t in;
	register char *cp;
	int namlen;

	cp = bufp + 2;
	for (in = imax; in > 0; in /= 10)
		cp++;
	*--cp = 0;
	namlen = cp - bufp;
	in = ino;
	while (cp > bufp) {
		*--cp = (in % 10) + '0';
		in /= 10;
	}
	*cp = '#';
	return (namlen);
}
