/* Incremental file updates. The caller owns an operation undo log and lock. */
#include "nextufs_internal.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint32_t
file_frags(const struct nextufs_image *img, uint64_t size, uint64_t block)
{
	uint64_t base = block * img->sb.block_size, bytes;
	if (base >= size)
		return 0;
	bytes = size - base;
	if (block >= 12 || bytes >= img->sb.block_size)
		return img->sb.frags_per_block;
	return (uint32_t)((bytes + img->sb.frag_size - 1) / img->sb.frag_size);
}

static int
size_valid(const struct nextufs_image *img, uint64_t size)
{
	(void)img;
	return size <= INT32_MAX ? 0 : -EFBIG;
}

/* Grow one block, preserving old bytes and zeroing every newly exposed byte. */
static int
grow_block(const struct nextufs_image *img, struct nextufs_node *node,
	   uint32_t logical, uint64_t old_size, uint64_t new_size)
{
	uint32_t old, allocated, metadata = 0;
	uint32_t before = file_frags(img, old_size, logical);
	uint32_t after = file_frags(img, new_size, logical);
	unsigned cg = node->inode_no / img->sb.inodes_per_group;
	uint8_t *buf;
	int rc;
	rc =
	    nextufs__resolve_inode_data_block(img, &node->inode, logical, &old);
	if (rc < 0)
		return rc;
	if (!old)
		before = 0;
	buf = calloc(1, (size_t)after * img->sb.frag_size);
	if (!buf)
		return -ENOMEM;
	if (old) {
		size_t keep = old_size - (uint64_t)logical * img->sb.block_size;
		if (keep > (size_t)before * img->sb.frag_size)
			keep = (size_t)before * img->sb.frag_size;
		rc = nextufs__read_exact(img, buf, keep,
					 img->slice_base +
					     (off_t)old * img->sb.frag_size);
		if (rc < 0)
			goto out;
	}
	allocated = old;
	if (after > before) {
		rc = old ? nextufs__extend_fragment_run(img, old, before, after)
			 : -ENOSPC;
		if (rc == -ENOSPC)
			rc = nextufs__allocate_frags_anycg(img, cg, after,
							   &allocated);
		if (rc < 0)
			goto out;
	}
	rc = nextufs__write_exact(img, buf, (size_t)after * img->sb.frag_size,
				  img->slice_base +
				      (off_t)allocated * img->sb.frag_size);
	if (rc < 0)
		goto out;
	rc = nextufs__set_inode_data_block(img, &node->inode, logical,
					   allocated, cg, &metadata);
	if (rc < 0)
		goto out;
	if (old && allocated != old) {
		rc = nextufs__free_fragment_run(img, old, before);
		if (rc < 0)
			goto out;
	}
	node->inode.blocks +=
	    (after - before + metadata) * img->sb.sectors_per_frag;
out:
	free(buf);
	return rc;
}

/* Prune allocated indirect subtrees, without walking every logical hole. */
static int
prune_tree(const struct nextufs_image *img, uint32_t *root, unsigned level,
	   uint64_t base, uint64_t keep, uint32_t *released)
{
	uint32_t count = img->sb.block_size / 4U;
	uint64_t span = 1;
	uint8_t *buf;
	int rc, any = 0;
	if (!*root)
		return 0;
	for (unsigned i = 1; i < level; i++)
		span *= count;
	buf = malloc(img->sb.block_size);
	if (!buf)
		return -ENOMEM;
	rc = nextufs__read_exact(img, buf, img->sb.block_size,
				 img->slice_base +
				     (off_t)*root * img->sb.frag_size);
	if (rc < 0)
		goto out;
	for (uint32_t i = 0; i < count; i++) {
		uint32_t child = nextufs__read_be32(buf + i * 4U);
		if (!child)
			continue;
		if (level > 1) {
			rc = prune_tree(img, &child, level - 1, base + i * span,
					keep, released);
		} else if (base + i >= keep) {
			rc = nextufs__free_fragment_run(
			    img, child, img->sb.frags_per_block);
			if (!rc) {
				child = 0;
				*released += img->sb.frags_per_block;
			}
		}
		if (rc < 0)
			goto out;
		nextufs__write_be32(buf + i * 4U, child);
		any |= child != 0;
	}
	if (any)
		rc = nextufs__write_exact(img, buf, img->sb.block_size,
					  img->slice_base +
					      (off_t)*root * img->sb.frag_size);
	else {
		rc = nextufs__free_fragment_run(img, *root,
						img->sb.frags_per_block);
		if (!rc) {
			*root = 0;
			*released += img->sb.frags_per_block;
		}
	}
out:
	free(buf);
	return rc;
}

static int
finish_inode(const struct nextufs_image *img, struct nextufs_node *node)
{
	if (img->unprivileged && nextufs_node_is_reg(node))
		node->inode.mode &= ~(04000 | 02000);
	node->inode.mtime = node->inode.ctime = (uint32_t)time(NULL);
	return nextufs__write_inode_raw(img, node->inode_no, &node->inode);
}

int
nextufs__file_resize(const struct nextufs_image *img, struct nextufs_node *node,
		     uint64_t size)
{
	uint64_t old_size = node->inode.size, block_size = img->sb.block_size;
	int rc = size_valid(img, size);
	if (rc < 0 || size == old_size)
		return rc;
	if (size > old_size) {
		uint32_t last = (uint32_t)((size - 1) / block_size);
		/* The old direct tail must become a full block before it is
		 * interior. Holes between that tail and the endpoint stay holes. */
		if (old_size % block_size && old_size / block_size != last) {
			uint32_t tail = (uint32_t)(old_size / block_size), frag;
			rc = nextufs__resolve_inode_data_block(img, &node->inode,
			    tail, &frag);
			if (rc < 0)
				return rc;
			if (frag) {
				rc = grow_block(img, node, tail, old_size, size);
				if (rc < 0)
					return rc;
			}
		}
		rc = grow_block(img, node, last, old_size, size);
		if (rc < 0)
			return rc;
	} else {
		uint32_t released = 0;
		uint64_t keep = (size + block_size - 1) / block_size;
		for (uint32_t i = 0; i < 12; i++) {
			uint32_t before = file_frags(img, old_size, i),
				 after = file_frags(img, size, i);
			if (!node->inode.db[i])
				continue;
			if (before > after) {
				rc = nextufs__free_fragment_run(
				    img, node->inode.db[i] + after,
				    before - after);
				if (rc < 0)
					return rc;
				released += before - after;
				if (!after)
					node->inode.db[i] = 0;
			}
		}
		uint64_t base = 12, span = block_size / 4;
		for (unsigned level = 1; level <= 3; level++) {
			rc = prune_tree(img, &node->inode.ib[level - 1], level,
					base, keep, &released);
			if (rc < 0)
				return rc;
			base += span;
			span *= block_size / 4;
		}
		if ((uint64_t)released * img->sb.sectors_per_frag >
		    node->inode.blocks)
			return -EINVAL;
		node->inode.blocks -= released * img->sb.sectors_per_frag;
		if (size % block_size) {
			uint32_t frag;
			uint8_t zero[8192] = {0};
			uint32_t logical = (uint32_t)(size / block_size);
			size_t within = size % block_size;
			rc = nextufs__resolve_inode_data_block(
			    img, &node->inode, logical, &frag);
			if (rc < 0)
				return rc;
			if (frag) {
				size_t bytes =
				    (size_t)file_frags(img, size, logical) *
					img->sb.frag_size -
				    within;
				rc = nextufs__write_exact(
				    img, zero, bytes,
				    img->slice_base +
					(off_t)frag * img->sb.frag_size +
					(off_t)within);
				if (rc < 0)
					return rc;
			}
		}
	}
	node->inode.size = size;
	return finish_inode(img, node);
}

int
nextufs__file_write(const struct nextufs_image *img, struct nextufs_node *node,
		    const void *data, size_t size, uint64_t offset)
{
	const uint8_t *in = data;
	int rc;
	if (!size)
		return 0;
	if (offset > UINT64_MAX - size)
		return -EFBIG;
	rc = size_valid(img, offset + size);
	if (rc < 0)
		return rc;
	if (offset + size > node->inode.size) {
		rc = nextufs__file_resize(img, node, offset + size);
		if (rc < 0)
			return rc;
	}
	while (size) {
		uint32_t logical = (uint32_t)(offset / img->sb.block_size),
			 frag;
		size_t within = offset % img->sb.block_size,
		       bytes = img->sb.block_size - within;
		if (bytes > size)
			bytes = size;
		rc = nextufs__resolve_inode_data_block(img, &node->inode,
						       logical, &frag);
		if (rc < 0)
			return rc;
		if (!frag) {
			rc = grow_block(img, node, logical,
					(uint64_t)logical * img->sb.block_size,
					node->inode.size);
			if (rc < 0)
				return rc;
			rc = nextufs__resolve_inode_data_block(
			    img, &node->inode, logical, &frag);
			if (rc < 0)
				return rc;
		}
		rc = nextufs__write_exact(img, in, bytes,
					  img->slice_base +
					      (off_t)frag * img->sb.frag_size +
					      (off_t)within);
		if (rc < 0)
			return rc;
		offset += bytes;
		in += bytes;
		size -= bytes;
	}
	return finish_inode(img, node);
}
