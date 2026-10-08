/* Native standard-L2 QCOW2 backend.
 * Format: https://www.qemu.org/docs/master/interop/qcow2.html
 */
#include "nextufs_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define QCOW_MAGIC 0x514649fbU
#define QCOW_OFFSET 0x00fffffffffffe00ULL
#define QCOW_COPIED (1ULL << 63)
#define QCOW_COMPRESSED (1ULL << 62)
#define QCOW_ZERO 1ULL
#define QCOW_MAX_DEPTH 32U
#define QCOW_MAX_CLUSTERS (16U * 1024U * 1024U)
#define QCOW_MAX_L1_BYTES (32U * 1024U * 1024U)
#define QCOW_MAX_REF_BYTES (8U * 1024U * 1024U)

struct qcow_seen { dev_t dev; ino_t ino; };
struct qcow {
	int fd, raw, writable, failed, changed, table_dirty, table_moved;
	uint32_t version, cluster_bits, cluster_size, l1_size, ref_clusters;
	uint64_t size, file_size, l1_offset, ref_offset;
	uint64_t *l1, *ref_table;
	size_t ref_entries, host_count, host_capacity;
	uint16_t *refs;
	uint8_t *tags, *ref_dirty;
	struct qcow *parent;
};

static int qcow_sync(void *ctx);

static uint64_t
be64(const uint8_t *p)
{
	return ((uint64_t)nextufs__read_be32(p) << 32) | nextufs__read_be32(p + 4);
}

static int
range_valid(const struct qcow *q, uint64_t off, uint64_t bytes)
{
	return off <= q->file_size && bytes <= q->file_size - off;
}

static int
read_at(struct qcow *q, void *buf, size_t bytes, uint64_t off)
{
	if (!range_valid(q, off, bytes))
		return -EINVAL;
	return nextufs__read_exact_fd(q->fd, buf, bytes, (off_t)off);
}

static int
read_u64(struct qcow *q, uint64_t off, uint64_t *out)
{
	uint8_t b[8];
	int rc = read_at(q, b, sizeof(b), off);
	if (rc == 0)
		*out = be64(b);
	return rc;
}

static void
qcow_close(void *ctx)
{
	struct qcow *q = ctx;
	if (q == NULL)
		return;
	if (q->writable && q->changed && !q->failed)
		(void)qcow_sync(q);
	qcow_close(q->parent);
	if (q->fd >= 0)
		close(q->fd);
	free(q->l1);
	free(q->ref_table);
	free(q->refs);
	free(q->tags);
	free(q->ref_dirty);
	free(q);
}

/* Metadata may not alias anything. Shared data clusters are valid COW inputs. */
static int
mark_region(struct qcow *q, uint16_t *expected, uint64_t off, uint64_t bytes,
    uint8_t tag)
{
	uint64_t first, count, i;
	if (off % q->cluster_size || !range_valid(q, off, bytes))
		return -EINVAL;
	first = off / q->cluster_size;
	count = (bytes + q->cluster_size - 1U) / q->cluster_size;
	if (first > q->host_count || count > q->host_count - first)
		return -EINVAL;
	for (i = first; i < first + count; i++) {
		if (q->tags[i] && (tag != q->tags[i] || (tag != 2 && tag != 3)))
			return -EINVAL;
		if (expected[i] == UINT16_MAX)
			return -EINVAL;
		q->tags[i] = tag;
		expected[i]++;
	}
	return 0;
}

static int
validate_tables(struct qcow *q)
{
	uint16_t *expected;
	uint8_t *block;
	uint64_t i, j;
	int rc = -ENOMEM;

	expected = calloc(q->host_count, sizeof(*expected));
	block = malloc(q->cluster_size);
	if (!expected || !block)
		goto out;
	rc = mark_region(q, expected, 0, q->cluster_size, 1);
	if (rc < 0)
		goto out;
	rc = mark_region(q, expected, q->l1_offset, (uint64_t)q->l1_size * 8, 1);
	if (rc < 0)
		goto out;
	rc = mark_region(q, expected, q->ref_offset,
	    (uint64_t)q->ref_clusters * q->cluster_size, 1);
	if (rc < 0)
		goto out;
	for (i = 0; i < q->ref_entries; i++) {
		uint64_t off = q->ref_table[i];
		if (!off)
			continue;
		rc = mark_region(q, expected, off, q->cluster_size, 1);
		if (rc < 0 || (rc = read_at(q, block, q->cluster_size, off)) < 0)
			goto out;
		for (j = 0; j < q->cluster_size / 2U; j++) {
			uint64_t n = i * (q->cluster_size / 2U) + j;
			uint16_t value = nextufs__read_be16(block + j * 2);
			if (n < q->host_count)
				q->refs[n] = value;
			else if (value) {
				rc = -EINVAL;
				goto out;
			}
		}
	}
	for (i = 0; i < q->l1_size; i++) {
		uint64_t entry = q->l1[i], off = entry & QCOW_OFFSET;
		if (entry & ~(QCOW_OFFSET | QCOW_COPIED) || (!off && entry)) {
			rc = -EINVAL;
			goto out;
		}
		if (!off)
			continue;
		if (off % q->cluster_size || !range_valid(q, off, q->cluster_size) ||
		    !!(entry & QCOW_COPIED) != (q->refs[off / q->cluster_size] == 1)) {
			rc = -EINVAL;
			goto out;
		}
		rc = mark_region(q, expected, off, q->cluster_size, 3);
		if (rc < 0 || (rc = read_at(q, block, q->cluster_size, off)) < 0)
			goto out;
		for (j = 0; j < q->cluster_size / 8U; j++) {
			uint64_t e = be64(block + j * 8), data = e & QCOW_OFFSET;
			if (e & QCOW_COMPRESSED) {
				rc = -ENOTSUP;
				goto out;
			}
			if (e & ~(QCOW_OFFSET | QCOW_COPIED | QCOW_ZERO) ||
			    (q->version == 2 && (e & QCOW_ZERO)) ||
			    (!data && (e & QCOW_COPIED))) {
				rc = -EINVAL;
				goto out;
			}
			if (data) {
				if (data % q->cluster_size || !range_valid(q, data, q->cluster_size) ||
				    !!(e & QCOW_COPIED) != (q->refs[data / q->cluster_size] == 1)) {
					rc = -EINVAL;
					goto out;
				}
				rc = mark_region(q, expected, data, q->cluster_size, 2);
				if (rc < 0)
					goto out;
			}
		}
	}
	for (i = 0; i < q->host_count; i++) {
		/* Reject inconsistent refcounts even for read-only opens. */
		if (expected[i] != q->refs[i]) {
			rc = -EINVAL;
			goto out;
		}
	}
	rc = 0;
out:
	free(block);
	free(expected);
	return rc;
}

static int
qcow_open(const char *path, int writable, int force_raw,
    const struct qcow_seen *ancestors, unsigned depth, struct qcow **out)
{
	struct qcow *q;
	struct stat st;
	struct qcow_seen seen[QCOW_MAX_DEPTH];
	uint8_t hdr[104];
	uint32_t header_len, backing_len;
	uint64_t backing_off, ext, ext_end, i;
	char backing_format[8] = "";
	int rc;

	if (depth >= QCOW_MAX_DEPTH)
		return -ELOOP;
	q = calloc(1, sizeof(*q));
	if (!q)
		return -ENOMEM;
	q->fd = open(path, writable ? O_RDWR : O_RDONLY);
	if (q->fd < 0) {
		rc = -errno;
		goto fail;
	}
	if (fstat(q->fd, &st) < 0) {
		rc = -errno;
		goto fail;
	}
	if (!S_ISREG(st.st_mode) || st.st_size < 0) {
		rc = -EINVAL;
		goto fail;
	}
	for (i = 0; i < depth; i++) {
		if (ancestors[i].dev == st.st_dev && ancestors[i].ino == st.st_ino) {
			rc = -ELOOP;
			goto fail;
		}
	}
	if (depth)
		memcpy(seen, ancestors, depth * sizeof(*seen));
	seen[depth].dev = st.st_dev;
	seen[depth].ino = st.st_ino;
	q->file_size = (uint64_t)st.st_size;
	q->writable = writable;
	if (force_raw) {
		q->raw = 1;
		q->size = q->file_size;
		*out = q;
		return 0;
	}
	memset(hdr, 0, sizeof(hdr));
	rc = read_at(q, hdr, q->file_size < sizeof(hdr) ?
	    (size_t)q->file_size : sizeof(hdr), 0);
	if (rc < 0)
		goto fail;
	if (nextufs__read_be32(hdr) != QCOW_MAGIC) {
		/* VDI parents are outside the supported QCOW2 chain formats. */
		uint8_t sig[4];
		if (q->file_size >= 0x44 && read_at(q, sig, 4, 0x40) == 0 &&
		    sig[0] == 0x7f && sig[1] == 0x10 && sig[2] == 0xda && sig[3] == 0xbe) {
			rc = -ENOTSUP;
			goto fail;
		}
		q->raw = 1;
		q->size = q->file_size;
		*out = q;
		return 0;
	}
	q->version = nextufs__read_be32(hdr + 4);
	q->cluster_bits = nextufs__read_be32(hdr + 20);
	q->size = be64(hdr + 24);
	q->l1_size = nextufs__read_be32(hdr + 36);
	q->l1_offset = be64(hdr + 40);
	q->ref_offset = be64(hdr + 48);
	q->ref_clusters = nextufs__read_be32(hdr + 56);
	backing_off = be64(hdr + 8);
	backing_len = nextufs__read_be32(hdr + 16);
	if ((q->version != 2 && q->version != 3) ||
	    q->cluster_bits < 9 || q->cluster_bits > 21 ||
	    nextufs__read_be32(hdr + 32) || nextufs__read_be32(hdr + 60)) {
		rc = -ENOTSUP;
		goto fail;
	}
	q->cluster_size = 1U << q->cluster_bits;
	header_len = q->version == 2 ? 72 : nextufs__read_be32(hdr + 100);
	if (q->version == 3 && (be64(hdr + 72) || be64(hdr + 80) ||
	    be64(hdr + 88) || nextufs__read_be32(hdr + 96) != 4)) {
		rc = -ENOTSUP;
		goto fail;
	}
	if (!q->size || q->size > INT64_MAX || !q->l1_size ||
	    (uint64_t)q->l1_size * 8 > QCOW_MAX_L1_BYTES ||
	    !q->ref_clusters || (uint64_t)q->ref_clusters * q->cluster_size > QCOW_MAX_REF_BYTES ||
	    header_len < (q->version == 2 ? 72U : 104U) || header_len > q->cluster_size ||
	    !range_valid(q, 0, q->cluster_size) ||
	    q->size / q->cluster_size + (q->size % q->cluster_size != 0) >
	    (uint64_t)q->l1_size * (q->cluster_size / 8U) ||
	    (backing_off && (backing_len == 0 || backing_len > 1023 ||
	    backing_off < header_len || backing_off > q->cluster_size ||
	    backing_len > q->cluster_size - backing_off))) {
		rc = -EINVAL;
		goto fail;
	}
	q->host_count = (q->file_size + q->cluster_size - 1U) / q->cluster_size;
	if (q->host_count > QCOW_MAX_CLUSTERS) {
		rc = -ENOTSUP;
		goto fail;
	}
	q->host_capacity = q->host_count;
	q->ref_entries = (size_t)q->ref_clusters * (q->cluster_size / 8U);
	q->l1 = calloc(q->l1_size, sizeof(*q->l1));
	q->ref_table = calloc(q->ref_entries, sizeof(*q->ref_table));
	q->refs = calloc(q->host_count, sizeof(*q->refs));
	q->tags = calloc(q->host_count, 1);
	q->ref_dirty = calloc(q->ref_entries, 1);
	if (!q->l1 || !q->ref_table || !q->refs || !q->tags || !q->ref_dirty) {
		rc = -ENOMEM;
		goto fail;
	}
	for (i = 0; i < q->l1_size; i++) {
		rc = read_u64(q, q->l1_offset + i * 8, &q->l1[i]);
		if (rc < 0)
			goto fail;
	}
	for (i = 0; i < q->ref_entries; i++) {
		rc = read_u64(q, q->ref_offset + i * 8, &q->ref_table[i]);
		if (rc < 0)
			goto fail;
	}
	/* Extensions occupy the first cluster up to the backing filename. */
	ext_end = backing_off ? backing_off : q->cluster_size;
	for (ext = header_len; ext + 8 <= ext_end;) {
		uint8_t e[8];
		uint32_t type, len;
		rc = read_at(q, e, 8, ext);
		if (rc < 0)
			goto fail;
		type = nextufs__read_be32(e);
		len = nextufs__read_be32(e + 4);
		if (!type)
			break;
		if ((uint64_t)len > ext_end - ext - 8) {
			rc = -EINVAL;
			goto fail;
		}
		if (type == 0xe2792acaU) {
			if (len >= sizeof(backing_format) || !len || backing_format[0]) {
				rc = -ENOTSUP;
				goto fail;
			}
			rc = read_at(q, backing_format, len, ext + 8);
			if (rc < 0)
				goto fail;
			if (memchr(backing_format, 0, len) ||
			    (strcmp(backing_format, "raw") && strcmp(backing_format, "qcow2"))) {
				rc = -ENOTSUP;
				goto fail;
			}
		}
		if (type == 0x23852875U || type == 0x0537be77U || type == 0x44415441U) {
			rc = -ENOTSUP; /* bitmaps, encryption, external data */
			goto fail;
		}
		ext += 8 + (((uint64_t)len + 7) & ~7ULL);
		if (ext > ext_end) {
			rc = -EINVAL;
			goto fail;
		}
	}
	rc = validate_tables(q);
	if (rc < 0)
		goto fail;
	if (backing_off) {
		char name[1024], resolved[NEXTUFS_MAX_PATH_LEN];
		const char *slash;
		size_t prefix;
		rc = read_at(q, name, backing_len, backing_off);
		if (rc < 0)
			goto fail;
		if (memchr(name, 0, backing_len)) {
			rc = -EINVAL;
			goto fail;
		}
		name[backing_len] = 0;
		slash = strrchr(path, '/');
		prefix = name[0] == '/' || !slash ? 0 : (size_t)(slash - path + 1);
		if (prefix + backing_len >= sizeof(resolved)) {
			rc = -ENAMETOOLONG;
			goto fail;
		}
		memcpy(resolved, path, prefix);
		memcpy(resolved + prefix, name, backing_len + 1);
		rc = qcow_open(resolved, 0, !strcmp(backing_format, "raw"),
		    seen, depth + 1, &q->parent);
		if (rc < 0)
			goto fail;
		if (!strcmp(backing_format, "qcow2") && q->parent->raw) {
			rc = -EINVAL;
			goto fail;
		}
	}
	*out = q;
	return 0;
fail:
	qcow_close(q);
	return rc;
}

static int
qcow_read(void *ctx, void *buf, size_t bytes, off_t offset)
{
	struct qcow *q = ctx;
	uint8_t *out = buf;
	uint64_t pos;
	int rc;
	if (offset < 0 || (uint64_t)offset > q->size || bytes > q->size - (uint64_t)offset)
		return -EINVAL;
	if (q->raw)
		return read_at(q, buf, bytes, (uint64_t)offset);
	pos = (uint64_t)offset;
	while (bytes) {
		uint64_t guest = pos / q->cluster_size;
		uint64_t l2 = q->l1[guest / (q->cluster_size / 8U)] & QCOW_OFFSET;
		uint64_t entry = 0;
		size_t within = pos % q->cluster_size;
		size_t chunk = q->cluster_size - within;
		if (chunk > bytes)
			chunk = bytes;
		if (l2) {
			rc = read_u64(q, l2 + (guest % (q->cluster_size / 8U)) * 8, &entry);
			if (rc < 0)
				return rc;
		}
		if (entry & QCOW_ZERO)
			memset(out, 0, chunk);
		else if (entry & QCOW_OFFSET) {
			rc = read_at(q, out, chunk, (entry & QCOW_OFFSET) + within);
			if (rc < 0)
				return rc;
		} else {
			size_t inherited = 0;
			memset(out, 0, chunk);
			if (q->parent && pos < q->parent->size) {
				inherited = q->parent->size - pos < chunk ?
				    (size_t)(q->parent->size - pos) : chunk;
				rc = qcow_read(q->parent, out, inherited, (off_t)pos);
				if (rc < 0)
					return rc;
			}
		}
		out += chunk;
		pos += chunk;
		bytes -= chunk;
	}
	return 0;
}

static int
write_at(struct qcow *q, const void *buf, size_t bytes, uint64_t off)
{
	int rc = nextufs__write_exact_fd(q->fd, buf, bytes, (off_t)off);
	if (rc < 0)
		q->failed = 1;
	return rc;
}

static int
write_u64(struct qcow *q, uint64_t off, uint64_t value)
{
	uint8_t b[8];
	nextufs__write_be64(b, value);
	return write_at(q, b, sizeof(b), off);
}

static int
sync_fd(struct qcow *q)
{
	if (fsync(q->fd) < 0) {
		q->failed = 1;
		return -errno;
	}
	return 0;
}

/* Dirty is persisted before any structural or guest-data modification. */
static int
begin_write(struct qcow *q)
{
	int rc;
	if (q->failed)
		return -EIO;
	if (q->changed)
		return 0;
	if (q->version == 3) {
		rc = write_u64(q, 72, 1);
		if (rc < 0 || (rc = sync_fd(q)) < 0)
			return rc;
	}
	q->changed = 1;
	return 0;
}

static void
ref_changed(struct qcow *q, size_t cluster)
{
	size_t block = cluster / (q->cluster_size / 2U);
	if (block < q->ref_entries)
		q->ref_dirty[block] = 1;
}

static int
reserve_hosts(struct qcow *q, size_t count)
{
	size_t capacity;
	uint16_t *refs;
	uint8_t *tags;
	if (count > QCOW_MAX_CLUSTERS)
		return -EFBIG;
	if (count <= q->host_capacity)
		return 0;
	capacity = q->host_capacity ? q->host_capacity : 16;
	while (capacity < count)
		capacity = capacity > QCOW_MAX_CLUSTERS / 2U ? QCOW_MAX_CLUSTERS : capacity * 2;
	refs = realloc(q->refs, capacity * sizeof(*refs));
	if (!refs)
		return -ENOMEM;
	q->refs = refs;
	tags = realloc(q->tags, capacity);
	if (!tags)
		return -ENOMEM;
	q->tags = tags;
	memset(q->refs + q->host_capacity, 0,
	    (capacity - q->host_capacity) * sizeof(*q->refs));
	memset(q->tags + q->host_capacity, 0, capacity - q->host_capacity);
	q->host_capacity = capacity;
	return 0;
}

/* Reserve physical space only; ensure_refcounts bootstraps its own metadata. */
static int
append_hosts(struct qcow *q, size_t count, uint8_t tag, uint64_t *off)
{
	size_t first = q->host_count, i;
	uint64_t end;
	int rc;
	if (count > QCOW_MAX_CLUSTERS - first)
		return -EFBIG;
	rc = reserve_hosts(q, first + count);
	if (rc < 0)
		return rc;
	end = (uint64_t)(first + count) * q->cluster_size;
	if (end > QCOW_OFFSET || ftruncate(q->fd, (off_t)end) < 0) {
		q->failed = 1;
		return end > QCOW_OFFSET ? -EFBIG : -errno;
	}
	*off = (uint64_t)first * q->cluster_size;
	q->file_size = end;
	q->host_count += count;
	for (i = first; i < q->host_count; i++) {
		q->refs[i] = 1;
		q->tags[i] = tag;
		ref_changed(q, i);
	}
	return 0;
}

static int
flush_refcounts(struct qcow *q)
{
	uint8_t *buf = malloc(q->cluster_size);
	size_t i, j;
	int rc = 0;
	if (!buf)
		return -ENOMEM;
	for (i = 0; i < q->ref_entries; i++) {
		if (!q->ref_dirty[i])
			continue;
		if (!q->ref_table[i]) {
			rc = -EINVAL;
			goto out;
		}
		memset(buf, 0, q->cluster_size);
		for (j = 0; j < q->cluster_size / 2U; j++) {
			size_t n = i * (q->cluster_size / 2U) + j;
			if (n < q->host_count)
				nextufs__write_be16(buf + j * 2, q->refs[n]);
		}
		rc = write_at(q, buf, q->cluster_size, q->ref_table[i]);
		if (rc < 0)
			goto out;
		q->ref_dirty[i] = 0;
	}
	if (q->table_dirty) {
		for (i = 0; i < q->ref_entries; i += q->cluster_size / 8U) {
			for (j = 0; j < q->cluster_size / 8U; j++)
				nextufs__write_be64(buf + j * 8, q->ref_table[i + j]);
			rc = write_at(q, buf, q->cluster_size, q->ref_offset + i * 8);
			if (rc < 0)
				goto out;
		}
		q->table_dirty = 0;
	}
	rc = sync_fd(q);
	if (rc < 0)
		goto out;
	if (q->table_moved) {
		uint8_t fields[12];
		nextufs__write_be64(fields, q->ref_offset);
		nextufs__write_be32(fields + 8, q->ref_clusters);
		rc = write_at(q, fields, sizeof(fields), 48);
		if (rc < 0 || (rc = sync_fd(q)) < 0)
			goto out;
		q->table_moved = 0;
	}
 out:
	free(buf);
	return rc;
}

static int
ensure_refcounts(struct qcow *q, size_t pending)
{
	uint64_t off;
	size_t required, i;
	int rc;
	for (;;) {
		required = (q->host_count + pending + q->cluster_size / 2U - 1U) /
		    (q->cluster_size / 2U);
		if (required > q->ref_entries) {
			size_t entries = q->ref_entries;
			uint64_t *table;
			uint8_t *dirty;
			uint64_t old_offset = q->ref_offset;
			uint32_t old_clusters = q->ref_clusters;
			while (entries < required)
				entries *= 2;
			if (entries * 8 > QCOW_MAX_REF_BYTES)
				return -EFBIG;
			table = realloc(q->ref_table, entries * sizeof(*table));
			if (!table)
				return -ENOMEM;
			q->ref_table = table;
			dirty = realloc(q->ref_dirty, entries);
			if (!dirty)
				return -ENOMEM;
			q->ref_dirty = dirty;
			memset(table + q->ref_entries, 0,
			    (entries - q->ref_entries) * sizeof(*table));
			memset(dirty + q->ref_entries, 0, entries - q->ref_entries);
			q->ref_entries = entries;
			q->ref_clusters = entries / (q->cluster_size / 8U);
			rc = append_hosts(q, q->ref_clusters, 1, &off);
			if (rc < 0)
				return rc;
			q->ref_offset = off;
			q->table_dirty = q->table_moved = 1;
			for (i = old_offset / q->cluster_size;
			    i < old_offset / q->cluster_size + old_clusters; i++) {
				q->refs[i]--;
				q->tags[i] = 0;
				ref_changed(q, i);
			}
			continue;
		}
		/* Allocate absent refcount blocks, including blocks covering themselves. */
		for (i = 0; i < required; i++) {
			if (q->ref_table[i])
				continue;
			rc = append_hosts(q, 1, 1, &off);
			if (rc < 0)
				return rc;
			q->ref_table[i] = off;
			q->ref_dirty[i] = q->table_dirty = 1;
		}
		if ((q->host_count + pending + q->cluster_size / 2U - 1U) /
		    (q->cluster_size / 2U) == required)
			return 0;
	}
}

static int
allocate_cluster(struct qcow *q, const uint8_t *buf, uint8_t tag, uint64_t *off)
{
	int rc = ensure_refcounts(q, 1);
	if (rc < 0)
		return rc;
	rc = append_hosts(q, 1, tag, off);
	if (rc < 0)
		return rc;
	rc = write_at(q, buf, q->cluster_size, *off);
	if (rc < 0)
		return rc;
	return flush_refcounts(q);
}

/* Copied flags must describe the remaining unique mapping after COW. */
static int
refresh_copied_flags(struct qcow *q, uint64_t off, uint8_t tag)
{
	size_t i, j;
	int rc;
	for (i = 0; i < q->l1_size; i++) {
		uint64_t l2 = q->l1[i] & QCOW_OFFSET;
		if (!l2)
			continue;
		if (tag == 3 && l2 == off) {
			rc = write_u64(q, q->l1_offset + i * 8, l2 | QCOW_COPIED);
			if (rc < 0)
				return rc;
			q->l1[i] = l2 | QCOW_COPIED;
		} else if (tag == 2) {
			for (j = 0; j < q->cluster_size / 8U; j++) {
				uint64_t entry = 0;
				rc = read_u64(q, l2 + j * 8, &entry);
				if (rc < 0)
					return rc;
				if ((entry & QCOW_OFFSET) == off) {
					rc = write_u64(q, l2 + j * 8, entry | QCOW_COPIED);
					if (rc < 0)
						return rc;
				}
			}
		}
	}
	return sync_fd(q);
}

static int
drop_reference(struct qcow *q, uint64_t off)
{
	size_t n = off / q->cluster_size;
	uint8_t tag = q->tags[n];
	int rc;
	if (!q->refs[n])
		return -EINVAL;
	q->refs[n]--;
	if (!q->refs[n])
		q->tags[n] = 0;
	ref_changed(q, n);
	rc = flush_refcounts(q);
	if (rc < 0)
		return rc;
	if (q->refs[n] == 1)
		return refresh_copied_flags(q, off, tag);
	return 0;
}

static int
writable_l2(struct qcow *q, size_t index, uint8_t *buf, uint64_t *out)
{
	uint64_t old = q->l1[index] & QCOW_OFFSET, off;
	int rc;
	if (old && q->refs[old / q->cluster_size] == 1) {
		*out = old;
		return 0;
	}
	memset(buf, 0, q->cluster_size);
	if (old && (rc = read_at(q, buf, q->cluster_size, old)) < 0)
		return rc;
	rc = allocate_cluster(q, buf, 3, &off);
	if (rc < 0)
		return rc;
	rc = write_u64(q, q->l1_offset + index * 8, off | QCOW_COPIED);
	if (rc < 0 || (rc = sync_fd(q)) < 0)
		return rc;
	q->l1[index] = off | QCOW_COPIED;
	if (old && (rc = drop_reference(q, old)) < 0)
		return rc;
	*out = off;
	return 0;
}

static int
qcow_write(void *ctx, const void *buf, size_t bytes, off_t offset)
{
	struct qcow *q = ctx;
	const uint8_t *in = buf;
	uint8_t *block;
	uint64_t pos;
	int rc;
	if (!q->writable)
		return -EROFS;
	if (offset < 0 || (uint64_t)offset > q->size || bytes > q->size - (uint64_t)offset)
		return -EINVAL;
	if (!bytes)
		return 0;
	block = malloc(q->cluster_size);
	if (!block)
		return -ENOMEM;
	rc = begin_write(q);
	if (rc < 0)
		goto out;
	pos = (uint64_t)offset;
	while (bytes) {
		uint64_t guest = pos / q->cluster_size, l2 = 0, entry = 0, data;
		uint64_t entry_off;
		size_t within = pos % q->cluster_size;
		size_t chunk = q->cluster_size - within;
		if (chunk > bytes)
			chunk = bytes;
		rc = writable_l2(q, guest / (q->cluster_size / 8U), block, &l2);
		if (rc < 0)
			goto out;
		entry_off = l2 + (guest % (q->cluster_size / 8U)) * 8;
		rc = read_u64(q, entry_off, &entry);
		if (rc < 0)
			goto out;
		data = entry & QCOW_OFFSET;
		if (!data || (entry & QCOW_ZERO) || q->refs[data / q->cluster_size] != 1) {
			uint64_t off;
			size_t inherited = q->size - guest * q->cluster_size < q->cluster_size ?
			    (size_t)(q->size - guest * q->cluster_size) : q->cluster_size;
			memset(block, 0, q->cluster_size);
			rc = qcow_read(q, block, inherited, (off_t)(guest * q->cluster_size));
			if (rc < 0)
				goto out;
			memcpy(block + within, in, chunk);
			rc = allocate_cluster(q, block, 2, &off);
			if (rc < 0)
				goto out;
			rc = write_u64(q, entry_off, off | QCOW_COPIED);
			if (rc < 0 || (rc = sync_fd(q)) < 0)
				goto out;
			if (data && (rc = drop_reference(q, data)) < 0)
				goto out;
		} else {
			rc = write_at(q, in, chunk, data + within);
			if (rc < 0)
				goto out;
		}
		pos += chunk;
		in += chunk;
		bytes -= chunk;
	}
	rc = 0;
 out:
	if (rc < 0)
		q->failed = 1;
	free(block);
	return rc;
}

static int
qcow_sync(void *ctx)
{
	struct qcow *q = ctx;
	int rc;
	if (q->failed)
		return -EIO;
	if (!q->changed)
		return 0;
	rc = flush_refcounts(q);
	if (rc < 0) {
		q->failed = 1;
		return rc;
	}
	if (q->version == 3) {
		rc = write_u64(q, 72, 0);
		if (rc < 0 || (rc = sync_fd(q)) < 0) {
			/* Best effort: don't advertise a failed flush as a clean image. */
			(void)write_u64(q, 72, 1);
			(void)sync_fd(q);
			return rc;
		}
	}
	q->changed = 0;
	return 0;
}

static const struct nextufs_image_backend_ops qcow_ops = {
	.read = qcow_read, .write = qcow_write, .fsync = qcow_sync, .close = qcow_close,
};

int
nextufs__qcow2_open(struct nextufs_image *img, const char *path, int writable)
{
	struct qcow *q = NULL;
	int rc;
	rc = qcow_open(path, writable, 0, NULL, 0, &q);
	if (rc < 0)
		return rc;
	if (q->raw) {
		qcow_close(q);
		return -EINVAL;
	}
	img->fd = q->fd;
	img->backend_ctx = q;
	img->backend_ops = &qcow_ops;
	img->writable = writable;
	img->source_is_container = 1;
	img->source_format = NEXTUFS_SOURCE_QCOW2;
	img->image_size = (off_t)q->size;
	return 0;
}
