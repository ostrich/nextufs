/* Filesystem image construction logic for mkfs.nextufs. */

#include "mkfs.h"

void
print_usage(FILE *out)
{
	fprintf(out,
	    "usage: mkfs.nextufs [-N] <special> <size> [nsect ntrak bsize fsize cpg minfree rps nbpi opt]\n");
	fprintf(out, "\n");
	fprintf(out, "Required arguments:\n");
	fprintf(out, "  special   output image path\n");
	fprintf(out, "  size      filesystem size in 1 KiB sectors\n");
	fprintf(out, "\n");
	fprintf(out, "Optional geometry and policy arguments:\n");
	fprintf(out, "  nsect     sectors per track           default: %d\n", DFLNSECT);
	fprintf(out, "  ntrak     tracks per cylinder         default: %d\n", DFLNTRAK);
	fprintf(out, "  bsize     block size                  default: %d\n", DESBLKSIZE);
	fprintf(out, "  fsize     fragment size               default: %d\n", DESFRAGSIZE);
	fprintf(out, "  cpg       cylinders per group         default: derived, usually %d\n",
	    DESCPG);
	fprintf(out, "  minfree   reserved free space percent default: %d\n", MINFREE);
	fprintf(out, "  rps       revolutions per second      default: %d\n", DEFHZ);
	fprintf(out, "  nbpi      bytes per inode             default: %d\n", NBPI);
	fprintf(out, "  opt       allocation policy           default: t (time)\n");
	fprintf(out, "\n");
	fprintf(out, "Notes:\n");
	fprintf(out, "  - With only <special> and <size>, mkfs.nextufs uses the defaults above.\n");
	fprintf(out, "  - size does not need to be a special multiple; it must be a positive\n");
	fprintf(out, "    count of 1 KiB sectors large enough for a valid filesystem layout.\n");
	fprintf(out, "  - Additional geometry combinations may still be rejected if they are\n");
	fprintf(out, "    inconsistent with UFS layout constraints.\n");
	fprintf(out, "\n");
	fprintf(out, "Flags:\n");
	fprintf(out, "  -N        print geometry and layout details without creating a filesystem\n");
	fprintf(out, "  -h        show this help\n");
}

int
main(int argc, char *argv[])
{
	long cylno, rpos, blk, i, j, inos, nbpi, fssize, warn = 0;
	char lastbuf[DEV_BSIZE];

#ifndef STANDALONE
	argc--, argv++;
	while (argc > 0 && argv[0][0] == '-') {
		if (strcmp(argv[0], "--help") == 0 || strcmp(argv[0], "-h") == 0) {
			print_usage(stdout);
			exit(0);
		}
		switch (argv[0][1]) {
		case 'N':
			Nflag++;
			break;
		default:
			fprintf(stderr, "%s: unknown flag\n", argv[0]);
			print_usage(stderr);
			exit(1);
		}
		argc--, argv++;
	}
	time(&utime);
	if (argc < 2) {
		print_usage(stderr);
		exit(1);
	}
	fsys = argv[0];
	fssize = atoi(argv[1]);
	if (!Nflag) {
		fso = creat(fsys, 0666);
		if (fso < 0) {
			fprintf(stderr, "%s: cannot create\n", fsys);
			exit(1);
		}
	}
	fsi = open(fsys, 0);
	if (fsi < 0) {
		fprintf(stderr, "%s: cannot open\n", fsys);
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
	if (fssize <= 0)
		fprintf(stderr, "preposterous size %ld\n", fssize), exit(1);
	bzero(lastbuf, sizeof(lastbuf));
	wtfs(fssize - 1, DEV_BSIZE, lastbuf);

	if (argc > 2)
		sblock.fs_nsect = atoi(argv[2]);
	else
		sblock.fs_nsect = DFLNSECT;
	if (argc > 3)
		sblock.fs_ntrak = atoi(argv[3]);
	else
		sblock.fs_ntrak = DFLNTRAK;
	if (sblock.fs_ntrak <= 0)
		fprintf(stderr, "preposterous ntrak %d\n", sblock.fs_ntrak), exit(1);
	if (sblock.fs_nsect <= 0)
		fprintf(stderr, "preposterous nsect %d\n", sblock.fs_nsect), exit(1);
	sblock.fs_spc = sblock.fs_ntrak * sblock.fs_nsect;

	if (argc > 4)
		sblock.fs_bsize = atoi(argv[4]);
	else
		sblock.fs_bsize = DESBLKSIZE;
	if (argc > 5)
		sblock.fs_fsize = atoi(argv[5]);
	else
		sblock.fs_fsize = DESFRAGSIZE;
	if (!POWEROF2(sblock.fs_bsize)) {
		fprintf(stderr, "block size must be a power of 2, not %d\n",
		    sblock.fs_bsize);
		exit(1);
	}
	if (!POWEROF2(sblock.fs_fsize)) {
		fprintf(stderr, "fragment size must be a power of 2, not %d\n",
		    sblock.fs_fsize);
		exit(1);
	}
	if (sblock.fs_fsize < DEV_BSIZE) {
		fprintf(stderr, "fragment size %d is too small, minimum is %d\n",
		    sblock.fs_fsize, DEV_BSIZE);
		exit(1);
	}
	if (sblock.fs_bsize < MINBSIZE) {
		fprintf(stderr, "block size %d is too small, minimum is %d\n",
		    sblock.fs_bsize, MINBSIZE);
		exit(1);
	}
	if (sblock.fs_bsize < sblock.fs_fsize) {
		fprintf(stderr,
		    "block size (%d) cannot be smaller than fragment size (%d)\n",
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
		fprintf(stderr,
		    "fragment size %d is too small, minimum with block size %d is %d\n",
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
		fprintf(stderr, "maximum block size with nsect %d and ntrak %d is %d\n",
		    sblock.fs_nsect, sblock.fs_ntrak,
		    sblock.fs_bsize / (sblock.fs_cpc / MAXCPG));
		exit(1);
	}
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
		fprintf(stderr, "cylinder groups must have at least 1 cylinder\n");
		exit(1);
	}
	if (sblock.fs_cpg > MAXCPG) {
		fprintf(stderr, "cylinder groups are limited to %d cylinders\n",
		    MAXCPG);
		exit(1);
	}
	if (sblock.fs_cpg % sblock.fs_cpc != 0) {
		fprintf(stderr, "cylinder groups must have a multiple of %d cylinders\n",
		    sblock.fs_cpc);
		exit(1);
	}

	sblock.fs_size = fssize = dbtofsb(&sblock, fssize);
	sblock.fs_ncyl = fssize * NSPF(&sblock) / sblock.fs_spc;
	if (fssize * NSPF(&sblock) > sblock.fs_ncyl * sblock.fs_spc) {
		sblock.fs_ncyl++;
		warn = 1;
	}
	if (sblock.fs_ncyl < 1) {
		fprintf(stderr, "file systems must have at least one cylinder\n");
		exit(1);
	}
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
	if ((unsigned long)sblock.fs_spc > MAXBPG(&sblock) * NSPB(&sblock)) {
		fprintf(stderr, "too many sectors per cylinder (%d sectors)\n",
		    sblock.fs_spc);
		while ((unsigned long)sblock.fs_spc >
		    MAXBPG(&sblock) * NSPB(&sblock)) {
			sblock.fs_bsize <<= 1;
			if (sblock.fs_frag < MAXFRAG)
				sblock.fs_frag <<= 1;
			else
				sblock.fs_fsize <<= 1;
		}
		fprintf(stderr, "nsect %d, and ntrak %d, requires block size of %d,\n",
		    sblock.fs_nsect, sblock.fs_ntrak, sblock.fs_bsize);
		fprintf(stderr, "\tand fragment size of %d\n", sblock.fs_fsize);
		exit(1);
	}
	if ((unsigned long)sblock.fs_fpg >
	    MAXBPG(&sblock) * (unsigned long)sblock.fs_frag) {
		fprintf(stderr, "cylinder group too large (%d cylinders); ",
		    sblock.fs_cpg);
		fprintf(stderr, "max: %lu cylinders per group\n",
		    (unsigned long)(MAXBPG(&sblock) * sblock.fs_frag /
		    (sblock.fs_fpg / sblock.fs_cpg)));
		exit(1);
	}
	sblock.fs_cgsize = fragroundup(&sblock,
	    sizeof(struct cg) + howmany(sblock.fs_fpg, NBBY));
	sblock.fs_ncg = sblock.fs_ncyl / sblock.fs_cpg;
	if (sblock.fs_ncyl % sblock.fs_cpg)
		sblock.fs_ncg++;
	if ((sblock.fs_spc * sblock.fs_cpg) % NSPF(&sblock)) {
		fprintf(stderr, "mkfs: nsect %d, ntrak %d, cpg %d is not tolerable\n",
		    sblock.fs_nsect, sblock.fs_ntrak, sblock.fs_cpg);
		fprintf(stderr, "as this would would have cyl groups whose size\n");
		fprintf(stderr, "is not a multiple of %d; choke!\n", sblock.fs_fsize);
		exit(1);
	}

	inos = MAX(NBPI, sblock.fs_fsize);
	if (argc > 9) {
		i = atoi(argv[9]);
		if (i <= 0)
			fprintf(stderr, "%s: bogus nbpi reset to %ld\n", argv[9], inos);
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
		fprintf(stderr, "inode blocks/cyl group (%ld) >= data blocks (%d)\n",
		    cgdmin(&sblock, i) - cgbase(&sblock, i) / sblock.fs_frag,
		    sblock.fs_fpg / sblock.fs_frag);
		fprintf(stderr,
		    "number of cylinders per cylinder group must be increased\n");
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
			fprintf(stderr, "%s: bogus minfree reset to %d%%\n", argv[7],
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
			fprintf(stderr, "%s: bogus optimization preference %s\n",
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

	printf("%s:\t%d sectors in %d cylinders of %d tracks, %d sectors\n",
	    fsys, sblock.fs_size * NSPF(&sblock), sblock.fs_ncyl,
	    sblock.fs_ntrak, sblock.fs_nsect);
#ifndef FP_BUG
	printf("\t%.1fMb in %d cyl groups (%d c/g, %.2fMb/g, %d i/g)\n",
	    (float)sblock.fs_size * sblock.fs_fsize * 1e-6, sblock.fs_ncg,
	    sblock.fs_cpg, (float)sblock.fs_fpg * sblock.fs_fsize * 1e-6,
	    sblock.fs_ipg);
#endif
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

	fsinit();
	sblock.fs_time = utime;
	write_superblock(SBLOCK, &sblock);
	for (i = 0; i < sblock.fs_cssize; i += sblock.fs_bsize)
		write_csum_block(fsbtodb(&sblock,
		    sblock.fs_csaddr + numfrags(&sblock, i)),
		    sblock.fs_cssize - i < sblock.fs_bsize ?
		    sblock.fs_cssize - i : sblock.fs_bsize,
		    (struct csum *)(((char *)fscs) + i));
	for (cylno = 0; cylno < sblock.fs_ncg; cylno++)
		write_superblock(fsbtodb(&sblock, cgsblock(&sblock, cylno)),
		    &sblock);
#ifndef STANDALONE
	exit(0);
#endif
}
