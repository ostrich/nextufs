/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 *
 *	@(#)fsck.h	1.2 88/05/05 4.0NFSSRC SMI;	from UCB 5.1 6/5/85
 *				@(#) from SUN 1.7
 */

#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <sys/param.h>

#define	MAXDUP		10	/* limit on dup blks (per inode) */
#define	MAXBAD		10	/* limit on bad blks (per inode) */

typedef	void	(*SIG_TYP)(int);

#ifndef BUFSIZ
#define BUFSIZ 1024
#endif

#define	USTATE	01		/* inode not allocated */
#define	FSTATE	02		/* inode is file */
#define	DSTATE	03		/* inode is directory */
#define	DFOUND	04		/* directory found during descent */
#define	DCLEAR	05		/* directory is to be cleared */
#define	FCLEAR	06		/* file is to be cleared */

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
	char	b_buf[MAXBSIZE];	/* buffer space */
	short	b_lnks[SPERB];		/* link counts */
	daddr_t	b_indir[MAXNINDIR];	/* indirect block */
	struct	fs b_fs;		/* super block */
	struct	cg b_cg;		/* cylinder group */
	struct dinode b_dinode[MAXINOPB]; /* inode block */
};

#define	RAW_IO_ALIGNMENT	16

struct bufarea {
	struct bufarea	*b_next;		/* must be first */
	daddr_t	b_bno;
	int	b_size;
	int	b_errs;
	char	b_un_blk[sizeof(union buf_union) + RAW_IO_ALIGNMENT];
	union	buf_union *b_unp;
	char	b_dirty;
	char	b_swapped;
};

typedef struct bufarea BUFAREA;

BUFAREA	inoblk;			/* inode blocks */
BUFAREA	fileblk;		/* other blks in filesys */
BUFAREA	sblk;			/* file system superblock */
BUFAREA	cgblk;			/* cylinder group blocks */

#define	initbarea(x)	(x)->b_dirty = 0;				\
			(x)->b_swapped = 0;				\
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

struct filecntl {
	int	rfdes;
	int	wfdes;
	int	mod;
} dfile;			/* file descriptors for filesys */

enum fixstate {DONTKNOW, NOFIX, FIX};

struct inodesc {
	enum fixstate id_fix;	/* policy on fixing errors */
	int (*id_func)(struct inodesc *); /* function to be applied to blocks of inode */
	ino_t id_number;	/* inode number described */
	ino_t id_parent;	/* for DATA nodes, their parent */
	daddr_t id_blkno;	/* current block number being examined */
	int id_numfrags;	/* number of frags contained in block */
	long id_filesize;	/* for DATA nodes, the size of the directory */
	int id_loc;		/* for DATA nodes, current location in dir */
	int id_entryno;		/* for DATA nodes, current entry number */
	DIRECT *id_dirp;	/* for DATA nodes, ptr to current entry */
	char *id_name;		/* for DATA nodes, name to find or enter */
	char id_type;		/* type of descriptor, DATA or ADDR */
};
/* file types */
#define	DATA	1
#define	ADDR	2

/*
 * Linked list of duplicate blocks.
 * 
 * The list is composed of two parts. The first part of the
 * list (from duplist through the node pointed to by muldup)
 * contains a single copy of each duplicate block that has been 
 * found. The second part of the list (from muldup to the end)
 * contains duplicate blocks that have been found more than once.
 * To check if a block has been found as a duplicate it is only
 * necessary to search from duplist through muldup. To find the 
 * total number of times that a block has been found as a duplicate
 * the entire list must be searched for occurences of the block
 * in question. The following diagram shows a sample list where
 * w (found twice), x (found once), y (found three times), and z
 * (found once) are duplicate block numbers:
 *
 *    w -> y -> x -> z -> y -> w -> y
 *    ^		     ^
 *    |		     |
 * duplist	  muldup
 */
struct dups {
	struct dups *next;
	daddr_t dup;
};
struct dups *duplist;		/* head of dup list */
struct dups *muldup;		/* end of unique duplicate dup block numbers */

/*
 * Linked list of inodes with zero link counts.
 */
struct zlncnt {
	struct zlncnt *next;
	ino_t zlncnt;
};
struct zlncnt *zlnhead;		/* head of zero link count list */

#if	NeXT
char	rootfs;			/* is this the root file system */
char	readonlyfs;		/* is this file system mounted read-only */
char	usingblkdev;		/* doing fsck via block device */
char	needswap;		/* on-disk metadata needs byte swapping */
#else
char	rawflg;
#endif
char	*devname;
#ifdef NeXT_MOD
char	Pflag;			/* "Really" FSCK flag */
#endif
char	nflag;			/* assume a no response */
char	yflag;			/* assume a yes response */
int	bflag;			/* location of alternate super block */
int	debug;			/* output debugging info */
char	preen;			/* just fix normal inconsistencies */
char	mountedfs;		/* checking mounted device */
int	exitstat;		/* exit status (set to 8 if 'No' response) */
char	preparedsource;		/* checking extracted temporary slice */

char	*blockmap;		/* ptr to primary blk allocation map */
char	*statemap;		/* ptr to inode state table */
short	*lncntp;		/* ptr to link count table */

char	pathname[BUFSIZ];	/* current pathname */
char	*pathp;			/* pointer to pathname position */
char	*endpathname;

daddr_t	fs_maxblock;		/* number of blocks in the volume */
#define fmax fs_maxblock
ino_t	imax;			/* number of inodes */
ino_t	lastino;		/* hiwater mark of inodes */
ino_t	lfdir;			/* lost & found directory inode number */
char	*lfname;		/* lost & found directory name */

off_t	maxblk;			/* largest logical blk in file */
off_t	bmapsz;			/* num chars in blockmap */

daddr_t	n_blks;			/* number of blocks used */
daddr_t	n_files;		/* number of files seen */

#if	NeXT_MOD
int	error_count;		/* number of errors seen */
#endif

#define	zapino(x)	(*(x) = zino)
struct	dinode zino;

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
int	fsck_prepare_source(const char *source, char *prepared_path,
	    size_t prepared_path_size, int *forced_readonly_out);
void	fsck_cleanup_prepared_source(void);
struct mntent *mntdup(struct mntent *mnt);
int	pass1check(struct inodesc *idesc);
int	pass2check(struct inodesc *idesc);
int	pass4check(struct inodesc *idesc);
void	badsb(char *s);
void	swap_superblock(struct fs *fs);
void	swap_cgblock(struct cg *cg, struct fs *fs);
void	swap_inode_block(struct dinode *dinodes, int count);
void	swap_indir_block(daddr_t *indir, int count);
void	swap_dirblock(char *buf, long size);
void	fragacct(struct fs *, int, int32_t [], int);
void	errexit(char *, ...);
void	pfatal(char *, ...);
void	pwarn(char *, ...);
#if	NeXT
void	pinfo(char *, ...);
#endif
