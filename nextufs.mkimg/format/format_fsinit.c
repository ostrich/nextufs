#include "format.h"

/*
 * Root directory construction keeps inode numbers, entry ordering, and the
 * initial allocation sequence stable so generated images remain reproducible.
 */
static struct inode node;
#define PREDEFDIR 3
static struct direct root_dir[] = {
	{ ROOTINO, sizeof(struct direct), 1, "." },
	{ ROOTINO, sizeof(struct direct), 2, ".." },
	{ LOSTFOUNDINO, sizeof(struct direct), 10, "lost+found" },
};
static struct direct lost_found_dir[] = {
	{ LOSTFOUNDINO, sizeof(struct direct), 1, "." },
	{ ROOTINO, sizeof(struct direct), 2, ".." },
	{ 0, DIRBLKSIZ, 0, { 0 } },
};
static char buf[MAXBSIZE];

void
initcg(int cylno)
{
	daddr_t cbase, d, dlower, dupper, dmax;
	long i, j;
	register struct csum *cs;

	cbase = cgbase(&sblock, cylno);
	dmax = cbase + sblock.fs_fpg;
	if (dmax > sblock.fs_size)
		dmax = sblock.fs_size;
	dlower = cgsblock(&sblock, cylno) - cbase;
	dupper = cgdmin(&sblock, cylno) - cbase;
	cs = fscs + cylno;
	acg.cg_time = utime;
	acg.cg_magic = CG_MAGIC;
	acg.cg_cgx = cylno;
	if (cylno == sblock.fs_ncg - 1)
		acg.cg_ncyl = sblock.fs_ncyl % sblock.fs_cpg;
	else
		acg.cg_ncyl = sblock.fs_cpg;
	acg.cg_niblk = sblock.fs_ipg;
	acg.cg_ndblk = dmax - cbase;
	acg.cg_cs.cs_ndir = 0;
	acg.cg_cs.cs_nffree = 0;
	acg.cg_cs.cs_nbfree = 0;
	acg.cg_cs.cs_nifree = 0;
	acg.cg_rotor = 0;
	acg.cg_frotor = 0;
	acg.cg_irotor = 0;
	for (i = 0; i < sblock.fs_frag; i++)
		acg.cg_frsum[i] = 0;
	for (i = 0; i < sblock.fs_ipg; ) {
		for (j = INOPB(&sblock); j > 0; j--) {
			clrbit(acg.cg_iused, i);
			i++;
		}
		acg.cg_cs.cs_nifree += INOPB(&sblock);
	}
	if (cylno == 0)
		for (i = 0; i < (long)ROOTINO; i++) {
			setbit(acg.cg_iused, i);
			acg.cg_cs.cs_nifree--;
		}
	while (i < MAXIPG) {
		clrbit(acg.cg_iused, i);
		i++;
	}
	write_inode_block(fsbtodb(&sblock, cgimin(&sblock, cylno)),
	    sblock.fs_ipg, zino);
	for (i = 0; i < MAXCPG; i++) {
		acg.cg_btot[i] = 0;
		for (j = 0; j < NRPOS; j++)
			acg.cg_b[i][j] = 0;
	}
	if (cylno == 0) {
		dupper += howmany(sblock.fs_cssize, sblock.fs_fsize);
		for (d = 0; d < dlower; d += sblock.fs_frag)
			clrblock(&sblock, acg.cg_free, d / sblock.fs_frag);
	} else {
		for (d = 0; d < dlower; d += sblock.fs_frag) {
			setblock(&sblock, acg.cg_free, d / sblock.fs_frag);
			acg.cg_cs.cs_nbfree++;
			acg.cg_btot[cbtocylno(&sblock, d)]++;
			acg.cg_b[cbtocylno(&sblock, d)][cbtorpos(&sblock, d)]++;
		}
		sblock.fs_dsize += dlower;
	}
	sblock.fs_dsize += acg.cg_ndblk - dupper;
	for (; d < dupper; d += sblock.fs_frag)
		clrblock(&sblock, acg.cg_free, d / sblock.fs_frag);
	if (d > dupper) {
		acg.cg_frsum[d - dupper]++;
		for (i = d - 1; i >= dupper; i--) {
			setbit(acg.cg_free, i);
			acg.cg_cs.cs_nffree++;
		}
	}
	while ((d + sblock.fs_frag) <= dmax - cbase) {
		setblock(&sblock, acg.cg_free, d / sblock.fs_frag);
		acg.cg_cs.cs_nbfree++;
		acg.cg_btot[cbtocylno(&sblock, d)]++;
		acg.cg_b[cbtocylno(&sblock, d)][cbtorpos(&sblock, d)]++;
		d += sblock.fs_frag;
	}
	if (d < dmax - cbase) {
		acg.cg_frsum[dmax - cbase - d]++;
		for (; d < dmax - cbase; d++) {
			setbit(acg.cg_free, d);
			acg.cg_cs.cs_nffree++;
		}
		for (; d % sblock.fs_frag != 0; d++)
			clrbit(acg.cg_free, d);
	}
	for (d /= sblock.fs_frag; (unsigned long)d < MAXBPG(&sblock); d++)
		clrblock(&sblock, acg.cg_free, d);
	sblock.fs_cstotal.cs_ndir += acg.cg_cs.cs_ndir;
	sblock.fs_cstotal.cs_nffree += acg.cg_cs.cs_nffree;
	sblock.fs_cstotal.cs_nbfree += acg.cg_cs.cs_nbfree;
	sblock.fs_cstotal.cs_nifree += acg.cg_cs.cs_nifree;
	*cs = acg.cg_cs;
	write_cg_block(fsbtodb(&sblock, cgtod(&sblock, cylno)), &acg);
}

void
fsinit(void)
{
	int i;

	node.i_atime = utime;
	node.i_mtime = utime;
	node.i_ctime = utime;

	(void)makedir(lost_found_dir, 2);
	for (i = DIRBLKSIZ; i < sblock.fs_bsize; i += DIRBLKSIZ)
		put_dirent(&buf[i], 0, DIRBLKSIZ, 0, NULL);
	node.i_number = LOSTFOUNDINO;
	node.i_mode = IFDIR | UMASK;
	node.i_nlink = 2;
	node.i_size = sblock.fs_bsize;
	node.i_db[0] = alloc(node.i_size, node.i_mode);
	node.i_blocks = btodb(fragroundup(&sblock, node.i_size));
	write_dir_block(fsbtodb(&sblock, node.i_db[0]), node.i_size, buf);
	iput(&node);

	node.i_number = ROOTINO;
	node.i_mode = IFDIR | UMASK;
	node.i_nlink = PREDEFDIR;
	node.i_size = makedir(root_dir, PREDEFDIR);
	node.i_db[0] = alloc(sblock.fs_fsize, node.i_mode);
	node.i_blocks = btodb(fragroundup(&sblock, node.i_size));
	write_dir_block(fsbtodb(&sblock, node.i_db[0]), sblock.fs_fsize, buf);
	iput(&node);
}

int
makedir(struct direct *protodir, int entries)
{
	char *cp;
	int i, spcleft;
	uint16_t reclen;

	bzero(buf, sizeof(buf));
	spcleft = DIRBLKSIZ;
	for (cp = buf, i = 0; i < entries - 1; i++) {
		reclen = DIRSIZ(&protodir[i]);
		put_dirent(cp, protodir[i].d_ino, reclen, protodir[i].d_namlen,
		    protodir[i].d_name);
		cp += reclen;
		spcleft -= reclen;
	}
	put_dirent(cp, protodir[i].d_ino, spcleft, protodir[i].d_namlen,
	    protodir[i].d_name);
	return (DIRBLKSIZ);
}

daddr_t
alloc(int size, int mode)
{
	int i, frag;
	daddr_t d;

	read_cg_block(fsbtodb(&sblock, cgtod(&sblock, 0)), &acg);
	if (acg.cg_magic != CG_MAGIC) {
		fprintf(stderr, "cg 0: bad magic number\n");
		return (0);
	}
	if (acg.cg_cs.cs_nbfree == 0) {
		fprintf(stderr, "first cylinder group ran out of space\n");
		return (0);
	}
	for (d = 0; d < acg.cg_ndblk; d += sblock.fs_frag)
		if (isblock(&sblock, acg.cg_free, d / sblock.fs_frag))
			goto goth;
	fprintf(stderr, "internal error: can't find block in cyl 0\n");
	return (0);
goth:
	clrblock(&sblock, acg.cg_free, d / sblock.fs_frag);
	acg.cg_cs.cs_nbfree--;
	sblock.fs_cstotal.cs_nbfree--;
	fscs[0].cs_nbfree--;
	if (mode & IFDIR) {
		acg.cg_cs.cs_ndir++;
		sblock.fs_cstotal.cs_ndir++;
		fscs[0].cs_ndir++;
	}
	acg.cg_btot[cbtocylno(&sblock, d)]--;
	acg.cg_b[cbtocylno(&sblock, d)][cbtorpos(&sblock, d)]--;
	if (size != sblock.fs_bsize) {
		frag = howmany(size, sblock.fs_fsize);
		fscs[0].cs_nffree += sblock.fs_frag - frag;
		sblock.fs_cstotal.cs_nffree += sblock.fs_frag - frag;
		acg.cg_cs.cs_nffree += sblock.fs_frag - frag;
		acg.cg_frsum[sblock.fs_frag - frag]++;
		for (i = frag; i < sblock.fs_frag; i++)
			setbit(acg.cg_free, d + i);
	}
	write_cg_block(fsbtodb(&sblock, cgtod(&sblock, 0)), &acg);
	return (d);
}

void
iput(struct inode *ip)
{
	struct dinode buf[MAXINOPB];
	daddr_t d;

	read_cg_block(fsbtodb(&sblock, cgtod(&sblock, 0)), &acg);
	if (acg.cg_magic != CG_MAGIC) {
		fprintf(stderr, "cg 0: bad magic number\n");
		exit(1);
	}
	acg.cg_cs.cs_nifree--;
	setbit(acg.cg_iused, ip->i_number);
	write_cg_block(fsbtodb(&sblock, cgtod(&sblock, 0)), &acg);
	sblock.fs_cstotal.cs_nifree--;
	fscs[0].cs_nifree--;
	if (ip->i_number >=
	    (ino_t)((unsigned long)sblock.fs_ipg * (unsigned long)sblock.fs_ncg)) {
		fprintf(stderr, "fsinit: inode value out of range (%lu).\n",
		    (unsigned long)ip->i_number);
		exit(1);
	}
	d = fsbtodb(&sblock, itod(&sblock, ip->i_number));
	read_inode_block(d, buf);
	buf[itoo(&sblock, ip->i_number)].di_ic = ip->i_ic;
	write_inode_block(d, sblock.fs_inopb, buf);
}
