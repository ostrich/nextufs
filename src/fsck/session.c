/* One-filesystem orchestration and source-name helpers. */

#include <stdio.h>
#include <sys/param.h>
#include <ufs/fs.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include <ufs/inode.h>
#include <sys/stat.h>
#define KERNEL
#include <ufs/fsdir.h>
#undef KERNEL
#include <mntent.h>
#include <strings.h>
#include "fsck.h"

static void
checkfilesys_active(char *filesys)
{
	daddr_t n_ffree, n_bfree;
	struct dups *dp;
	struct zlncnt *zlnp;
#if	NeXT
	int	expected_state;
#endif
	char saved_nflag, saved_yflag;

	saved_nflag = nflag;
	saved_yflag = yflag;

	if ((devname = setup(filesys)) == 0) {
		if (preen)
			pfatal("CAN'T CHECK FILE SYSTEM.");
		return;
	}
	devname = filesys;
#if	NeXT
	expected_state = (mountedfs && ! readonlyfs)
		? FS_STATE_DIRTY : FS_STATE_CLEAN;
	if (preen && sblock.fs_state == expected_state && !Pflag) {
		pinfo("file system clean: skipping check\n");
		nflag = saved_nflag;
		yflag = saved_yflag;
		return;
	}
#endif
	if (preen == 0) {
		if (mountedfs)
			printf("** Currently Mounted on %s\n", sblock.fs_fsmnt);
		else
			printf("** Last Mounted on %s\n", sblock.fs_fsmnt);
		printf("** Phase 1 - Check Blocks and Sizes\n");
	}
	pass1();

	if (duplist) {
		if (preen)
			pfatal("INTERNAL ERROR: dups with -p");
		printf("** Phase 1b - Rescan For More DUPS\n");
		pass1b();
	}

	if (preen == 0)
		printf("** Phase 2 - Check Pathnames\n");
	pass2();

	if (preen == 0)
		printf("** Phase 3 - Check Connectivity\n");
	pass3();

	if (preen == 0)
		printf("** Phase 4 - Check Reference Counts\n");
	pass4();

	if (preen == 0)
		printf("** Phase 5 - Check Cyl groups\n");
	pass5();

	n_ffree = sblock.fs_cstotal.cs_nffree;
	n_bfree = sblock.fs_cstotal.cs_nbfree;
#if	NeXT
	pinfo("%d files, %d used, %d free ",
	    n_files, n_blks, n_ffree + sblock.fs_frag * n_bfree);
#else
	pwarn("%d files, %d used, %d free ",
	    n_files, n_blks, n_ffree + sblock.fs_frag * n_bfree);
#endif
	printf("(%d frags, %d blocks, %.1f%% fragmentation)\n",
	    n_ffree, n_bfree, (float)(n_ffree * 100) / sblock.fs_dsize);
	if (debug && (n_files -= imax - ROOTINO - sblock.fs_cstotal.cs_nifree))
		printf("%d files missing\n", n_files);
	if (debug) {
		n_blks += sblock.fs_ncg *
			(cgdmin(&sblock, 0) - cgsblock(&sblock, 0));
		n_blks += cgsblock(&sblock, 0) - cgbase(&sblock, 0);
		n_blks += howmany(sblock.fs_cssize, sblock.fs_fsize);
		if (n_blks -= fmax - (n_ffree + sblock.fs_frag * n_bfree))
			printf("%d blocks missing\n", n_blks);
		if (duplist != NULL) {
			printf("The following duplicate blocks remain:");
			for (dp = duplist; dp; dp = dp->next)
				printf(" %ld,", (long)dp->dup);
			printf("\n");
		}
		if (zlnhead != NULL) {
			printf("The following zero link count inodes remain:");
			for (zlnp = zlnhead; zlnp; zlnp = zlnp->next)
				printf(" %lu,", (unsigned long)zlnp->zlncnt);
			printf("\n");
		}
	}
	zlnhead = (struct zlncnt *)0;
	duplist = (struct dups *)0;
#if	NeXT
	if (preen || (error_count == 0 && sblock.fs_state != expected_state)) {
		if (mountedfs) {
			sblock.fs_state = FS_STATE_CLEAN;
			sbdirty();
			printf("Mounted file system set to clean state");
			if (readonlyfs && !rootfs)
				printf(" -- must umount and then re-mount\n");
			else
				printf(" -- must reboot without sync\n");
		} else {
			sblock.fs_state = FS_STATE_CLEAN;
			sbdirty();
		}
	} else if (error_count) {
		printf("File system not may not be clean!  ");
		printf("Run fsck again to clean.\n");
	}
#endif
	if (dfile.mod) {
		sblock.fs_time = (int32_t)time((time_t *)0);
		sbdirty();
	}
	ckfini();
	nflag = saved_nflag;
	yflag = saved_yflag;
	free(blockmap);
	free(statemap);
	free((char *)lncntp);
	if (!dfile.mod)
		return;
	if (!preen) {
		printf("\n***** FILE SYSTEM WAS MODIFIED *****\n");
		if (mountedfs) {
			if (readonlyfs && !rootfs) {
				printf("\n***** UNMOUNT AND RE-MOUNT"
				 " FILE SYSTEM *****\n");
			} else {
				printf("\n***** REBOOT THE SYSTEM"
				 " (NO SYNC) *****\n");
			}
		}
	}
#if	NeXT
	if (usingblkdev && (!mountedfs || readonlyfs))
		(void)fsck_file_fsync(&dfile);
#endif
	if (mountedfs)
		exit(4);
}

int
checkfilesys(char *filesys, const struct fsck_runtime_options *opts)
{
	struct fsck_ctx ctx;
	int status;

	fsck_ctx_init_from_runtime(&ctx, opts);
	fsck_ctx_set_current(&ctx);
	ctx.ctx_abort_active = 1;
	status = setjmp(ctx.ctx_abort);
	if (status != 0) {
		fsck_source_cleanup();
		fsck_ctx_set_current(NULL);
		return status;
	}
	checkfilesys_active(filesys);
	ctx.ctx_abort_active = 0;
	fsck_source_cleanup();
	status = ctx.ctx_exitstat;
	fsck_ctx_set_current(NULL);
	return status;
}

char *
blockcheck(char *name, char *nametmp, size_t nametmp_size)
{
	struct stat statb;
	int looped = 0;
	char *raw;

	(void)nametmp_size;
	strcpy(nametmp, name);
	if (stat(nametmp, &statb) < 0) {
		printf("Can't stat %s\n", nametmp);
		return (0);
	}
retry:
	switch (statb.st_mode & S_IFMT) {
	case S_IFCHR:
		return (nametmp);

	case S_IFBLK:
		raw = rawname(name, nametmp, nametmp_size);
		if (raw == NULL)
			break;
		if (looped++ == 0 && stat(nametmp, &statb) >= 0)
			goto retry;
		break;
	}
	if (looped)
		printf("Can't determine raw device name for %s\n", name);
	else
		printf("%s is not block or character device\n", name);
	return (0);
}

char *
unrawname(char *cp)
{
	char *dp = rindex(cp, '/');
	struct stat stb;

	if (dp == 0)
		return (cp);
	if (stat(cp, &stb) < 0)
		return (cp);
	if ((stb.st_mode&S_IFMT) != S_IFCHR)
		return (cp);
	if (*(dp+1) != 'r')
		return (cp);
	(void)strcpy(dp+1, dp+2);
	return (cp);
}

char *
rawname(char *cp, char *rawbuf, size_t rawbuf_size)
{
	char *dp = rindex(cp, '/');

	(void)rawbuf_size;
	if (dp == 0)
		return (0);
	*dp = 0;
	(void)strcpy(rawbuf, cp);
	*dp = '/';
	(void)strcat(rawbuf, "/r");
	(void)strcat(rawbuf, dp+1);
	return (rawbuf);
}
