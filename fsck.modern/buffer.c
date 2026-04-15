/*
 * Equivalence note:
 * This file isolates the legacy buffer-cache behavior. Buffer eviction,
 * superblock-summary flush ordering, and final teardown sequence are preserved
 * verbatim from the old checker.
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

BUFAREA *
getblk(BUFAREA *bp, daddr_t blk, long size)
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
