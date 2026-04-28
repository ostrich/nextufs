#ifndef PORTS_FSCK_UFS_INODE_H
#define PORTS_FSCK_UFS_INODE_H

#include <sys/types.h>
#include <stdint.h>

#define NDADDR 12
#define NIADDR 3
#define MAX_FASTLINK_SIZE ((NDADDR + NIADDR) * sizeof(daddr_t))

#define IFMT 0170000
#define IFDIR 0040000
#define IFCHR 0020000
#define IFBLK 0060000
#define IFREG 0100000
#define IFLNK 0120000
#define IFSOCK 0140000
#define IFIFO 0010000

#define IC_FASTLINK 0x0001

struct icommon {
	uint16_t ic_mode;
	int16_t ic_nlink;
	uint16_t ic_uid;
	uint16_t ic_gid;
	quad ic_size;
	int32_t ic_atime;
	int32_t ic_atspare;
	int32_t ic_mtime;
	int32_t ic_mtspare;
	int32_t ic_ctime;
	int32_t ic_ctspare;
	union {
		struct {
			daddr_t Mb_db[NDADDR];
			daddr_t Mb_ib[NIADDR];
		} ic_Mb;
		char ic_Msymlink[MAX_FASTLINK_SIZE];
	} ic_Mun;
	int32_t ic_flags;
	int32_t ic_blocks;
	int32_t ic_gen;
	int32_t ic_spare[4];
};

struct dinode {
	union {
		struct icommon di_icom;
		char di_size[128];
	} di_un;
};

struct inode {
	ino_t i_number;
	struct icommon i_ic;
};

#define di_ic di_un.di_icom
#define di_mode di_ic.ic_mode
#define di_nlink di_ic.ic_nlink
#define di_uid di_ic.ic_uid
#define di_gid di_ic.ic_gid
#define di_size di_ic.ic_size.val[1]
#define di_db di_ic.ic_Mun.ic_Mb.Mb_db
#define di_ib di_ic.ic_Mun.ic_Mb.Mb_ib
#define di_symlink di_ic.ic_Mun.ic_Msymlink
#define di_icflags di_ic.ic_flags
#define di_atime di_ic.ic_atime
#define di_mtime di_ic.ic_mtime
#define di_ctime di_ic.ic_ctime
#define di_rdev di_ic.ic_Mun.ic_Mb.Mb_db[0]
#define di_blocks di_ic.ic_blocks
#define di_gen di_ic.ic_gen

#define i_mode i_ic.ic_mode
#define i_nlink i_ic.ic_nlink
#define i_uid i_ic.ic_uid
#define i_gid i_ic.ic_gid
#define i_size i_ic.ic_size.val[1]
#define i_db i_ic.ic_Mun.ic_Mb.Mb_db
#define i_ib i_ic.ic_Mun.ic_Mb.Mb_ib
#define i_symlink i_ic.ic_Mun.ic_Msymlink
#define i_icflags i_ic.ic_flags
#define i_atime i_ic.ic_atime
#define i_mtime i_ic.ic_mtime
#define i_ctime i_ic.ic_ctime
#define i_rdev i_ic.ic_Mun.ic_Mb.Mb_db[0]
#define i_blocks i_ic.ic_blocks
#define i_gen i_ic.ic_gen

#endif
