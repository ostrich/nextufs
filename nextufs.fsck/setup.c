/* Filesystem-open and superblock setup logic. */

#include <stdio.h>
#include <sys/param.h>
#include <ufs/fs.h>
#include <sys/time.h>
#include <sys/vnode.h>
#if	NeXT
#include <sys/file.h>
#endif
#include <ufs/inode.h>
#include <sys/stat.h>
#define KERNEL
#include <ufs/fsdir.h>
#undef KERNEL
#include <mntent.h>
#include <strings.h>
#include "fsck.h"

char *
setup(char *dev)
{
	struct stat statb;
	daddr_t super = bflag ? bflag : SBLOCK;
	int i, j;
	long size;
	BUFAREA asblk;
	static char devstr[MAXPATHLEN];
#	define altsblock asblk.b_unp->b_fs
	strcpy(devstr, dev);
restat:
	if (stat(devstr, &statb) < 0) {
		printf("Can't stat %s\n", devstr);
		return (0);
	}
#if	NeXT
	usingblkdev = 0;
	switch (statb.st_mode & S_IFMT) {

	case S_IFBLK:
		usingblkdev++;
		break;

	case S_IFCHR:
		break;

	case S_IFDIR: {
		FILE *fstab;
		struct mntent *mnt;
		/*
		 * Check fstab for a mount point with this name
		 */
		if ((fstab = setmntent(MNTTAB, "r")) == NULL) {
			errexit("Can't open checklist file: %s\n", MNTTAB);
		}
		while ((mnt = getmntent(fstab)) != NULL) {
			if (strcmp(devstr, mnt->mnt_dir) == 0) {
				if (strcmp(mnt->mnt_type, MNTTYPE_43) != 0) {
					/*
					 * found the entry but it is not a
					 * 4.2 filesystem, don't check it
					 */
					endmntent(fstab);
					return (0);
				}
				strcpy(devstr, mnt->mnt_fsname);
				goto restat;
			}
		}
		endmntent(fstab);
		break; }

	default:
		if ((statb.st_mode & S_IFMT) == S_IFREG)
			break;
		if (reply("file is not a block or character device; OK") == 0)
			return (0);
		break;
	}
	(void) mounted(devstr);	/* sets mountedfs, readonlyfs, and rootfs */
	if (usingblkdev && mountedfs && !readonlyfs) {
		pinfo("WARNING: Using block device with file system"
		 " mounted read-write!\n");
		if (!preen && reply("CONTINUE? ") == 0)
			return (0);
	}
#else
	rawflg = 0;
	if ((statb.st_mode & S_IFMT) == S_IFBLK)
		;
	else if ((statb.st_mode & S_IFMT) == S_IFCHR)
		rawflg++;
	else if ((statb.st_mode & S_IFMT) == S_IFDIR) {
		FILE *fstab;
		struct mntent *mnt;
		/*
		 * Check fstab for a mount point with this name
		 */
		if ((fstab = setmntent(MNTTAB, "r")) == NULL) {
			errexit("Can't open checklist file: %s\n", MNTTAB);
		}
		while ((mnt = getmntent(fstab)) != NULL) {
			if (strcmp(devstr, mnt->mnt_dir) == 0) {
				if (strcmp(mnt->mnt_type, MNTTYPE_43) != 0) {
					/*
					 * found the entry but it is not a
					 * 4.2 filesystem, don't check it
					 */
					endmntent(fstab);
					return (0);
				}
				strcpy(devstr, mnt->mnt_fsname);
				if (rawflg) {
					raw =
					    rawname(unrawname(mnt->mnt_fsname));
					strcpy(devstr, raw);
				}
				goto restat;
			}
		}
		endmntent(fstab);
	} else {
		if (reply("file is not a block or character device; OK") == 0)
			return (0);
	}
	if (mounted(devstr))
		mountedfs++;
#endif
	if (preen == 0)
		printf("** %s", devstr);
	dfile.rfdes = -1;
	dfile.wfdes = -1;
	dfile.use_image = 0;
	dfile.image.fd = -1;
	if (fsck_source_use_image_backend(devstr)) {
		int rc;

		if (!nflag)
			rc = nextufs_image_open_rw(&dfile.image, devstr);
		else
			rc = -1;
		if (rc < 0) {
			if (nextufs_image_open(&dfile.image, devstr) < 0) {
				printf("Can't open %s\n", devstr);
				return (0);
			}
		}
		dfile.use_image = 1;
		if (nflag || !dfile.image.writable) {
			if (preen)
				pfatal("NO WRITE ACCESS");
			printf(" (NO WRITE)");
		}
	} else {
		if ((dfile.rfdes = open(devstr, 0)) < 0) {
			printf("Can't open %s\n", devstr);
			return (0);
		}
		if (nflag || (dfile.wfdes = open(devstr, 1)) < 0) {
			dfile.wfdes = -1;
			if (preen)
				pfatal("NO WRITE ACCESS");
			printf(" (NO WRITE)");
		}
	}
	if (preen == 0)
		printf("\n");
	dfile.mod = 0;
	lfdir = 0;
	initbarea(&sblk);
	initbarea(&fileblk);
	initbarea(&inoblk);
	initbarea(&cgblk);
	initbarea(&asblk);
	sblk.b_type = BT_SUPER;
	fileblk.b_type = BT_DIR;
	inoblk.b_type = BT_INODE;
	cgblk.b_type = BT_CG;
	asblk.b_type = BT_SUPER;
	/*
	 * Read in the super block and its summary info.
	 */
	if (bread(&dfile, (char *)&sblock, super, (long)SBSIZE) != 0)
		return (0);
	needswap = 0;
	if (sblock.fs_magic != FS_MAGIC) {
		struct fs tmp;

		tmp = sblock;
		swap_superblock(&tmp);
		if (tmp.fs_magic == FS_MAGIC) {
			sblock = tmp;
			needswap = 1;
		}
	}
	sblk.b_bno = super;
	sblk.b_size = SBSIZE;
	sblk.b_swapped = needswap;
	/*
	 * run a few consistency checks of the super block
	 */
	if (sblock.fs_magic != FS_MAGIC)
		{ badsb("MAGIC NUMBER WRONG"); return (0); }
	if (sblock.fs_ncg < 1)
		{ badsb("NCG OUT OF RANGE"); return (0); }
	if (sblock.fs_cpg < 1 || sblock.fs_cpg > MAXCPG)
		{ badsb("CPG OUT OF RANGE"); return (0); }
	if (sblock.fs_ncg * sblock.fs_cpg < sblock.fs_ncyl ||
	    (sblock.fs_ncg - 1) * sblock.fs_cpg >= sblock.fs_ncyl)
		{ badsb("NCYL DOES NOT JIVE WITH NCG*CPG"); return (0); }
	if (sblock.fs_sbsize > SBSIZE)
		{ badsb("SIZE PREPOSTEROUSLY LARGE"); return (0); }
	/*
	 * Check and potentially fix certain fields in the super block.
	 */
	if (sblock.fs_optim != FS_OPTTIME && sblock.fs_optim != FS_OPTSPACE) {
		pfatal("UNDEFINED OPTIMIZATION IN SUPERBLOCK");
		if (reply("SET TO DEFAULT") == 1) {
			sblock.fs_optim = FS_OPTTIME;
			sbdirty();
		}
	}
	if ((sblock.fs_minfree < 0 || sblock.fs_minfree > 99)) {
		pfatal("IMPOSSIBLE MINFREE=%d IN SUPERBLOCK",
			sblock.fs_minfree);
		if (reply("SET TO DEFAULT") == 1) {
			sblock.fs_minfree = 10;
			sbdirty();
		}
	}
	/*
	 * Set all possible fields that could differ, then do check
	 * of whole super block against an alternate super block.
	 * When an alternate super-block is specified this check is skipped.
	 */
	if (bflag)
		goto sbok;
	getblk(&asblk, cgsblock(&sblock, sblock.fs_ncg - 1), sblock.fs_sbsize);
	if (asblk.b_errs != 0)
		return (0);
	altsblock.fs_link = sblock.fs_link;
	altsblock.fs_rlink = sblock.fs_rlink;
	altsblock.fs_time = sblock.fs_time;
	altsblock.fs_cstotal = sblock.fs_cstotal;
	altsblock.fs_cgrotor = sblock.fs_cgrotor;
	altsblock.fs_fmod = sblock.fs_fmod;
#if	NeXT_MOD
	altsblock.fs_state = sblock.fs_state;
#else
	altsblock.fs_clean = sblock.fs_clean;
#endif
	altsblock.fs_ronly = sblock.fs_ronly;
	altsblock.fs_flags = sblock.fs_flags;
	altsblock.fs_maxcontig = sblock.fs_maxcontig;
	altsblock.fs_minfree = sblock.fs_minfree;
	altsblock.fs_optim = sblock.fs_optim;
	altsblock.fs_rotdelay = sblock.fs_rotdelay;
	altsblock.fs_maxbpg = sblock.fs_maxbpg;
	bcopy((char *)sblock.fs_csp_pad, (char *)altsblock.fs_csp_pad,
		sizeof sblock.fs_csp_pad);
	bcopy((char *)sblock.fs_fsmnt, (char *)altsblock.fs_fsmnt,
		sizeof sblock.fs_fsmnt);
	if (bcmp((char *)&sblock, (char *)&altsblock, (int)sblock.fs_sbsize))
		{ badsb("TRASHED VALUES IN SUPER BLOCK"); return (0); }
sbok:
	fmax = sblock.fs_size;
	imax = sblock.fs_ncg * sblock.fs_ipg;
	/*
	 * read in the summary info.
	 */
	for (i = 0, j = 0; i < sblock.fs_cssize; i += sblock.fs_bsize, j++) {
		size = sblock.fs_cssize - i < sblock.fs_bsize ?
		    sblock.fs_cssize - i : sblock.fs_bsize;
		fsck_fs_csp[j] = (struct csum *)calloc(1, (unsigned)size);
		if (bread(&dfile, (char *)fsck_fs_csp[j],
		    fsbtodb(&sblock, sblock.fs_csaddr + j * sblock.fs_frag),
		    size) != 0)
			return (0);
		if (needswap) {
			int k, ncs;

			ncs = size / sizeof(struct csum);
			for (k = 0; k < ncs; k++) {
				struct csum *cs;

				cs = &fsck_fs_csp[j][k];
				cs->cs_ndir = ((cs->cs_ndir & 0xff) << 24) |
				    ((cs->cs_ndir & 0xff00) << 8) |
				    ((cs->cs_ndir & 0xff0000) >> 8) |
				    ((cs->cs_ndir >> 24) & 0xff);
				cs->cs_nbfree = ((cs->cs_nbfree & 0xff) << 24) |
				    ((cs->cs_nbfree & 0xff00) << 8) |
				    ((cs->cs_nbfree & 0xff0000) >> 8) |
				    ((cs->cs_nbfree >> 24) & 0xff);
				cs->cs_nifree = ((cs->cs_nifree & 0xff) << 24) |
				    ((cs->cs_nifree & 0xff00) << 8) |
				    ((cs->cs_nifree & 0xff0000) >> 8) |
				    ((cs->cs_nifree >> 24) & 0xff);
				cs->cs_nffree = ((cs->cs_nffree & 0xff) << 24) |
				    ((cs->cs_nffree & 0xff00) << 8) |
				    ((cs->cs_nffree & 0xff0000) >> 8) |
				    ((cs->cs_nffree >> 24) & 0xff);
			}
		}
	}
	/*
	 * allocate and initialize the necessary maps
	 */
	bmapsz = roundup(howmany(fmax, NBBY), sizeof(short));
	blockmap = calloc((unsigned)bmapsz, sizeof (char));
	if (blockmap == NULL) {
		printf("cannot alloc %ld bytes for blockmap\n", (long)bmapsz);
		goto badsb;
	}
	statemap = calloc((unsigned)(imax + 1), sizeof(char));
	if (statemap == NULL) {
		printf("cannot alloc %lu bytes for statemap\n",
		    (unsigned long)(imax + 1));
		goto badsb;
	}
	lncntp = (short *)calloc((unsigned)(imax + 1), sizeof(short));
	if (lncntp == NULL) {
		printf("cannot alloc %lu bytes for lncntp\n",
		    (unsigned long)((imax + 1) * sizeof(short)));
		goto badsb;
	}

	return (devstr);

badsb:
	ckfini();
	return (0);
#	undef altsblock
}

void
badsb(char *s)
{

	if (preen)
		printf("%s: ", devname);
	printf("BAD SUPER BLOCK: %s\n", s);
	pwarn("USE -b OPTION TO FSCK TO SPECIFY LOCATION OF AN ALTERNATE\n");
	pfatal("SUPER-BLOCK TO SUPPLY NEEDED INFORMATION; SEE fsck(8).\n");
}
