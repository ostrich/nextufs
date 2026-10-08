/* Native standard-L2 QCOW2 backend. Format: QEMU docs/interop/qcow2.txt. */
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
	int fd, raw, writable, failed;
	uint32_t version, cluster_bits, cluster_size, l1_size, ref_clusters;
	uint64_t size, file_size, l1_offset, ref_offset;
	uint64_t *l1, *ref_table;
	size_t ref_entries, host_count;
	uint16_t *refs;
	uint8_t *tags;
	struct qcow *parent;
};

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
	qcow_close(q->parent);
	if (q->fd >= 0)
		close(q->fd);
	free(q->l1);
	free(q->ref_table);
	free(q->refs);
	free(q->tags);
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
		if (q->tags[i] && (tag != 2 || q->tags[i] != 2))
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
		rc = mark_region(q, expected, off, q->cluster_size, 1);
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
	q->ref_entries = (size_t)q->ref_clusters * (q->cluster_size / 8U);
	q->l1 = calloc(q->l1_size, sizeof(*q->l1));
	q->ref_table = calloc(q->ref_entries, sizeof(*q->ref_table));
	q->refs = calloc(q->host_count, sizeof(*q->refs));
	q->tags = calloc(q->host_count, 1);
	if (!q->l1 || !q->ref_table || !q->refs || !q->tags) {
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
qcow_write(void *ctx, const void *buf, size_t bytes, off_t offset)
{
	(void)ctx; (void)buf; (void)bytes; (void)offset;
	return -ENOTSUP;
}

static int
qcow_sync(void *ctx)
{
	struct qcow *q = ctx;
	return fsync(q->fd) < 0 ? -errno : 0;
}

static const struct nextufs_image_backend_ops qcow_ops = {
	.read = qcow_read, .write = qcow_write, .fsync = qcow_sync, .close = qcow_close,
};

int
nextufs__qcow2_open(struct nextufs_image *img, const char *path, int writable)
{
	struct qcow *q = NULL;
	int rc;
	if (writable)
		return -ENOTSUP;
	rc = qcow_open(path, writable, 0, NULL, 0, &q);
	if (rc < 0)
		return rc;
	img->fd = q->fd;
	img->backend_ctx = q;
	img->backend_ops = &qcow_ops;
	img->writable = writable;
	img->source_is_container = 1;
	img->source_format = NEXTUFS_SOURCE_QCOW2;
	img->image_size = (off_t)q->size;
	return 0;
}
