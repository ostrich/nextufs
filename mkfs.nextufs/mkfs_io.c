#include "mkfs.h"

static uint16_t
swap16(uint16_t x)
{
	return (uint16_t)((x >> 8) | (x << 8));
}

static uint32_t
swap32(uint32_t x)
{
	return ((x & 0x000000ffU) << 24) |
	    ((x & 0x0000ff00U) << 8) |
	    ((x & 0x00ff0000U) >> 8) |
	    ((x & 0xff000000U) >> 24);
}

void
rdfs(daddr_t bno, int size, char *bf)
{
	int n;

	if (lseek(fsi, bno * DEV_BSIZE, 0) < 0) {
		fprintf(stderr, "seek error: %ld\n", (long)bno);
		perror("rdfs");
		exit(1);
	}
	n = read(fsi, bf, size);
	if(n != size) {
		fprintf(stderr, "read error: %ld\n", (long)bno);
		perror("rdfs");
		exit(1);
	}
}

void
wtfs(daddr_t bno, int size, char *bf)
{
	int n;

	if (Nflag)
		return;
	if (lseek(fso, bno * DEV_BSIZE, 0) < 0) {
		fprintf(stderr, "seek error: %ld\n", (long)bno);
		perror("wtfs");
		exit(1);
	}
	n = write(fso, bf, size);
	if(n != size) {
		fprintf(stderr, "write error: %ld\n", (long)bno);
		perror("wtfs");
		exit(1);
	}
}

void
swap_csum(struct csum *cs)
{
	cs->cs_ndir = swap32(cs->cs_ndir);
	cs->cs_nbfree = swap32(cs->cs_nbfree);
	cs->cs_nifree = swap32(cs->cs_nifree);
	cs->cs_nffree = swap32(cs->cs_nffree);
}

void
swap_superblock(struct fs *fs)
{
	int i, j;

	fs->fs_link = swap32(fs->fs_link);
	fs->fs_rlink = swap32(fs->fs_rlink);
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
	for (i = 0; i < MAXCSBUFS; i++)
		fs->fs_csp_pad[i] = swap32(fs->fs_csp_pad[i]);
	fs->fs_cpc = swap32(fs->fs_cpc);
	for (i = 0; i < MAXCPG; i++)
		for (j = 0; j < NRPOS; j++)
			fs->fs_postbl[i][j] = swap16(fs->fs_postbl[i][j]);
	fs->fs_magic = swap32(fs->fs_magic);
}

void
swap_cg(struct cg *cg)
{
	int i, j;

	cg->cg_link = swap32(cg->cg_link);
	cg->cg_rlink = swap32(cg->cg_rlink);
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

void
swap_inode_block_bytes(struct dinode *dp, int count)
{
	int i, j;

	for (i = 0; i < count; i++) {
		dp[i].di_mode = swap16(dp[i].di_mode);
		dp[i].di_nlink = swap16(dp[i].di_nlink);
		dp[i].di_uid = swap16(dp[i].di_uid);
		dp[i].di_gid = swap16(dp[i].di_gid);
		dp[i].di_ic.ic_size.val[0] = swap32(dp[i].di_ic.ic_size.val[0]);
		dp[i].di_ic.ic_size.val[1] = swap32(dp[i].di_ic.ic_size.val[1]);
		dp[i].di_atime = swap32(dp[i].di_atime);
		dp[i].di_mtime = swap32(dp[i].di_mtime);
		dp[i].di_ctime = swap32(dp[i].di_ctime);
		for (j = 0; j < NDADDR; j++)
			dp[i].di_db[j] = swap32(dp[i].di_db[j]);
		for (j = 0; j < NIADDR; j++)
			dp[i].di_ib[j] = swap32(dp[i].di_ib[j]);
		dp[i].di_icflags = swap32(dp[i].di_icflags);
		dp[i].di_blocks = swap32(dp[i].di_blocks);
		dp[i].di_gen = swap32(dp[i].di_gen);
	}
}

void
put_dirent(char *dst, uint32_t ino, uint16_t reclen, uint16_t namlen,
    const char *name)
{
	bzero(dst, reclen);
	dst[0] = (char)(ino >> 24);
	dst[1] = (char)(ino >> 16);
	dst[2] = (char)(ino >> 8);
	dst[3] = (char)ino;
	dst[4] = (char)(reclen >> 8);
	dst[5] = (char)reclen;
	dst[6] = (char)(namlen >> 8);
	dst[7] = (char)namlen;
	if (name != NULL && namlen != 0)
		memcpy(dst + 8, name, namlen);
}

void
write_superblock(daddr_t bno, const struct fs *fs)
{
	char out[SBSIZE];

	memcpy(out, (const char *)fs, sizeof(out));
	swap_superblock((struct fs *)out);
	wtfs(bno, SBSIZE, out);
}

void
write_csum_block(daddr_t bno, int size, const struct csum *cs)
{
	char buf[MAXBSIZE];
	int i;
	int count;

	bzero(buf, sizeof(buf));
	memcpy(buf, cs, size);
	count = size / sizeof(struct csum);
	for (i = 0; i < count; i++)
		swap_csum(&((struct csum *)buf)[i]);
	wtfs(bno, size, buf);
}

void
write_cg_block(daddr_t bno, const struct cg *cg)
{
	char out[MAXBSIZE];

	memcpy(out, (const char *)cg, sblock.fs_bsize);
	swap_cg((struct cg *)out);
	wtfs(bno, sblock.fs_bsize, out);
}

void
write_inode_block(daddr_t bno, int count, const struct dinode *dp)
{
	char *buf;
	size_t size;

	size = (size_t)count * sizeof(struct dinode);
	buf = malloc(size);
	if (buf == NULL) {
		fprintf(stderr, "write_inode_block: malloc failed for %lu bytes\n",
		    (unsigned long)size);
		exit(1);
	}
	bzero(buf, size);
	memcpy(buf, dp, size);
	swap_inode_block_bytes((struct dinode *)buf, count);
	wtfs(bno, (int)size, buf);
	free(buf);
}

void
write_dir_block(daddr_t bno, int size, char *buf)
{
	wtfs(bno, size, buf);
}

void
read_cg_block(daddr_t bno, struct cg *cg)
{
	rdfs(bno, sblock.fs_cgsize, (char *)cg);
	swap_cg(cg);
}

void
read_inode_block(daddr_t bno, struct dinode *dp)
{
	rdfs(bno, sblock.fs_bsize, (char *)dp);
	swap_inode_block_bytes(dp, sblock.fs_inopb);
}

int
isblock(struct fs *fs, unsigned char *cp, int h)
{
	unsigned char mask;

	switch (fs->fs_frag) {
	case 8:
		return (cp[h] == 0xff);
	case 4:
		mask = 0x0f << ((h & 0x1) << 2);
		return ((cp[h >> 1] & mask) == mask);
	case 2:
		mask = 0x03 << ((h & 0x3) << 1);
		return ((cp[h >> 2] & mask) == mask);
	case 1:
		mask = 0x01 << (h & 0x7);
		return ((cp[h >> 3] & mask) == mask);
	default:
#ifdef STANDALONE
		printf("isblock bad fs_frag %d\n", fs->fs_frag);
#else
		fprintf(stderr, "isblock bad fs_frag %d\n", fs->fs_frag);
#endif
		return (0);
	}
}

void
clrblock(struct fs *fs, unsigned char *cp, int h)
{
	switch (fs->fs_frag) {
	case 8:
		cp[h] = 0;
		return;
	case 4:
		cp[h >> 1] &= ~(0x0f << ((h & 0x1) << 2));
		return;
	case 2:
		cp[h >> 2] &= ~(0x03 << ((h & 0x3) << 1));
		return;
	case 1:
		cp[h >> 3] &= ~(0x01 << (h & 0x7));
		return;
	default:
#ifdef STANDALONE
		printf("clrblock bad fs_frag %d\n", fs->fs_frag);
#else
		fprintf(stderr, "clrblock bad fs_frag %d\n", fs->fs_frag);
#endif
		return;
	}
}

void
setblock(struct fs *fs, unsigned char *cp, int h)
{
	switch (fs->fs_frag) {
	case 8:
		cp[h] = 0xff;
		return;
	case 4:
		cp[h >> 1] |= (0x0f << ((h & 0x1) << 2));
		return;
	case 2:
		cp[h >> 2] |= (0x03 << ((h & 0x3) << 1));
		return;
	case 1:
		cp[h >> 3] |= (0x01 << (h & 0x7));
		return;
	default:
#ifdef STANDALONE
		printf("setblock bad fs_frag %d\n", fs->fs_frag);
#else
		fprintf(stderr, "setblock bad fs_frag %d\n", fs->fs_frag);
#endif
		return;
	}
}
