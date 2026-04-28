/* Raw UFS formatter API used by nextufs mkimg. */

#include "format.h"

struct nextufs_format *format_current;

void
nextufs_format_defaults(struct nextufs_format_options *opts)
{
	memset(opts, 0, sizeof(*opts));
	opts->nsect = DFLNSECT;
	opts->ntrak = DFLNTRAK;
	opts->bsize = DESBLKSIZE;
	opts->fsize = DESFRAGSIZE;
	opts->minfree = MINFREE;
	opts->rps = DEFHZ;
	opts->nbpi = NBPI;
	opts->opt = 't';
}

void
format_abort(void)
{
	if (format_current != NULL && format_current->abort_active)
		longjmp(format_current->abort_env, 1);
}

static void
format_close(struct nextufs_format *ctx)
{
	if (ctx->input_fd >= 0)
		close(ctx->input_fd);
	if (ctx->output_fd >= 0)
		close(ctx->output_fd);
	free(ctx->csums);
	if (format_current == ctx)
		format_current = NULL;
}

int
nextufs_format(const struct nextufs_format_options *opts)
{
	struct nextufs_format ctx;
	long cylno, rpos, blk, i, j, inos, nbpi, fssize;
	volatile long warn = 0;
	char lastbuf[DEV_BSIZE];

	memset(&ctx, 0, sizeof(ctx));
	ctx.input_fd = -1;
	ctx.output_fd = -1;
	ctx.target_path = opts->target;
	ctx.dry_run_flag = opts->dry_run;
	ctx.base_offset_bytes = opts->base_offset;
	format_current = &ctx;
	time(&utime);
	if (setjmp(ctx.abort_env) != 0) {
		format_close(&ctx);
		return 1;
	}
	ctx.abort_active = 1;

	if (opts->target == NULL || opts->size_1k_sectors == 0 ||
	    opts->size_1k_sectors > LONG_MAX) {
		fprintf(stderr, "nextufs mkimg: invalid filesystem size\n");
		return 1;
	}
	fssize = (long)opts->size_1k_sectors;
	if (!opts->force_size &&
	    (unsigned long long)fssize > FORMAT_COMPAT_MAX_SECTORS) {
		fprintf(stderr,
		    "requested size %ld exceeds the NEXTSTEP/OPENSTEP compatibility limit of %llu 1K sectors\n",
		    fssize, (unsigned long long)FORMAT_COMPAT_MAX_SECTORS);
		return 1;
	}
	if (!Nflag && !opts->no_create) {
		fso = creat(fsys, 0666);
		if (fso < 0) {
			fprintf(stderr, "%s: cannot create\n", fsys);
			return 1;
		}
	} else if (!Nflag) {
		fso = open(fsys, O_RDWR);
		if (fso < 0) {
			fprintf(stderr, "%s: cannot open for writing\n", fsys);
			format_close(&ctx);
			return 1;
		}
	}
	fsi = open(fsys, 0);
	if (fsi < 0) {
		fprintf(stderr, "%s: cannot open\n", fsys);
		format_close(&ctx);
		return 1;
	}
	if (fssize <= 0) {
		fprintf(stderr, "preposterous size %ld\n", fssize);
		format_close(&ctx);
		return 1;
	}
	bzero(lastbuf, sizeof(lastbuf));
	wtfs(fssize - 1, DEV_BSIZE, lastbuf);

	sblock.fs_nsect = opts->nsect;
	sblock.fs_ntrak = opts->ntrak;
	if (sblock.fs_ntrak <= 0) {
		fprintf(stderr, "preposterous ntrak %d\n", sblock.fs_ntrak);
		format_close(&ctx);
		return 1;
	}
	if (sblock.fs_nsect <= 0) {
		fprintf(stderr, "preposterous nsect %d\n", sblock.fs_nsect);
		format_close(&ctx);
		return 1;
	}
	sblock.fs_spc = sblock.fs_ntrak * sblock.fs_nsect;

	sblock.fs_bsize = opts->bsize;
	sblock.fs_fsize = opts->fsize;
	if (!POWEROF2(sblock.fs_bsize)) {
		fprintf(stderr, "block size must be a power of 2, not %d\n",
		    sblock.fs_bsize);
		format_close(&ctx);
		return 1;
	}
	if (!POWEROF2(sblock.fs_fsize)) {
		fprintf(stderr, "fragment size must be a power of 2, not %d\n",
		    sblock.fs_fsize);
		format_close(&ctx);
		return 1;
	}
	if (sblock.fs_fsize < DEV_BSIZE) {
		fprintf(stderr, "fragment size %d is too small, minimum is %d\n",
		    sblock.fs_fsize, DEV_BSIZE);
		format_close(&ctx);
		return 1;
	}
	if (sblock.fs_bsize < MINBSIZE) {
		fprintf(stderr, "block size %d is too small, minimum is %d\n",
		    sblock.fs_bsize, MINBSIZE);
		format_close(&ctx);
		return 1;
	}
	if (sblock.fs_bsize < sblock.fs_fsize) {
		fprintf(stderr,
		    "block size (%d) cannot be smaller than fragment size (%d)\n",
		    sblock.fs_bsize, sblock.fs_fsize);
		format_close(&ctx);
		return 1;
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
		format_close(&ctx);
		return 1;
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
		format_close(&ctx);
		return 1;
	}
	if (opts->cpg > 0) {
		sblock.fs_cpg = opts->cpg;
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
		format_close(&ctx);
		return 1;
	}
	if (sblock.fs_cpg > MAXCPG) {
		fprintf(stderr, "cylinder groups are limited to %d cylinders\n",
		    MAXCPG);
		format_close(&ctx);
		return 1;
	}
	if (sblock.fs_cpg % sblock.fs_cpc != 0) {
		fprintf(stderr, "cylinder groups must have a multiple of %d cylinders\n",
		    sblock.fs_cpc);
		format_close(&ctx);
		return 1;
	}

	sblock.fs_size = fssize = dbtofsb(&sblock, fssize);
	sblock.fs_ncyl = fssize * NSPF(&sblock) / sblock.fs_spc;
	if (fssize * NSPF(&sblock) > sblock.fs_ncyl * sblock.fs_spc) {
		sblock.fs_ncyl++;
		warn = 1;
	}
	if (sblock.fs_ncyl < 1) {
		fprintf(stderr, "file systems must have at least one cylinder\n");
		format_close(&ctx);
		return 1;
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
		format_close(&ctx);
		return 1;
	}
	if ((unsigned long)sblock.fs_fpg >
	    MAXBPG(&sblock) * (unsigned long)sblock.fs_frag) {
		fprintf(stderr, "cylinder group too large (%d cylinders); ",
		    sblock.fs_cpg);
		fprintf(stderr, "max: %lu cylinders per group\n",
		    (unsigned long)(MAXBPG(&sblock) * sblock.fs_frag /
		    (sblock.fs_fpg / sblock.fs_cpg)));
		format_close(&ctx);
		return 1;
	}
	sblock.fs_cgsize = fragroundup(&sblock,
	    sizeof(struct cg) + howmany(sblock.fs_fpg, NBBY));
	sblock.fs_ncg = sblock.fs_ncyl / sblock.fs_cpg;
	if (sblock.fs_ncyl % sblock.fs_cpg)
		sblock.fs_ncg++;
	if ((sblock.fs_spc * sblock.fs_cpg) % NSPF(&sblock)) {
		fprintf(stderr, "nextufs mkimg: nsect %d, ntrak %d, cpg %d is not tolerable\n",
		    sblock.fs_nsect, sblock.fs_ntrak, sblock.fs_cpg);
		fprintf(stderr, "as this would would have cyl groups whose size\n");
		fprintf(stderr, "is not a multiple of %d; choke!\n", sblock.fs_fsize);
		format_close(&ctx);
		return 1;
	}

	inos = MAX(NBPI, sblock.fs_fsize);
	if (opts->nbpi > 0) {
		i = opts->nbpi;
		if (i <= 0)
			fprintf(stderr, "bogus nbpi reset to %ld\n", inos);
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
		format_close(&ctx);
		return 1;
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
	if (opts->minfree >= 0) {
		sblock.fs_minfree = opts->minfree;
		if (sblock.fs_minfree < 0 || sblock.fs_minfree > 99) {
			fprintf(stderr, "bogus minfree reset to %d%%\n",
				MINFREE);
			sblock.fs_minfree = MINFREE;
		}
	} else
		sblock.fs_minfree = MINFREE;
	sblock.fs_maxcontig = MAXCONTIG;
	sblock.fs_maxbpg = MAXBLKPG(&sblock);
	sblock.fs_rps = opts->rps;
	if (opts->opt != '\0')
		if (opts->opt == 's')
			sblock.fs_optim = FS_OPTSPACE;
		else if (opts->opt == 't')
			sblock.fs_optim = FS_OPTTIME;
		else {
			fprintf(stderr, "%c: bogus optimization preference %s\n",
				opts->opt, "reset to time");
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
	if (Nflag) {
		format_close(&ctx);
		return 0;
	}

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
	format_close(&ctx);
	return 0;
}
