#ifndef NEXTUFS_INTERNAL_H
#define NEXTUFS_INTERNAL_H

#include "nextufs.h"

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define NEXTUFS_MAX_LOOKUP_DEPTH 16
#define NEXTUFS_MAX_PATH_LEN 4096

#define DEV_BSIZE 1024
#define DIRBLKSIZ DEV_BSIZE
#define UFS_INODE_SIZE 128U
#define CG_MAGIC 0x00090255U
#define NRPOS 8
#define NEXTUFS_IC_FASTLINK 0x0001U

#define SB_TIME_OFF 0x20U
#define SB_NDIR_OFF 0x0c0U
#define SB_NBFREE_OFF 0x0c4U
#define SB_NIFREE_OFF 0x0c8U
#define SB_NFFREE_OFF 0x0ccU

#define CG_TIME_OFF 8U
#define CG_CGX_OFF 12U
#define CG_NCYL_OFF 16U
#define CG_CS_NDIR_OFF 24U
#define CG_CS_NBFREE_OFF 28U
#define CG_CS_NIFREE_OFF 32U
#define CG_CS_NFFREE_OFF 36U
#define CG_ROTOR_OFF 40U
#define CG_IROTOR_OFF 48U
#define CG_BTOT_OFF 84U
#define CG_BPOS_OFF 212U
#define CG_IUSED_OFF 724U
#define CG_MAGIC_OFF 980U
#define CG_FREE_OFF 984U

struct nextufs_dirent_view {
	uint32_t ino;
	uint16_t reclen;
	uint16_t namlen;
	const char *name;
};

int nextufs__find_name_in_directory(const struct nextufs_image *img,
	const struct nextufs_inode *dirino, const char *target_name,
	unsigned *target_inode);
int nextufs__node_check_access(const struct nextufs_node *node, uid_t uid,
	gid_t gid, const gid_t *groups, size_t group_count, int mask,
	int allow_write);

uint16_t nextufs__read_be16(const uint8_t *p);
uint32_t nextufs__read_be32(const uint8_t *p);
void nextufs__write_be16(uint8_t *p, uint16_t v);
void nextufs__write_be32(uint8_t *p, uint32_t v);
void nextufs__write_be64(uint8_t *p, uint64_t v);
int nextufs__read_exact(int fd, void *buf, size_t size, off_t offset);
int nextufs__write_exact(int fd, const void *buf, size_t size, off_t offset);
uint64_t nextufs__cgstart(const struct nextufs_image *img, unsigned cg);
off_t nextufs__cg_block_offset(const struct nextufs_image *img, unsigned cg);
off_t nextufs__inode_offset(const struct nextufs_image *img, unsigned inode_no);
size_t nextufs__dirent_size(size_t name_len);
int nextufs__read_dirent(const uint8_t *buf, size_t size, size_t off,
	struct nextufs_dirent_view *ent);
int nextufs__write_inode_raw(const struct nextufs_image *img, unsigned inode_no,
	const struct nextufs_inode *ino);
int nextufs__update_summary_counts(const struct nextufs_image *img, unsigned cg,
	int32_t d_ndir, int32_t d_nbfree, int32_t d_nifree, int32_t d_nffree);
int nextufs__path_split(const char *path, char *parent, size_t parent_size,
	char *name, size_t name_size);
int nextufs__allocate_inode_in_group(const struct nextufs_image *img,
	unsigned cg, uint16_t mode, unsigned *inode_no_out);
int nextufs__free_inode_in_group(const struct nextufs_image *img,
	unsigned inode_no, uint16_t mode);
int nextufs__allocate_frags_anycg(const struct nextufs_image *img,
	unsigned preferred_cg, uint32_t frags_needed, uint32_t *frag_out);
int nextufs__extend_fragment_run(const struct nextufs_image *img,
	uint32_t frag_base, uint32_t old_frags, uint32_t new_frags);
int nextufs__free_fragment_run(const struct nextufs_image *img,
	uint32_t frag_base, uint32_t frags);
int nextufs__reallocate_fragment_run(const struct nextufs_image *img,
	uint32_t old_frag, uint32_t old_frags, uint32_t new_frags,
	uint32_t preferred_cg, uint32_t *new_frag_out);
int nextufs__allocate_full_block_anycg(const struct nextufs_image *img,
	unsigned preferred_cg, uint32_t *frag_out);
int nextufs__read_indirect_entry(const struct nextufs_image *img,
	uint32_t block_frag, uint64_t entry_index, uint32_t *entry_out);
int nextufs__write_indirect_entry(const struct nextufs_image *img,
	uint32_t block_frag, uint64_t entry_index, uint32_t entry_value);
int nextufs__allocate_data_for_inode(const struct nextufs_image *img,
	unsigned preferred_cg, const uint8_t *data, size_t data_len,
	struct nextufs_inode *ino_out);
int nextufs__free_file_storage(const struct nextufs_image *img,
	struct nextufs_inode *ino);
int nextufs__write_dirent_raw(uint8_t *p, unsigned ino, uint16_t reclen,
	const char *name);
int nextufs__touch_node(const struct nextufs_image *img,
	struct nextufs_node *node, int set_size);
int nextufs__write_new_directory_block(const struct nextufs_image *img,
	uint32_t frag_addr, unsigned self_ino, unsigned parent_ino);
int nextufs__directory_is_empty(const struct nextufs_image *img,
	const struct nextufs_node *dirnode, unsigned parent_ino);
void nextufs__store_inline_symlink(struct nextufs_inode *ino,
	const char *target, size_t len);
int nextufs__insert_dirent(const struct nextufs_image *img,
	struct nextufs_node *dirnode, const char *name, unsigned new_inode);
int nextufs__remove_dirent(const struct nextufs_image *img,
	struct nextufs_node *dirnode, const char *name, unsigned *removed_inode_out);
int nextufs__read_directory_parent_inode(const struct nextufs_image *img,
	const struct nextufs_node *dirnode, unsigned *parent_inode_out);
int nextufs__update_directory_parent_inode(const struct nextufs_image *img,
	struct nextufs_node *dirnode, unsigned parent_inode);

#endif
