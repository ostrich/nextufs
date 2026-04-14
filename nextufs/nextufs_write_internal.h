#ifndef NEXTUFS_WRITE_INTERNAL_H
#define NEXTUFS_WRITE_INTERNAL_H

#include "nextufs_read.h"

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define DEV_BSIZE 1024
#define DIRBLKSIZ DEV_BSIZE
#define UFS_INODE_SIZE 128U
#define CG_MAGIC 0x00090255U
#define NRPOS 8

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

struct nextufs_write_dirent {
	uint32_t ino;
	uint16_t reclen;
	uint16_t namlen;
	const char *name;
};

uint16_t nextufs_w_read_be16(const uint8_t *p);
uint32_t nextufs_w_read_be32(const uint8_t *p);
void nextufs_w_write_be16(uint8_t *p, uint16_t v);
void nextufs_w_write_be32(uint8_t *p, uint32_t v);
void nextufs_w_write_be64(uint8_t *p, uint64_t v);
int nextufs_w_read_exact(int fd, void *buf, size_t size, off_t offset);
int nextufs_w_write_exact(int fd, const void *buf, size_t size, off_t offset);
uint64_t nextufs_w_cgstart(const struct nextufs_image *img, unsigned cg);
off_t nextufs_w_cg_block_offset(const struct nextufs_image *img, unsigned cg);
off_t nextufs_w_inode_offset(const struct nextufs_image *img, unsigned inode_no);
size_t nextufs_w_dirent_size(size_t name_len);
int nextufs_w_read_dirent(const uint8_t *buf, size_t size, size_t off,
	struct nextufs_write_dirent *ent);
int nextufs_w_write_inode_raw(const struct nextufs_image *img, unsigned inode_no,
	const struct nextufs_inode *ino);
int nextufs_w_update_summary_counts(const struct nextufs_image *img, unsigned cg,
	int32_t d_ndir, int32_t d_nbfree, int32_t d_nifree, int32_t d_nffree);
int nextufs_w_path_dirname_basename(const char *path, char *parent,
	size_t parent_size, char *name, size_t name_size);

int nextufs_w_allocate_inode_in_cg(const struct nextufs_image *img, unsigned cg,
	uint16_t mode, unsigned *inode_no_out);
int nextufs_w_free_inode_in_cg(const struct nextufs_image *img, unsigned inode_no,
	uint16_t mode);
int nextufs_w_allocate_frags_anycg(const struct nextufs_image *img,
	unsigned preferred_cg, uint32_t frags_needed, uint32_t *frag_out);
int nextufs_w_extend_fragment_run(const struct nextufs_image *img, uint32_t frag_base,
	uint32_t old_frags, uint32_t new_frags);
int nextufs_w_free_fragment_run(const struct nextufs_image *img, uint32_t frag_base,
	uint32_t frags);
int nextufs_w_reallocate_fragment_run(const struct nextufs_image *img,
	uint32_t old_frag, uint32_t old_frags, uint32_t new_frags,
	uint32_t preferred_cg, uint32_t *new_frag_out);
int nextufs_w_allocate_data_for_inode(const struct nextufs_image *img,
	unsigned preferred_cg, const uint8_t *data, size_t data_len,
	struct nextufs_inode *ino_out);
int nextufs_w_free_regular_file_storage(const struct nextufs_image *img,
	struct nextufs_inode *ino);

int nextufs_w_write_dirent_raw(uint8_t *p, unsigned ino, uint16_t reclen,
	const char *name);
int nextufs_w_update_node_times(const struct nextufs_image *img,
	struct nextufs_node *node, int set_size);
int nextufs_w_write_new_directory_block(const struct nextufs_image *img,
	uint32_t frag_addr, unsigned self_ino, unsigned parent_ino);
int nextufs_w_directory_is_empty(const struct nextufs_image *img,
	const struct nextufs_node *dirnode, unsigned parent_ino);
void nextufs_w_store_inline_symlink_bytes(struct nextufs_inode *ino,
	const char *target, size_t len);
int nextufs_w_insert_dirent_existing_space(const struct nextufs_image *img,
	struct nextufs_node *dirnode, const char *name, unsigned new_inode);
int nextufs_w_remove_dirent(const struct nextufs_image *img,
	struct nextufs_node *dirnode, const char *name, unsigned *removed_inode_out);

#endif
