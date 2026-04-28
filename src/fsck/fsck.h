/* Shared declarations for the fsck implementation. */

#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <sys/param.h>
#include "nextufs.h"

#define	MAXDUP		10
#define	MAXBAD		10

typedef	void	(*SIG_TYP)(int);

#ifndef BUFSIZ
#define BUFSIZ 1024
#endif

#define	USTATE	01
#define	FSTATE	02
#define	DSTATE	03
#define	DFOUND	04
#define	DCLEAR	05
#define	FCLEAR	06

typedef struct dinode	DINODE;
typedef struct direct	DIRECT;

#define	ALLOC(dip)	(((dip)->di_mode & IFMT) != 0)
#define	DIRCT(dip)	(((dip)->di_mode & IFMT) == IFDIR)
#define	SPECIAL(dip) \
	(((dip)->di_mode & IFMT) == IFBLK || ((dip)->di_mode & IFMT) == IFCHR)

#define	MAXNINDIR	(MAXBSIZE / sizeof (daddr_t))
#define	MAXINOPB	(MAXBSIZE / sizeof (struct dinode))
#define	SPERB		(MAXBSIZE / sizeof(short))

union buf_union {
	char	b_buf[MAXBSIZE];
	short	b_lnks[SPERB];
	daddr_t	b_indir[MAXNINDIR];
	struct	fs b_fs;
	struct	cg b_cg;
	struct dinode b_dinode[MAXINOPB];
};

#define	RAW_IO_ALIGNMENT	16

struct bufarea {
	struct bufarea	*b_next;
	daddr_t	b_bno;
	int	b_size;
	int	b_errs;
	char	b_un_blk[sizeof(union buf_union) + RAW_IO_ALIGNMENT];
	union	buf_union *b_unp;
	char	b_dirty;
	char	b_swapped;
	char	b_type;
};

typedef struct bufarea BUFAREA;

enum bufarea_type {
	BT_UNKNOWN = 0,
	BT_INDIR,
	BT_SUPER,
	BT_CG,
	BT_INODE,
	BT_DIR,
};

struct filecntl {
	int	rfdes;
	int	wfdes;
	int	mod;
	char	use_image;
	struct nextufs_image image;
};

enum fixstate {DONTKNOW, NOFIX, FIX};

struct inodesc {
	enum fixstate id_fix;
	int (*id_func)(struct inodesc *);
	ino_t id_number;
	ino_t id_parent;
	daddr_t id_blkno;
	int id_numfrags;
	long id_filesize;
	int id_loc;
	int id_entryno;
	DIRECT *id_dirp;
	char *id_name;
	char id_type;
};

#define	DATA	1
#define	ADDR	2

struct dups {
	struct dups *next;
	daddr_t dup;
};

struct zlncnt {
	struct zlncnt *next;
	ino_t zlncnt;
};

struct fsck_ctx {
	BUFAREA	ctx_inoblk;
	BUFAREA	ctx_fileblk;
	BUFAREA	ctx_sblk;
	BUFAREA	ctx_cgblk;
	struct filecntl ctx_dfile;
	struct dups *ctx_duplist;
	struct dups *ctx_muldup;
	struct zlncnt *ctx_zlnhead;
#if	NeXT
	char	ctx_rootfs;
	char	ctx_readonlyfs;
	char	ctx_usingblkdev;
	char	ctx_needswap;
#else
	char	ctx_rawflg;
#endif
	char	*ctx_devname;
	char	ctx_source_path[PATH_MAX];
	char	ctx_source_is_temp;
	char	ctx_source_force_readonly;
#ifdef NeXT_MOD
	char	ctx_Pflag;
#endif
	char	ctx_nflag;
	char	ctx_yflag;
	int	ctx_bflag;
	int	ctx_debug;
	char	ctx_preen;
	char	ctx_mountedfs;
	int	ctx_exitstat;
	char	*ctx_blockmap;
	char	*ctx_statemap;
	short	*ctx_lncntp;
	char	ctx_pathname[BUFSIZ];
	char	*ctx_pathp;
	char	*ctx_endpathname;
	struct dirtemplate ctx_emptydir;
	struct dirtemplate ctx_dirhead;
	daddr_t	ctx_fs_maxblock;
	ino_t	ctx_imax;
	ino_t	ctx_lastino;
	ino_t	ctx_lfdir;
	char	*ctx_lfname;
	off_t	ctx_maxblk;
	off_t	ctx_bmapsz;
	daddr_t	ctx_n_blks;
	daddr_t	ctx_n_files;
#if	NeXT_MOD
	int	ctx_error_count;
#endif
	struct dinode ctx_zino;
	struct csum *ctx_fsck_fs_csp[MAXCSBUFS];
};

struct fsck_runtime_options {
#if	NeXT
	char	opt_Pflag;
#endif
	char	opt_nflag;
	char	opt_yflag;
	int	opt_bflag;
	int	opt_debug;
	char	opt_preen;
};

extern struct fsck_runtime_options fsck_runtime_opts;
extern int fsck_process_exitstat;

struct fsck_ctx *fsck_ctx_current(void);
void fsck_ctx_set_current(struct fsck_ctx *ctx);
void fsck_ctx_init(struct fsck_ctx *ctx);
void fsck_ctx_init_from_runtime(struct fsck_ctx *ctx);

#define	inoblk		(fsck_ctx_current()->ctx_inoblk)
#define	fileblk		(fsck_ctx_current()->ctx_fileblk)
#define	sblk		(fsck_ctx_current()->ctx_sblk)
#define	cgblk		(fsck_ctx_current()->ctx_cgblk)
#define dfile		(fsck_ctx_current()->ctx_dfile)
#define duplist		(fsck_ctx_current()->ctx_duplist)
#define muldup		(fsck_ctx_current()->ctx_muldup)
#define zlnhead		(fsck_ctx_current()->ctx_zlnhead)
#if	NeXT
#define	rootfs		(fsck_ctx_current()->ctx_rootfs)
#define	readonlyfs	(fsck_ctx_current()->ctx_readonlyfs)
#define	usingblkdev	(fsck_ctx_current()->ctx_usingblkdev)
#define	needswap	(fsck_ctx_current()->ctx_needswap)
#else
#define	rawflg		(fsck_ctx_current()->ctx_rawflg)
#endif
#define	devname		(fsck_ctx_current()->ctx_devname)
#define	source_path	(fsck_ctx_current()->ctx_source_path)
#define	source_is_temp	(fsck_ctx_current()->ctx_source_is_temp)
#define	source_force_readonly (fsck_ctx_current()->ctx_source_force_readonly)
#ifdef NeXT_MOD
#define	Pflag		(fsck_ctx_current()->ctx_Pflag)
#endif
#define	nflag		(fsck_ctx_current()->ctx_nflag)
#define	yflag		(fsck_ctx_current()->ctx_yflag)
#define	bflag		(fsck_ctx_current()->ctx_bflag)
#define	debug		(fsck_ctx_current()->ctx_debug)
#define	preen		(fsck_ctx_current()->ctx_preen)
#define	mountedfs	(fsck_ctx_current()->ctx_mountedfs)
#define	exitstat	(fsck_ctx_current()->ctx_exitstat)
#define	blockmap	(fsck_ctx_current()->ctx_blockmap)
#define	statemap	(fsck_ctx_current()->ctx_statemap)
#define	lncntp		(fsck_ctx_current()->ctx_lncntp)
#define	pathname	(fsck_ctx_current()->ctx_pathname)
#define	pathp		(fsck_ctx_current()->ctx_pathp)
#define	endpathname	(fsck_ctx_current()->ctx_endpathname)
#define emptydir	(fsck_ctx_current()->ctx_emptydir)
#define dirhead		(fsck_ctx_current()->ctx_dirhead)
#define	fs_maxblock	(fsck_ctx_current()->ctx_fs_maxblock)
#define	imax		(fsck_ctx_current()->ctx_imax)
#define	lastino		(fsck_ctx_current()->ctx_lastino)
#define	lfdir		(fsck_ctx_current()->ctx_lfdir)
#define	lfname		(fsck_ctx_current()->ctx_lfname)
#define	maxblk		(fsck_ctx_current()->ctx_maxblk)
#define	bmapsz		(fsck_ctx_current()->ctx_bmapsz)
#define	n_blks		(fsck_ctx_current()->ctx_n_blks)
#define	n_files		(fsck_ctx_current()->ctx_n_files)
#if	NeXT_MOD
#define	error_count	(fsck_ctx_current()->ctx_error_count)
#endif
#define	zino		(fsck_ctx_current()->ctx_zino)
#define fsck_fs_csp	(fsck_ctx_current()->ctx_fsck_fs_csp)

#define	initbarea(x)	(x)->b_dirty = 0;				\
			(x)->b_swapped = 0;				\
			(x)->b_type = BT_UNKNOWN;			\
			(x)->b_bno = (daddr_t)-1;			\
			(x)->b_unp = (union buf_union *)		\
			   roundup((unsigned long)((x)->b_un_blk), RAW_IO_ALIGNMENT)

#define	dirty(x)	(x)->b_dirty = 1
#define	inodirty()	inoblk.b_dirty = 1
#define	sbdirty()	sblk.b_dirty = 1
#define	cgdirty()	cgblk.b_dirty = 1

#define	dirblk		(*fileblk.b_unp)
#define	sblock		sblk.b_unp->b_fs
#define	cgrp		cgblk.b_unp->b_cg

#define fmax fs_maxblock
#define	zapino(x)	(*(x) = zino)

#define	setbmap(x)	setbit(blockmap, x)
#define	getbmap(x)	isset(blockmap, x)
#define	clrbmap(x)	clrbit(blockmap, x)

#define	ALTERED	010
#define	KEEPON	04
#define	SKIP	02
#define	STOP	01

void	pass1(void);
void	pass1b(void);
void	pass2(void);
void	pass3(void);
void	pass4(void);
void	pass5(void);
void	checkfilesys(char *filesys);
char	*blockcheck(char *name);
char	*rawname(char *cp);
char	*unrawname(char *cp);
DINODE	*ginode(ino_t inumber);
int	ckinode(DINODE *dp, struct inodesc *idesc);
int	iblock(struct inodesc *idesc, int ilevel, long isize);
int	outrange(daddr_t blk, int cnt);
void	clri(struct inodesc *idesc, char *s, int flg);
int	findname(struct inodesc *idesc);
int	findino(struct inodesc *idesc);
void	pinode(ino_t ino);
void	blkerr(ino_t ino, char *s, daddr_t blk);
ino_t	allocino(ino_t request, int type);
void	freeino(ino_t ino);
void	descend(struct inodesc *parentino, ino_t inumber);
int	dirscan(struct inodesc *idesc);
DIRECT	*fsck_readdir(struct inodesc *idesc);
int	dircheck(struct inodesc *idesc, DIRECT *dp);
void	direrr(ino_t ino, char *s);
void	adjust(struct inodesc *idesc, short lcnt);
int	mkentry(struct inodesc *idesc);
int	chgino(struct inodesc *idesc);
int	linkup(ino_t orphan, ino_t pdir);
int	makeentry(ino_t parent, ino_t ino, char *name);
int	expanddir(DINODE *dp);
ino_t	allocdir(ino_t parent, ino_t request);
void	freedir(ino_t ino, ino_t parent);
int	lftempname(char *bufp, ino_t ino);
char	*ftypeok(DINODE *dp);
int	reply(char *s);
int	fsck_getline(FILE *fp, char *loc, int maxlen);
BUFAREA	*getblk(BUFAREA *bp, daddr_t blk, long size);
void	flush(struct filecntl *fcp, BUFAREA *bp);
void	rwerr(char *s, daddr_t blk);
void	ckfini(void);
int	bread(struct filecntl *fcp, char *buf, daddr_t blk, long size);
void	bwrite(struct filecntl *fcp, char *buf, daddr_t blk, int size);
int	fsck_file_is_writable(struct filecntl *fcp);
int	fsck_file_fsync(struct filecntl *fcp);
void	fsck_file_close(struct filecntl *fcp);
daddr_t	allocblk(int frags);
void	freeblk(daddr_t blkno, int frags);
void	getpathname(char *namebuf, ino_t curdir, ino_t ino);
void	catch(int signo);
void	catchquit(int signo);
void	voidquit(int signo);
int	dofix(struct inodesc *idesc, char *msg);
void	panic(const char *s);
int	mounted(char *name);
int	is_mounted_on(char *dir, char *dev);
void	*xmalloc(unsigned long size);
char	*setup(char *dev);
int	fsck_source_use_image_backend(const char *path);
void	fsck_source_cleanup(void);
struct mntent *mntdup(struct mntent *mnt);
int	pass1check(struct inodesc *idesc);
int	pass2check(struct inodesc *idesc);
int	pass4check(struct inodesc *idesc);
void	badsb(char *s);
void	swap_superblock(struct fs *fs);
void	swap_cgblock(struct cg *cg, struct fs *fs);
void	swap_inode_block(struct dinode *dinodes, int count);
void	swap_indir_block(daddr_t *indir, int count);
void	swap_dirblock(char *buf, long size, int dir_write_pass);
void	fragacct(struct fs *, int, int32_t [], int);
void	errexit(char *, ...);
void	pfatal(char *, ...);
void	pwarn(char *, ...);
#if	NeXT
void	pinfo(char *, ...);
#endif
