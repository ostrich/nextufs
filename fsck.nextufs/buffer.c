/* Buffer-cache and buffered writeback helpers. */

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

static void
swap_bufarea(BUFAREA *bp, int dir_write_pass)
{
	switch (bp->b_type) {
	case BT_INDIR:
		swap_indir_block(bp->b_unp->b_indir, NINDIR(&sblock));
		break;

	case BT_SUPER:
		swap_superblock(&bp->b_unp->b_fs);
		break;

	case BT_CG:
		swap_cgblock(&bp->b_unp->b_cg, &sblock);
		break;

	case BT_INODE:
		swap_inode_block(bp->b_unp->b_dinode, INOPB(&sblock));
		break;

	case BT_DIR:
		swap_dirblock(bp->b_unp->b_buf, bp->b_size, dir_write_pass);
		break;

	case BT_UNKNOWN:
	default:
		break;
	}
}

static void
swap_csum_block(struct csum *cs, long size)
{
	int k, ncs;

	ncs = size / sizeof(struct csum);
	for (k = 0; k < ncs; k++) {
		struct csum *cur;

		cur = &cs[k];
		cur->cs_ndir = ((cur->cs_ndir & 0xff) << 24) |
		    ((cur->cs_ndir & 0xff00) << 8) |
		    ((cur->cs_ndir & 0xff0000) >> 8) |
		    ((cur->cs_ndir >> 24) & 0xff);
		cur->cs_nbfree = ((cur->cs_nbfree & 0xff) << 24) |
		    ((cur->cs_nbfree & 0xff00) << 8) |
		    ((cur->cs_nbfree & 0xff0000) >> 8) |
		    ((cur->cs_nbfree >> 24) & 0xff);
		cur->cs_nifree = ((cur->cs_nifree & 0xff) << 24) |
		    ((cur->cs_nifree & 0xff00) << 8) |
		    ((cur->cs_nifree & 0xff0000) >> 8) |
		    ((cur->cs_nifree >> 24) & 0xff);
		cur->cs_nffree = ((cur->cs_nffree & 0xff) << 24) |
		    ((cur->cs_nffree & 0xff00) << 8) |
		    ((cur->cs_nffree & 0xff0000) >> 8) |
		    ((cur->cs_nffree >> 24) & 0xff);
	}
}

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
	if (needswap && bp->b_errs == 0) {
		swap_bufarea(bp, 0);
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
	if (needswap && bp->b_swapped)
		swap_bufarea(bp, 1);
	bwrite(fcp, bp->b_unp->b_buf, bp->b_bno, (long)bp->b_size);
	if (needswap && bp->b_swapped)
		swap_bufarea(bp, 0);
	if (bp != &sblk)
		return;
	for (i = 0, j = 0; i < sblock.fs_cssize; i += sblock.fs_bsize, j++) {
		long size;

		size = sblock.fs_cssize - i < sblock.fs_bsize ?
		    sblock.fs_cssize - i : sblock.fs_bsize;
		if (needswap)
			swap_csum_block(fsck_fs_csp[j], size);
		bwrite(&dfile, (char *)fsck_fs_csp[j],
		    fsbtodb(&sblock, sblock.fs_csaddr + j * sblock.fs_frag),
		    size);
		if (needswap)
			swap_csum_block(fsck_fs_csp[j], size);
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
	fsck_file_close(&dfile);
}
