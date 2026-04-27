/* Shared state and helpers for nextufs.mkimg raw UFS formatting. */

#ifndef NEXTUFS_MKIMG_FORMAT_H
#define NEXTUFS_MKIMG_FORMAT_H

#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif

#ifndef STANDALONE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <stdint.h>
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

/*
 * These defaults define the on-disk layout of minimally specified filesystems.
 */
#define DFLNSECT	32
#define DFLNTRAK	16
#define DESBLKSIZE	8192
#define DESFRAGSIZE	1024
#define	DESCPG		16
#define MINFREE		10
#define DEFAULTOPT	FS_OPTTIME
#define ROTDELAY	4
#define MAXCONTIG	1
#define MAXBLKPG(fs)	((fs)->fs_fsize / sizeof(daddr_t))
#define	NBPI		2048
#define	DEFHZ		60
#define FORMAT_COMPAT_MAX_BYTES	4294836224ULL
#define FORMAT_COMPAT_MAX_SECTORS	(FORMAT_COMPAT_MAX_BYTES / 1024ULL)

#define UMASK		0755
#define MAXINOPB	(MAXBSIZE / sizeof(struct dinode))
#define POWEROF2(num)	(((num) & ((num) - 1)) == 0)

union format_superblock_store {
	struct fs fs;
	char pad[MAXBSIZE];
};

union format_cg_store {
	struct cg cg;
	char pad[MAXBSIZE];
};

extern union format_superblock_store fsun;
extern union format_cg_store cgun;
extern struct csum *fscs;
extern struct dinode zino[MAXIPG];
extern char *fsys;
extern time_t utime;
extern int fsi;
extern int fso;
extern int Nflag;
extern int format_no_create;
extern off_t format_base_offset;

#define sblock	fsun.fs
#define acg	cgun.cg

void print_usage(FILE *out);
int nextufs_format_main(int argc, char *argv[]);
void initcg(int cylno);
void fsinit(void);
int makedir(struct direct *protodir, int entries);
daddr_t alloc(int size, int mode);
void iput(struct inode *ip);
void rdfs(daddr_t bno, int size, char *bf);
void wtfs(daddr_t bno, int size, char *bf);
int isblock(struct fs *fs, unsigned char *cp, int h);
void clrblock(struct fs *fs, unsigned char *cp, int h);
void setblock(struct fs *fs, unsigned char *cp, int h);
void write_superblock(daddr_t bno, const struct fs *fs);
void write_csum_block(daddr_t bno, int size, const struct csum *cs);
void write_cg_block(daddr_t bno, const struct cg *cg);
void write_inode_block(daddr_t bno, int count, const struct dinode *dp);
void write_dir_block(daddr_t bno, int size, char *buf);
void read_cg_block(daddr_t bno, struct cg *cg);
void read_inode_block(daddr_t bno, struct dinode *dp);
void swap_csum(struct csum *cs);
void swap_superblock(struct fs *fs);
void swap_cg(struct cg *cg);
void swap_inode_block_bytes(struct dinode *dp, int count);
void put_dirent(char *dst, uint32_t ino, uint16_t reclen, uint16_t namlen,
    const char *name);

#endif
