#include <sys/param.h>
#include <ufs/fs.h>
#include <ufs/inode.h>
#define KERNEL
#include <ufs/fsdir.h>
#undef KERNEL
#include "fsck.h"

static unsigned short
swap16(x)
	unsigned short x;
{
	return (x >> 8) | (x << 8);
}

static unsigned long
swap32(x)
	unsigned long x;
{
	return ((x & 0x000000ffUL) << 24) |
	    ((x & 0x0000ff00UL) << 8) |
	    ((x & 0x00ff0000UL) >> 8) |
	    ((x & 0xff000000UL) >> 24);
}

static void
swap_csum(cs)
	struct csum *cs;
{
	cs->cs_ndir = swap32(cs->cs_ndir);
	cs->cs_nbfree = swap32(cs->cs_nbfree);
	cs->cs_nifree = swap32(cs->cs_nifree);
	cs->cs_nffree = swap32(cs->cs_nffree);
}

void
swap_superblock(fs)
	struct fs *fs;
{
	int i, j;

	fs->fs_sblkno = swap32(fs->fs_sblkno);
	fs->fs_cblkno = swap32(fs->fs_cblkno);
	fs->fs_iblkno = swap32(fs->fs_iblkno);
	fs->fs_dblkno = swap32(fs->fs_dblkno);
	fs->fs_cgoffset = swap32(fs->fs_cgoffset);
	fs->fs_cgmask = swap32(fs->fs_cgmask);
	fs->fs_time = swap32(fs->fs_time);
	fs->fs_size = swap32(fs->fs_size);
	fs->fs_dsize = swap32(fs->fs_dsize);
	fs->fs_ncg = swap32(fs->fs_ncg);
	fs->fs_bsize = swap32(fs->fs_bsize);
	fs->fs_fsize = swap32(fs->fs_fsize);
	fs->fs_frag = swap32(fs->fs_frag);
	fs->fs_minfree = swap32(fs->fs_minfree);
	fs->fs_rotdelay = swap32(fs->fs_rotdelay);
	fs->fs_rps = swap32(fs->fs_rps);
	fs->fs_bmask = swap32(fs->fs_bmask);
	fs->fs_fmask = swap32(fs->fs_fmask);
	fs->fs_bshift = swap32(fs->fs_bshift);
	fs->fs_fshift = swap32(fs->fs_fshift);
	fs->fs_maxcontig = swap32(fs->fs_maxcontig);
	fs->fs_maxbpg = swap32(fs->fs_maxbpg);
	fs->fs_fragshift = swap32(fs->fs_fragshift);
	fs->fs_fsbtodb = swap32(fs->fs_fsbtodb);
	fs->fs_sbsize = swap32(fs->fs_sbsize);
	fs->fs_csmask = swap32(fs->fs_csmask);
	fs->fs_csshift = swap32(fs->fs_csshift);
	fs->fs_nindir = swap32(fs->fs_nindir);
	fs->fs_inopb = swap32(fs->fs_inopb);
	fs->fs_nspf = swap32(fs->fs_nspf);
	fs->fs_optim = swap32(fs->fs_optim);
	for (i = 0; i < 5; i++)
		fs->fs_sparecon[i] = swap32(fs->fs_sparecon[i]);
	fs->fs_csaddr = swap32(fs->fs_csaddr);
	fs->fs_cssize = swap32(fs->fs_cssize);
	fs->fs_cgsize = swap32(fs->fs_cgsize);
	fs->fs_ntrak = swap32(fs->fs_ntrak);
	fs->fs_nsect = swap32(fs->fs_nsect);
	fs->fs_spc = swap32(fs->fs_spc);
	fs->fs_ncyl = swap32(fs->fs_ncyl);
	fs->fs_cpg = swap32(fs->fs_cpg);
	fs->fs_ipg = swap32(fs->fs_ipg);
	fs->fs_fpg = swap32(fs->fs_fpg);
	swap_csum(&fs->fs_cstotal);
	fs->fs_cgrotor = swap32(fs->fs_cgrotor);
	fs->fs_cpc = swap32(fs->fs_cpc);
	for (i = 0; i < MAXCPG; i++)
		for (j = 0; j < NRPOS; j++)
			fs->fs_postbl[i][j] = swap16(fs->fs_postbl[i][j]);
	fs->fs_magic = swap32(fs->fs_magic);
}

void
swap_cgblock(cg, fs)
	struct cg *cg;
	struct fs *fs;
{
	int i, j;
	(void)fs;

	cg->cg_time = swap32(cg->cg_time);
	cg->cg_cgx = swap32(cg->cg_cgx);
	cg->cg_ncyl = swap16(cg->cg_ncyl);
	cg->cg_niblk = swap16(cg->cg_niblk);
	cg->cg_ndblk = swap32(cg->cg_ndblk);
	swap_csum(&cg->cg_cs);
	cg->cg_rotor = swap32(cg->cg_rotor);
	cg->cg_frotor = swap32(cg->cg_frotor);
	cg->cg_irotor = swap32(cg->cg_irotor);
	for (i = 0; i < MAXFRAG; i++)
		cg->cg_frsum[i] = swap32(cg->cg_frsum[i]);
	for (i = 0; i < MAXCPG; i++) {
		cg->cg_btot[i] = swap32(cg->cg_btot[i]);
		for (j = 0; j < NRPOS; j++)
			cg->cg_b[i][j] = swap16(cg->cg_b[i][j]);
	}
	cg->cg_magic = swap32(cg->cg_magic);
}

static void
swap_dinode(dp)
	struct dinode *dp;
{
	int i;

	dp->di_mode = swap16(dp->di_mode);
	dp->di_nlink = swap16(dp->di_nlink);
	dp->di_uid = swap16(dp->di_uid);
	dp->di_gid = swap16(dp->di_gid);
	dp->di_ic.ic_size.val[0] = swap32(dp->di_ic.ic_size.val[0]);
	dp->di_ic.ic_size.val[1] = swap32(dp->di_ic.ic_size.val[1]);
	dp->di_atime = swap32(dp->di_atime);
	dp->di_mtime = swap32(dp->di_mtime);
	dp->di_ctime = swap32(dp->di_ctime);
	for (i = 0; i < NDADDR; i++)
		dp->di_db[i] = swap32(dp->di_db[i]);
	for (i = 0; i < NIADDR; i++)
		dp->di_ib[i] = swap32(dp->di_ib[i]);
	dp->di_icflags = swap32(dp->di_icflags);
	dp->di_blocks = swap32(dp->di_blocks);
	dp->di_gen = swap32(dp->di_gen);
}

void
swap_inode_block(dp, count)
	struct dinode *dp;
	int count;
{
	int i;

	for (i = 0; i < count; i++)
		swap_dinode(&dp[i]);
}

void
swap_indir_block(ap, count)
	daddr_t *ap;
	int count;
{
	int i;

	for (i = 0; i < count; i++)
		ap[i] = swap32(ap[i]);
}

void
swap_dirblock(buf, size, dir_write_pass)
	char *buf;
	long size;
	int dir_write_pass;
{
	long off;

	off = 0;
	while (off + 8 <= size) {
		struct direct *dp;
		unsigned short reclen;

		dp = (struct direct *)(buf + off);
		if (dir_write_pass != 0)
			reclen = dp->d_reclen;
		dp->d_ino = swap32(dp->d_ino);
		dp->d_reclen = swap16(dp->d_reclen);
		dp->d_namlen = swap16(dp->d_namlen);
		if (dir_write_pass == 0)
			reclen = dp->d_reclen;
		if (reclen == 0 || off + reclen > size)
			break;
		off += reclen;
	}
}
