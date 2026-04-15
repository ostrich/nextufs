/*
 * Equivalence note:
 * This file groups the legacy free-space bitmap manipulation helpers moved out
 * of utilities.c. Allocation scan order and block-accounting updates are
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

void
freeblk(daddr_t blkno, int frags)
{
	struct inodesc idesc;

	idesc.id_blkno = blkno;
	idesc.id_numfrags = frags;
	pass4check(&idesc);
}
