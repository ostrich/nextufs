/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

#ifndef lint
char copyright[] =
"@(#) Copyright (c) 1980 Regents of the University of California.\n\
 All rights reserved.\n";
#endif

#ifndef lint
static const char sccsid[] __attribute__((unused)) =
	"@(#)mkfs.c    1.2 88/03/10 4.0NFSSRC; from    5.3 (Berkeley) 9/11/85";
#endif

/*
 * make file system for cylinder-group style file systems
 *
 * usage:
 *    mkfs -N special size [ nsect ntrak bsize fsize cpg minfree rps nbpi opt ]
 */

/*
 * The following constants set the defaults used for the number
 * of sectors (fs_nsect), and number of tracks (fs_ntrak).
 */
#define DFLNSECT	32
#define DFLNTRAK	16

/*
 * The following two constants set the default block and fragment sizes.
 * Both constants must be a power of 2 and meet the following constraints:
 *	MINBSIZE <= DESBLKSIZE <= MAXBSIZE
 *	DEV_BSIZE <= DESFRAGSIZE <= DESBLKSIZE
 *	DESBLKSIZE / DESFRAGSIZE <= 8
 */
#define DESBLKSIZE	8192
#define DESFRAGSIZE	1024

/*
 * Cylinder groups may have up to MAXCPG cylinders. The actual
 * number used depends upon how much information can be stored
 * on a single cylinder. The default is to used 16 cylinders
 * per group.
 */
#define	DESCPG		16	/* desired fs_cpg */

/*
 * MINFREE gives the minimum acceptable percentage of file system
 * blocks which may be free. If the freelist drops below this level
 * only the superuser may continue to allocate blocks. This may
 * be set to 0 if no reserve of free blocks is deemed necessary,
 * however throughput drops by fifty percent if the file system
 * is run at between 90% and 100% full; thus the default value of
 * fs_minfree is 10%. With 10% free space, fragmentation is not a
 * problem, so we choose to optimize for time.
 */
#define MINFREE		10
#define DEFAULTOPT	FS_OPTTIME

/*
 * ROTDELAY gives the minimum number of milliseconds to initiate
 * another disk transfer on the same cylinder. It is used in
 * determining the rotationally optimal layout for disk blocks
 * within a file; the default of fs_rotdelay is 4ms.
 */
#define ROTDELAY	4

/*
 * MAXCONTIG sets the default for the maximum number of blocks
 * that may be allocated sequentially. Since UNIX drivers are
 * not capable of scheduling multi-block transfers, this defaults
 * to 1 (ie no contiguous blocks are allocated).
 */
#define MAXCONTIG	1

/*
 * MAXBLKPG determines the maximum number of data blocks which are
 * placed in a single cylinder group. This is currently a function
 * of the block and fragment size of the file system.
 */
#define MAXBLKPG(fs)	((fs)->fs_fsize / sizeof(daddr_t))

/*
 * Each file system has a number of inodes statically allocated.
 * We allocate one inode slot per NBPI bytes, expecting this
 * to be far more than we will ever need.
 */
#define	NBPI		2048

/*
 * Disks are assumed to rotate at 60HZ, unless otherwise specified.
 */
#define	DEFHZ		60

#ifndef STANDALONE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#ifdef notdef
#include <a.out.h>
#endif
#endif

#include <sys/param.h>
#ifdef	NeXT_NFS
#include <sys/time.h>
#include <sys/vnode.h>
#include <ufs/fsdir.h>
#include <ufs/inode.h>
#include <ufs/fs.h>
#else
#include <sys/inode.h>
#include <sys/fs.h>
#include <sys/dir.h>
#endif

#define UMASK		0755
#define MAXINOPB	(MAXBSIZE / sizeof(struct dinode))
#define POWEROF2(num)	(((num) & ((num) - 1)) == 0)

union {
	struct fs fs;
	char pad[MAXBSIZE];
} fsun;
#define	sblock	fsun.fs
struct	csum *fscs;

union {
	struct cg cg;
	char pad[MAXBSIZE];
} cgun;
#define	acg	cgun.cg

struct	dinode zino[MAXIPG];

char	*fsys;
time_t	utime;
int	fsi;
int	fso;
int	Nflag;
daddr_t	alloc(int size, int mode);
static void initcg(int cylno);
static void fsinit(void);
static int makedir(struct direct *protodir, int entries);
static void iput(struct inode *ip);
static void rdfs(daddr_t bno, int size, char *bf);
static void wtfs(daddr_t bno, int size, char *bf);
static int isblock(struct fs *fs, unsigned char *cp, int h);
static void clrblock(struct fs *fs, unsigned char *cp, int h);
static void setblock(struct fs *fs, unsigned char *cp, int h);
static void write_superblock(daddr_t, const struct fs *);
static void write_csum_block(daddr_t, int, const struct csum *);
static void write_cg_block(daddr_t, const struct cg *);
static void write_inode_block(daddr_t, int, const struct dinode *);
static void write_dir_block(daddr_t, int, char *);
static void read_cg_block(daddr_t, struct cg *);
static void read_inode_block(daddr_t, struct dinode *);
static uint16_t swap16(uint16_t);
static uint32_t swap32(uint32_t);
static void swap_csum(struct csum *);
static void swap_superblock(struct fs *);
static void swap_cg(struct cg *);
static void swap_inode_block_bytes(struct dinode *, int);
static void put_dirent(char *, uint32_t, uint16_t, uint16_t, const char *);

int
main(int argc, char *argv[])
{
	long cylno, rpos, blk, i, j, inos, nbpi, fssize, warn = 0;
	char lastbuf[DEV_BSIZE];

#ifndef STANDALONE
	argc--, argv++;
	if (argv[0][0] == '-') {
		switch (argv[0][1]) {
		case 'N':
			Nflag++;
			break;
		default:
			printf("%s: unknown flag\n", &argv[0][1]);
			argc = 1;	/* force usage message */
			break;
		}
		argc--, argv++;
	}
	time(&utime);
	if (argc < 2) {
		printf("usage: mkfs -N special size [ nsect ntrak bsize fsize cpg minfree rps nbpi ]\n");
		exit(1);
	}
	fsys = argv[0];
	fssize = atoi(argv[1]);
	if (!Nflag) {
		fso = creat(fsys, 0666);
		if(fso < 0) {
			printf("%s: cannot create\n", fsys);
			exit(1);
		}
	}
	fsi = open(fsys, 0);
	if(fsi < 0) {
		printf("%s: cannot open\n", fsys);
		exit(1);
	}
#else
	{
		static char protos[60];
		char fsbuf[100];

		printf("file sys size: ");
		gets(protos);
		fssize = atoi(protos);
		do {
			printf("file system: ");
			gets(fsbuf);
			fso = open(fsbuf, 1);
			fsi = open(fsbuf, 0);
		} while (fso < 0 || fsi < 0);
	}
	argc = 0;
#endif
	/*
	 * Validate the given file system size.
	 * Verify that its last block can actually be accessed.
	 */
	if (fssize <= 0)
		printf("preposterous size %ld\n", fssize), exit(1);
	bzero(lastbuf, sizeof(lastbuf));
	wtfs(fssize - 1, DEV_BSIZE, lastbuf);
	/*
	 * collect and verify the sector and track info
	 */
	if (argc > 2)
		sblock.fs_nsect = atoi(argv[2]);
	else
		sblock.fs_nsect = DFLNSECT;
	if (argc > 3)
		sblock.fs_ntrak = atoi(argv[3]);
	else
		sblock.fs_ntrak = DFLNTRAK;
	if (sblock.fs_ntrak <= 0)
		printf("preposterous ntrak %d\n", sblock.fs_ntrak), exit(1);
	if (sblock.fs_nsect <= 0)
		printf("preposterous nsect %d\n", sblock.fs_nsect), exit(1);
	sblock.fs_spc = sblock.fs_ntrak * sblock.fs_nsect;
	/*
	 * collect and verify the block and fragment sizes
	 */
	if (argc > 4)
		sblock.fs_bsize = atoi(argv[4]);
	else
		sblock.fs_bsize = DESBLKSIZE;
	if (argc > 5)
		sblock.fs_fsize = atoi(argv[5]);
	else
		sblock.fs_fsize = DESFRAGSIZE;
	if (!POWEROF2(sblock.fs_bsize)) {
		printf("block size must be a power of 2, not %d\n",
		    sblock.fs_bsize);
		exit(1);
	}
	if (!POWEROF2(sblock.fs_fsize)) {
		printf("fragment size must be a power of 2, not %d\n",
		    sblock.fs_fsize);
		exit(1);
	}
	if (sblock.fs_fsize < DEV_BSIZE) {
		printf("fragment size %d is too small, minimum is %d\n",
		    sblock.fs_fsize, DEV_BSIZE);
		exit(1);
	}
	if (sblock.fs_bsize < MINBSIZE) {
		printf("block size %d is too small, minimum is %d\n",
		    sblock.fs_bsize, MINBSIZE);
		exit(1);
	}
	if (sblock.fs_bsize < sblock.fs_fsize) {
		printf("block size (%d) cannot be smaller than fragment size (%d)\n",
		    sblock.fs_bsize, sblock.fs_fsize);
		exit(1);
	}
	sblock.fs_bmask = ~(sblock.fs_bsize - 1);
	sblock.fs_fmask = ~(sblock.fs_fsize - 1);
	for (sblock.fs_bshift = 0, i = sblock.fs_bsize; i > 1; i >>= 1)
		sblock.fs_bshift++;
	for (sblock.fs_fshift = 0, i = sblock.fs_fsize; i > 1; i >>= 1)
		sblock.fs_fshift++;
	sblock.fs_frag = numfrags(&sblock, sblock.fs_bsize);
	for (sblock.fs_fragshift = 0, i = sblock.fs_frag; i > 1; i >>= 1)
		sblock.fs_fragshift++;
	if (sblock.fs_frag > MAXFRAG) {
		printf("fragment size %d is too small, minimum with block size %d is %d\n",
		    sblock.fs_fsize, sblock.fs_bsize,
		    sblock.fs_bsize / MAXFRAG);
		exit(1);
	}
	sblock.fs_nindir = sblock.fs_bsize / sizeof(daddr_t);
	sblock.fs_inopb = sblock.fs_bsize / sizeof(struct dinode);
	sblock.fs_nspf = sblock.fs_fsize / DEV_BSIZE;
	for (sblock.fs_fsbtodb = 0, i = sblock.fs_nspf; i > 1; i >>= 1)
		sblock.fs_fsbtodb++;
	sblock.fs_sblkno =
	    roundup(howmany(BBSIZE + SBSIZE, sblock.fs_fsize), sblock.fs_frag);
	sblock.fs_cblkno = (daddr_t)(sblock.fs_sblkno +
	    roundup(howmany(SBSIZE, sblock.fs_fsize), sblock.fs_frag));
	sblock.fs_iblkno = sblock.fs_cblkno + sblock.fs_frag;
	sblock.fs_cgoffset = roundup(
	    howmany(sblock.fs_nsect, sblock.fs_fsize / DEV_BSIZE),
	    sblock.fs_frag);
	for (sblock.fs_cgmask = 0xffffffff, i = sblock.fs_ntrak; i > 1; i >>= 1)
		sblock.fs_cgmask <<= 1;
	if (!POWEROF2(sblock.fs_ntrak))
		sblock.fs_cgmask <<= 1;
	for (sblock.fs_cpc = NSPB(&sblock), i = sblock.fs_spc;
	     sblock.fs_cpc > 1 && (i & 1) == 0;
	     sblock.fs_cpc >>= 1, i >>= 1)
		/* void */;
	if (sblock.fs_cpc > MAXCPG) {
		printf("maximum block size with nsect %d and ntrak %d is %d\n",
		    sblock.fs_nsect, sblock.fs_ntrak,
		    sblock.fs_bsize / (sblock.fs_cpc / MAXCPG));
		exit(1);
	}
	/* 
	 * collect and verify the number of cylinders per group
	 */
	if (argc > 6) {
		sblock.fs_cpg = atoi(argv[6]);
		sblock.fs_fpg = (sblock.fs_cpg * sblock.fs_spc) / NSPF(&sblock);
	} else {
		sblock.fs_cpg = MAX(sblock.fs_cpc, DESCPG);
		sblock.fs_fpg = (sblock.fs_cpg * sblock.fs_spc) / NSPF(&sblock);
		while ((unsigned long)(sblock.fs_fpg / sblock.fs_frag) >
		    MAXBPG(&sblock) &&
		    sblock.fs_cpg > sblock.fs_cpc) {
			sblock.fs_cpg -= sblock.fs_cpc;
			sblock.fs_fpg =
			    (sblock.fs_cpg * sblock.fs_spc) / NSPF(&sblock);
		}
	}
	if (sblock.fs_cpg < 1) {
		printf("cylinder groups must have at least 1 cylinder\n");
		exit(1);
	}
	if (sblock.fs_cpg > MAXCPG) {
		printf("cylinder groups are limited to %d cylinders\n", MAXCPG);
		exit(1);
	}
	if (sblock.fs_cpg % sblock.fs_cpc != 0) {
		printf("cylinder groups must have a multiple of %d cylinders\n",
		    sblock.fs_cpc);
		exit(1);
	}
	/*
	 * Now have size for file system and nsect and ntrak.
	 * Determine number of cylinders and blocks in the file system.
	 */
	sblock.fs_size = fssize = dbtofsb(&sblock, fssize);
	sblock.fs_ncyl = fssize * NSPF(&sblock) / sblock.fs_spc;
	if (fssize * NSPF(&sblock) > sblock.fs_ncyl * sblock.fs_spc) {
		sblock.fs_ncyl++;
		warn = 1;
	}
	if (sblock.fs_ncyl < 1) {
		printf("file systems must have at least one cylinder\n");
		exit(1);
	}
	/*
	 * determine feasability/values of rotational layout tables
	 */
	if (sblock.fs_ntrak == 1) {
		sblock.fs_cpc = 0;
		goto next;
	}
	if ((unsigned long)(sblock.fs_spc * sblock.fs_cpc) >
	    MAXBPC * NSPB(&sblock) ||
	    sblock.fs_nsect > (1 << NBBY) * NSPB(&sblock)) {
		printf("%s %s %d %s %d.%s",
		    "Warning: insufficient space in super block for\n",
		    "rotational layout tables with nsect", sblock.fs_nsect,
		    "and ntrak", sblock.fs_ntrak,
		    "\nFile system performance may be impaired.\n");
		sblock.fs_cpc = 0;
		goto next;
	}
	/*
	 * calculate the available blocks for each rotational position
	 */
	for (cylno = 0; cylno < MAXCPG; cylno++)
		for (rpos = 0; rpos < NRPOS; rpos++)
			sblock.fs_postbl[cylno][rpos] = -1;
	blk = sblock.fs_spc * sblock.fs_cpc / NSPF(&sblock);
	for (i = 0; i < blk; i += sblock.fs_frag)
		/* void */;
	for (i -= sblock.fs_frag; i >= 0; i -= sblock.fs_frag) {
		cylno = cbtocylno(&sblock, i);
		rpos = cbtorpos(&sblock, i);
		blk = i / sblock.fs_frag;
		if (sblock.fs_postbl[cylno][rpos] == -1)
			sblock.fs_rotbl[blk] = 0;
		else
			sblock.fs_rotbl[blk] =
			    sblock.fs_postbl[cylno][rpos] - blk;
		sblock.fs_postbl[cylno][rpos] = blk;
	}
next:
	/*
	 * Validate specified/determined cpg.
	 */
	if ((unsigned long)sblock.fs_spc > MAXBPG(&sblock) * NSPB(&sblock)) {
		printf("too many sectors per cylinder (%d sectors)\n",
		    sblock.fs_spc);
		while ((unsigned long)sblock.fs_spc >
		    MAXBPG(&sblock) * NSPB(&sblock)) {
			sblock.fs_bsize <<= 1;
			if (sblock.fs_frag < MAXFRAG)
				sblock.fs_frag <<= 1;
			else
				sblock.fs_fsize <<= 1;
		}
		printf("nsect %d, and ntrak %d, requires block size of %d,\n",
		    sblock.fs_nsect, sblock.fs_ntrak, sblock.fs_bsize);
		printf("\tand fragment size of %d\n", sblock.fs_fsize);
		exit(1);
	}
	if ((unsigned long)sblock.fs_fpg >
	    MAXBPG(&sblock) * (unsigned long)sblock.fs_frag) {
		printf("cylinder group too large (%d cylinders); ",
		    sblock.fs_cpg);
		printf("max: %lu cylinders per group\n",
		    (unsigned long)(MAXBPG(&sblock) * sblock.fs_frag /
		    (sblock.fs_fpg / sblock.fs_cpg)));
		exit(1);
	}
	sblock.fs_cgsize = fragroundup(&sblock,
	    sizeof(struct cg) + howmany(sblock.fs_fpg, NBBY));
	/*
	 * Compute/validate number of cylinder groups.
	 */
	sblock.fs_ncg = sblock.fs_ncyl / sblock.fs_cpg;
	if (sblock.fs_ncyl % sblock.fs_cpg)
		sblock.fs_ncg++;
	if ((sblock.fs_spc * sblock.fs_cpg) % NSPF(&sblock)) {
		printf("mkfs: nsect %d, ntrak %d, cpg %d is not tolerable\n",
		    sblock.fs_nsect, sblock.fs_ntrak, sblock.fs_cpg);
		printf("as this would would have cyl groups whose size\n");
		printf("is not a multiple of %d; choke!\n", sblock.fs_fsize);
		exit(1);
	}
	/*
	 * Compute number of inode blocks per cylinder group.
	 * Start with one inode per NBPI bytes; adjust as necessary.
	 */
	inos = MAX(NBPI, sblock.fs_fsize);
	if (argc > 9) {
		i = atoi(argv[9]);
		if (i <= 0)
			printf("%s: bogus nbpi reset to %ld\n", argv[9], inos);
		else
			inos = i;
	}
	i = sblock.fs_iblkno + MAXIPG / INOPF(&sblock);
	nbpi = inos;
	inos = (fssize - sblock.fs_ncg * i) * sblock.fs_fsize / inos /
	    INOPB(&sblock);
	if (inos <= 0)
		inos = 1;
	sblock.fs_ipg = ((inos / sblock.fs_ncg) + 1) * INOPB(&sblock);
	if (sblock.fs_ipg > MAXIPG) {
		sblock.fs_ipg = MAXIPG;
		printf("Warning: %ld bytes per inode impossible due\n", nbpi);
		printf("to cylinder group size, using %ld bytes per inode\n",
		    (fssize - sblock.fs_ncg * MAXIPG) * sblock.fs_fsize
		      / (sblock.fs_ipg * sblock.fs_ncg));
		printf("Reduce cylinder group size to reduce bytes per inode.\n");
	}
	sblock.fs_dblkno = sblock.fs_iblkno + sblock.fs_ipg / INOPF(&sblock);
	i = MIN(~sblock.fs_cgmask, sblock.fs_ncg - 1);
	if (cgdmin(&sblock, i) - cgbase(&sblock, i) >= sblock.fs_fpg) {
		printf("inode blocks/cyl group (%ld) >= data blocks (%d)\n",
		    cgdmin(&sblock, i) - cgbase(&sblock, i) / sblock.fs_frag,
		    sblock.fs_fpg / sblock.fs_frag);
		printf("number of cylinders per cylinder group must be increased\n");
		exit(1);
	}
	j = sblock.fs_ncg - 1;
	if ((i = fssize - j * sblock.fs_fpg) < sblock.fs_fpg &&
	    cgdmin(&sblock, j) - cgbase(&sblock, j) > i) {
		printf("Warning: inode blocks/cyl group (%ld) >= data blocks (%ld) in last\n",
		    (cgdmin(&sblock, j) - cgbase(&sblock, j)) / sblock.fs_frag,
		    i / sblock.fs_frag);
		printf("    cylinder group. This implies %ld sector(s) cannot be allocated.\n",
		    i * NSPF(&sblock));
		sblock.fs_ncg--;
		sblock.fs_ncyl -= sblock.fs_ncyl % sblock.fs_cpg;
		sblock.fs_size = fssize = sblock.fs_ncyl * sblock.fs_spc /
		    NSPF(&sblock);
		warn = 0;
	}
	if (warn) {
		printf("Warning: %ld sector(s) in last cylinder unallocated\n",
		    sblock.fs_spc -
		    (fssize * NSPF(&sblock) - (sblock.fs_ncyl - 1)
		    * sblock.fs_spc));
	}
	/*
	 * fill in remaining fields of the super block
	 */
	sblock.fs_csaddr = cgdmin(&sblock, 0);
	sblock.fs_cssize =
	    fragroundup(&sblock, sblock.fs_ncg * sizeof(struct csum));
	i = sblock.fs_bsize / sizeof(struct csum);
	sblock.fs_csmask = ~(i - 1);
	for (sblock.fs_csshift = 0; i > 1; i >>= 1)
		sblock.fs_csshift++;
	i = sizeof(struct fs) +
		howmany(sblock.fs_spc * sblock.fs_cpc, NSPB(&sblock));
	sblock.fs_sbsize = fragroundup(&sblock, i);
	fscs = (struct csum *)calloc(1, sblock.fs_cssize);
	sblock.fs_magic = FS_MAGIC;
	sblock.fs_rotdelay = ROTDELAY;
	if (argc > 7) {
		sblock.fs_minfree = atoi(argv[7]);
		if (sblock.fs_minfree < 0 || sblock.fs_minfree > 99) {
			printf("%s: bogus minfree reset to %d%%\n", argv[7],
				MINFREE);
			sblock.fs_minfree = MINFREE;
		}
	} else
		sblock.fs_minfree = MINFREE;
	sblock.fs_maxcontig = MAXCONTIG;
	sblock.fs_maxbpg = MAXBLKPG(&sblock);
	if (argc > 8)
		sblock.fs_rps = atoi(argv[8]);
	else
		sblock.fs_rps = DEFHZ;
	if (argc > 10)
		if (*argv[10] == 's')
			sblock.fs_optim = FS_OPTSPACE;
		else if (*argv[10] == 't')
			sblock.fs_optim = FS_OPTTIME;
		else {
			printf("%s: bogus optimization preference %s\n",
				argv[10], "reset to time");
			sblock.fs_optim = FS_OPTTIME;
		}
	else
		sblock.fs_optim = DEFAULTOPT;
	sblock.fs_cgrotor = 0;
	sblock.fs_cstotal.cs_ndir = 0;
	sblock.fs_cstotal.cs_nbfree = 0;
	sblock.fs_cstotal.cs_nifree = 0;
	sblock.fs_cstotal.cs_nffree = 0;
	sblock.fs_fmod = 0;
	sblock.fs_ronly = 0;
#if	NeXT_MOD
	sblock.fs_state = FS_STATE_CLEAN;
#endif
	/*
	 * Dump out summary information about file system.
	 */
	printf("%s:\t%d sectors in %d cylinders of %d tracks, %d sectors\n",
	    fsys, sblock.fs_size * NSPF(&sblock), sblock.fs_ncyl,
	    sblock.fs_ntrak, sblock.fs_nsect);
#ifndef FP_BUG
	printf("\t%.1fMb in %d cyl groups (%d c/g, %.2fMb/g, %d i/g)\n",
	    (float)sblock.fs_size * sblock.fs_fsize * 1e-6, sblock.fs_ncg,
	    sblock.fs_cpg, (float)sblock.fs_fpg * sblock.fs_fsize * 1e-6,
	    sblock.fs_ipg);
#endif
	/*
	 * Now build the cylinders group blocks and
	 * then print out indices of cylinder groups.
	 */
	printf("super-block backups (for fsck -b#) at:");
	for (cylno = 0; cylno < sblock.fs_ncg; cylno++) {
		initcg(cylno);
		if (cylno % 10 == 0)
			printf("\n");
		printf(" %ld,", (long)fsbtodb(&sblock, cgsblock(&sblock, cylno)));
	}
	printf("\n");
	if (Nflag)
		exit(0);
	/*
	 * Now construct the initial file system,
	 * then write out the super-block.
	 */
	fsinit();
	sblock.fs_time = utime;
	write_superblock(SBLOCK, &sblock);
	for (i = 0; i < sblock.fs_cssize; i += sblock.fs_bsize)
		write_csum_block(fsbtodb(&sblock,
		    sblock.fs_csaddr + numfrags(&sblock, i)),
		    sblock.fs_cssize - i < sblock.fs_bsize ?
		    sblock.fs_cssize - i : sblock.fs_bsize,
		    (struct csum *)(((char *)fscs) + i));
	/* 
	 * Write out the duplicate super blocks
	 */
	for (cylno = 0; cylno < sblock.fs_ncg; cylno++)
		write_superblock(fsbtodb(&sblock, cgsblock(&sblock, cylno)),
		    &sblock);
#ifndef STANDALONE
	exit(0);
#endif
}

/*
 * Initialize a cylinder group.
 */
static void
initcg(int cylno)
{
	daddr_t cbase, d, dlower, dupper, dmax;
	long i, j;
	register struct csum *cs;

	/*
	 * Determine block bounds for cylinder group.
	 * Allow space for super block summary information in first
	 * cylinder group.
	 */
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
	for (i = 0; i < sblock.fs_frag; i++) {
		acg.cg_frsum[i] = 0;
	}
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
		/*
		 * reserve space for summary info and Boot block
		 */
		dupper += howmany(sblock.fs_cssize, sblock.fs_fsize);
		for (d = 0; d < dlower; d += sblock.fs_frag)
			clrblock(&sblock, acg.cg_free, d/sblock.fs_frag);
	} else {
		for (d = 0; d < dlower; d += sblock.fs_frag) {
			setblock(&sblock, acg.cg_free, d/sblock.fs_frag);
			acg.cg_cs.cs_nbfree++;
			acg.cg_btot[cbtocylno(&sblock, d)]++;
			acg.cg_b[cbtocylno(&sblock, d)][cbtorpos(&sblock, d)]++;
		}
		sblock.fs_dsize += dlower;
	}
	sblock.fs_dsize += acg.cg_ndblk - dupper;
	for (; d < dupper; d += sblock.fs_frag)
		clrblock(&sblock, acg.cg_free, d/sblock.fs_frag);
	if (d > dupper) {
		acg.cg_frsum[d - dupper]++;
		for (i = d - 1; i >= dupper; i--) {
			setbit(acg.cg_free, i);
			acg.cg_cs.cs_nffree++;
		}
	}
	while ((d + sblock.fs_frag) <= dmax - cbase) {
		setblock(&sblock, acg.cg_free, d/sblock.fs_frag);
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

/*
 * initialize the file system
 */
struct inode node;
#define PREDEFDIR 3
struct direct root_dir[] = {
	{ ROOTINO, sizeof(struct direct), 1, "." },
	{ ROOTINO, sizeof(struct direct), 2, ".." },
	{ LOSTFOUNDINO, sizeof(struct direct), 10, "lost+found" },
};
struct direct lost_found_dir[] = {
	{ LOSTFOUNDINO, sizeof(struct direct), 1, "." },
	{ ROOTINO, sizeof(struct direct), 2, ".." },
	{ 0, DIRBLKSIZ, 0, { 0 } },
};
char buf[MAXBSIZE];

static void
fsinit(void)
{
	int i;

	/*
	 * initialize the node
	 */
	node.i_atime = utime;
	node.i_mtime = utime;
	node.i_ctime = utime;
	/*
	 * create the lost+found directory
	 */
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
	/*
	 * create the root directory
	 */
	node.i_number = ROOTINO;
	node.i_mode = IFDIR | UMASK;
	node.i_nlink = PREDEFDIR;
	node.i_size = makedir(root_dir, PREDEFDIR);
	node.i_db[0] = alloc(sblock.fs_fsize, node.i_mode);
	node.i_blocks = btodb(fragroundup(&sblock, node.i_size));
	write_dir_block(fsbtodb(&sblock, node.i_db[0]), sblock.fs_fsize, buf);
	iput(&node);
}

/*
 * construct a set of directory entries in "buf".
 * return size of directory.
 */
static int
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

/*
 * allocate a block or frag
 */
daddr_t
alloc(size, mode)
	int size;
	int mode;
{
	int i, frag;
	daddr_t d;

	read_cg_block(fsbtodb(&sblock, cgtod(&sblock, 0)), &acg);
	if (acg.cg_magic != CG_MAGIC) {
		printf("cg 0: bad magic number\n");
		return (0);
	}
	if (acg.cg_cs.cs_nbfree == 0) {
		printf("first cylinder group ran out of space\n");
		return (0);
	}
	for (d = 0; d < acg.cg_ndblk; d += sblock.fs_frag)
		if (isblock(&sblock, acg.cg_free, d / sblock.fs_frag))
			goto goth;
	printf("internal error: can't find block in cyl 0\n");
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

/*
 * Allocate an inode on the disk
 */
static void
iput(struct inode *ip)
{
	struct dinode buf[MAXINOPB];
	daddr_t d;
	read_cg_block(fsbtodb(&sblock, cgtod(&sblock, 0)), &acg);
	if (acg.cg_magic != CG_MAGIC) {
		printf("cg 0: bad magic number\n");
		exit(1);
	}
	acg.cg_cs.cs_nifree--;
	setbit(acg.cg_iused, ip->i_number);
	write_cg_block(fsbtodb(&sblock, cgtod(&sblock, 0)), &acg);
	sblock.fs_cstotal.cs_nifree--;
	fscs[0].cs_nifree--;
	if (ip->i_number >=
	    (ino_t)((unsigned long)sblock.fs_ipg * (unsigned long)sblock.fs_ncg)) {
		printf("fsinit: inode value out of range (%lu).\n",
		    (unsigned long)ip->i_number);
		exit(1);
	}
	d = fsbtodb(&sblock, itod(&sblock, ip->i_number));
	read_inode_block(d, buf);
	buf[itoo(&sblock, ip->i_number)].di_ic = ip->i_ic;
	write_inode_block(d, sblock.fs_inopb, buf);
}

/*
 * read a block from the file system
 */
static void
rdfs(daddr_t bno, int size, char *bf)
{
	int n;

	if (lseek(fsi, bno * DEV_BSIZE, 0) < 0) {
		printf("seek error: %ld\n", (long)bno);
		perror("rdfs");
		exit(1);
	}
	n = read(fsi, bf, size);
	if(n != size) {
		printf("read error: %ld\n", (long)bno);
		perror("rdfs");
		exit(1);
	}
}

/*
 * write a block to the file system
 */
static void
wtfs(daddr_t bno, int size, char *bf)
{
	int n;

	if (Nflag)
		return;
	if (lseek(fso, bno * DEV_BSIZE, 0) < 0) {
		printf("seek error: %ld\n", (long)bno);
		perror("wtfs");
		exit(1);
	}
	n = write(fso, bf, size);
	if(n != size) {
		printf("write error: %ld\n", (long)bno);
		perror("wtfs");
		exit(1);
	}
}

static uint16_t
swap16(x)
	uint16_t x;
{
	return (uint16_t)((x >> 8) | (x << 8));
}

static uint32_t
swap32(x)
	uint32_t x;
{
	return ((x & 0x000000ffU) << 24) |
	    ((x & 0x0000ff00U) << 8) |
	    ((x & 0x00ff0000U) >> 8) |
	    ((x & 0xff000000U) >> 24);
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

static void
swap_superblock(fs)
	struct fs *fs;
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

static void
swap_cg(cg)
	struct cg *cg;
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

static void
swap_inode_block_bytes(dp, count)
	struct dinode *dp;
	int count;
{
	int i, j;

	for (i = 0; i < count; i++) {
		dp[i].di_mode = swap16(dp[i].di_mode);
		dp[i].di_nlink = swap16(dp[i].di_nlink);
		dp[i].di_uid = swap16(dp[i].di_uid);
		dp[i].di_gid = swap16(dp[i].di_gid);
		dp[i].di_ic.ic_size.val[0] =
		    swap32(dp[i].di_ic.ic_size.val[0]);
		dp[i].di_ic.ic_size.val[1] =
		    swap32(dp[i].di_ic.ic_size.val[1]);
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

static void
put_dirent(dst, ino, reclen, namlen, name)
	char *dst;
	uint32_t ino;
	uint16_t reclen;
	uint16_t namlen;
	const char *name;
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

static void
write_superblock(bno, fs)
	daddr_t bno;
	const struct fs *fs;
{
	char out[SBSIZE];

	memcpy(out, (const char *)fs, sizeof(out));
	swap_superblock((struct fs *)out);
	wtfs(bno, SBSIZE, out);
}

static void
write_csum_block(bno, size, cs)
	daddr_t bno;
	int size;
	const struct csum *cs;
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

static void
write_cg_block(bno, cg)
	daddr_t bno;
	const struct cg *cg;
{
	char out[MAXBSIZE];

	memcpy(out, (const char *)cg, sblock.fs_bsize);
	swap_cg((struct cg *)out);
	wtfs(bno, sblock.fs_bsize, out);
}

static void
write_inode_block(bno, count, dp)
	daddr_t bno;
	int count;
	const struct dinode *dp;
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

static void
write_dir_block(bno, size, buf)
	daddr_t bno;
	int size;
	char *buf;
{
	wtfs(bno, size, buf);
}

static void
read_cg_block(bno, cg)
	daddr_t bno;
	struct cg *cg;
{
	rdfs(bno, sblock.fs_cgsize, (char *)cg);
	swap_cg(cg);
}

static void
read_inode_block(bno, dp)
	daddr_t bno;
	struct dinode *dp;
{
	rdfs(bno, sblock.fs_bsize, (char *)dp);
	swap_inode_block_bytes(dp, sblock.fs_inopb);
}

/*
 * check if a block is available
 */
static int
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

/*
 * take a block out of the map
 */
static void
clrblock(struct fs *fs, unsigned char *cp, int h)
{
	switch ((fs)->fs_frag) {
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

/*
 * put a block into the map
 */
static void
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
