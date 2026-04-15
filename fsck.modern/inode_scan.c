/*
 * Equivalence note:
 * This file contains the legacy inode/block traversal logic. Direct-block
 * ordering, indirect recursion, truncation repair conditions, and inode-block
 * caching are preserved.
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

int
ckinode(DINODE *dp, struct inodesc *idesc)
{
	register daddr_t *ap;
	int ret, n, ndb, offset;
	DINODE dino;

	idesc->id_fix = DONTKNOW;
	idesc->id_entryno = 0;
	idesc->id_filesize = dp->di_size;
	if (SPECIAL(dp))
		return (KEEPON);
	dino = *dp;
	ndb = howmany(dino.di_size, sblock.fs_bsize);
	for (ap = &dino.di_db[0]; ap < &dino.di_db[NDADDR]; ap++) {
		if (--ndb == 0 && (offset = blkoff(&sblock, dino.di_size)) != 0)
			idesc->id_numfrags =
				numfrags(&sblock, fragroundup(&sblock, offset));
		else
			idesc->id_numfrags = sblock.fs_frag;
		if (*ap == 0)
			continue;
		idesc->id_blkno = *ap;
		if (idesc->id_type == ADDR)
			ret = (*idesc->id_func)(idesc);
		else
			ret = dirscan(idesc);
		if (ret & STOP)
			return (ret);
	}
	idesc->id_numfrags = sblock.fs_frag;
	for (ap = &dino.di_ib[0], n = 1; n <= NIADDR; ap++, n++) {
		if (*ap) {
			idesc->id_blkno = *ap;
			ret = iblock(idesc, n,
				dino.di_size - sblock.fs_bsize * NDADDR);
			if (ret & STOP)
				return (ret);
		}
	}
	return (KEEPON);
}

int
iblock(struct inodesc *idesc, int ilevel, long isize)
{
	register daddr_t *ap;
	register daddr_t *aplim;
	int i, n, nif, sizepb;
	int (*func)(struct inodesc *);
	BUFAREA ib;
	char buf[BUFSIZ];

	if (idesc->id_type == ADDR) {
		func = idesc->id_func;
		if (((n = (*func)(idesc)) & KEEPON) == 0)
			return (n);
	} else
		func = dirscan;
	if (outrange(idesc->id_blkno, idesc->id_numfrags))
		return (SKIP);
	initbarea(&ib);
	getblk(&ib, idesc->id_blkno, sblock.fs_bsize);
	if (ib.b_errs != 0)
		return (SKIP);
	if (needswap && !ib.b_swapped) {
		swap_indir_block(ib.b_unp->b_indir, NINDIR(&sblock));
		ib.b_swapped = 1;
	}
	ilevel--;
	for (sizepb = sblock.fs_bsize, i = 0; i < ilevel; i++)
		sizepb *= NINDIR(&sblock);
	nif = isize / sizepb + 1;
	if (nif > NINDIR(&sblock))
		nif = NINDIR(&sblock);
	if (idesc->id_func == pass1check && nif < NINDIR(&sblock)) {
		aplim = &ib.b_unp->b_indir[NINDIR(&sblock)];
		for (ap = &ib.b_unp->b_indir[nif]; ap < aplim; ap++) {
			if (*ap == 0)
				continue;
			sprintf(buf, "PARTIALLY TRUNCATED INODE I=%lu",
				(unsigned long)idesc->id_number);
			if (dofix(idesc, buf)) {
				*ap = 0;
				dirty(&ib);
			}
		}
		flush(&dfile, &ib);
	}
	aplim = &ib.b_unp->b_indir[nif];
	for (ap = ib.b_unp->b_indir, i = 1; ap < aplim; ap++, i++)
		if (*ap) {
			idesc->id_blkno = *ap;
			if (ilevel > 0)
				n = iblock(idesc, ilevel, isize - i * sizepb);
			else
				n = (*func)(idesc);
			if (n & STOP)
				return (n);
		}
	return (KEEPON);
}

int
outrange(daddr_t blk, int cnt)
{
	register int c;

	if ((blk + cnt) > fmax)
		return (1);
	c = dtog(&sblock, blk);
	if (blk < cgdmin(&sblock, c)) {
		if ((blk+cnt) > cgsblock(&sblock, c)) {
			if (debug) {
				printf("blk %d < cgdmin %d;",
				    blk, cgdmin(&sblock, c));
				printf(" blk+cnt %d > cgsbase %d\n",
				    blk+cnt, cgsblock(&sblock, c));
			}
			return (1);
		}
	} else {
		if ((blk+cnt) > cgbase(&sblock, c+1)) {
			if (debug)  {
				printf("blk %d >= cgdmin %d;",
				    blk, cgdmin(&sblock, c));
				printf(" blk+cnt %d > sblock.fs_fpg %d\n",
				    blk+cnt, sblock.fs_fpg);
			}
			return (1);
		}
	}
	return (0);
}

DINODE *
ginode(ino_t inumber)
{
	daddr_t iblk;
	static ino_t startinum = 0;

	if (inumber < ROOTINO || inumber > imax)
		errexit("bad inode number %lu to ginode\n",
		    (unsigned long)inumber);
	if (startinum == 0 ||
	    inumber < startinum || inumber >= startinum + INOPB(&sblock)) {
		iblk = itod(&sblock, inumber);
		getblk(&inoblk, iblk, sblock.fs_bsize);
		if (needswap && !inoblk.b_swapped) {
			swap_inode_block(inoblk.b_unp->b_dinode, INOPB(&sblock));
			inoblk.b_swapped = 1;
		}
		startinum = (inumber / INOPB(&sblock)) * INOPB(&sblock);
	}
	return (&inoblk.b_unp->b_dinode[inumber % INOPB(&sblock)]);
}
