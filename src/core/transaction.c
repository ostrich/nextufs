/* Operation-local undo log. This handles returned errors, not power loss. */
#include "nextufs_internal.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

struct nextufs_transaction {
	FILE *log;
	off_t end;
};
struct undo_record {
	off_t offset;
	size_t size;
};

int
nextufs__transaction_begin(struct nextufs_image *img)
{
	struct nextufs_transaction *tx = calloc(1, sizeof(*tx));
	if (!tx)
		return -ENOMEM;
	tx->log = tmpfile();
	if (!tx->log) {
		int rc = -errno;
		free(tx);
		return rc;
	}
	/* Detect a full undo filesystem before allowing the image write. */
	setvbuf(tx->log, NULL, _IONBF, 0);
	img->transaction = tx;
	return 0;
}

int
nextufs__transaction_record(const struct nextufs_image *img, size_t size,
			    off_t offset)
{
	struct nextufs_transaction *tx = img->transaction;
	struct undo_record record = {offset, size};
	void *buf;
	int rc;
	if (!tx || !size)
		return 0;
	buf = malloc(size);
	if (!buf)
		return -ENOMEM;
	rc = nextufs__read_exact(img, buf, size, offset);
	if (!rc &&
	    (fwrite(buf, 1, size, tx->log) != size ||
	     fwrite(&record, 1, sizeof(record), tx->log) != sizeof(record)))
		rc = -EIO;
	free(buf);
	if (!rc) {
		off_t end = ftello(tx->log);
		if (end < 0)
			rc = -EIO;
		else
			tx->end = end;
	}
	return rc;
}

int
nextufs__transaction_end(struct nextufs_image *img, int status)
{
	struct nextufs_transaction *tx = img->transaction;
	const struct nextufs_image_backend_ops *ops = img->backend_ops;
	int failed = 0;
	if (!tx)
		return status;
	img->transaction = NULL;
	if (status < 0) {
		clearerr(tx->log);
		while (tx->end > 0) {
			struct undo_record record;
			void *buf;
			if (fseeko(tx->log, tx->end - (off_t)sizeof(record),
				   SEEK_SET) ||
			    fread(&record, 1, sizeof(record), tx->log) !=
				sizeof(record)) {
				failed = 1;
				break;
			}
			tx->end -= (off_t)(sizeof(record) + record.size);
			buf = malloc(record.size);
			if (!buf) {
				failed = 1;
				break;
			}
			if (fseeko(tx->log, tx->end, SEEK_SET) ||
			    fread(buf, 1, record.size, tx->log) !=
				record.size ||
			    ops->write(img->backend_ctx, buf, record.size,
				       record.offset) < 0)
				failed = 1;
			free(buf);
			if (failed)
				break;
		}
		if (ops->fsync(img->backend_ctx) < 0)
			failed = 1;
	}
	fclose(tx->log);
	free(tx);
	return failed ? -EUCLEAN : status;
}
