#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>

#include "sockbuf.h"

typedef struct {
	z_stream instream, outstream;
	int counter;
} zipmode_t;

static int zipmode_read(int idx, int event, int level, sockbuf_iobuf_t *new_data, zipmode_t *zip)
{
	sockbuf_iobuf_t my_iobuf;
	char buf[4096];
	int r;

	my_iobuf.data = buf;
	my_iobuf.max = sizeof(buf);
	zip->instream.next_in = new_data->data;
	zip->instream.avail_in = new_data->len;
	do {
		zip->instream.next_out = buf;
		zip->instream.avail_out = sizeof(buf);
		r = inflate(&zip->instream, Z_SYNC_FLUSH);
		if (zip->instream.avail_out != sizeof(buf)) {
			my_iobuf.len = sizeof(buf) - zip->instream.avail_out;
			sockbuf_filter(idx, event, level, &my_iobuf);
		}
	} while (r == Z_OK && zip->instream.avail_in);
	return(0);
}

static int zipmode_eof_and_err(int idx, int event, int level, void *ignore, zipmode_t *zip)
{
	char buf[8192];
	sockbuf_iobuf_t iobuf;

	/* If there is any buffered data, do one more on->read callback. */
	zip->instream.next_out = buf;
	zip->instream.avail_out = sizeof(buf);
	inflate(&zip->instream, Z_FINISH);

	if (zip->instream.avail_out != sizeof(buf)) {
		iobuf.data = buf;
		iobuf.len = sizeof(buf) - zip->instream.avail_out;
		iobuf.max = sizeof(buf);
		sockbuf_filter(idx, SOCKBUF_READ, level, &iobuf);
	}
	printf("zip stats: input %d compressed / %d uncompressed\n", zip->instream.total_in, zip->instream.total_out);
	printf("zip stats: output %d compressed / %d uncompressed\n", zip->outstream.total_out, zip->outstream.total_in);
	/* And now continue the EOF/ERR event chain. */
	sockbuf_filter(idx, event, level, ignore);
	return(0);
}

static int zipmode_write(int idx, int event, int level, sockbuf_iobuf_t *data, zipmode_t *zip)
{
	sockbuf_iobuf_t my_iobuf;
	char buf[4096];
	int flags, r;

	zip->outstream.next_in = data->data;
	zip->outstream.avail_in = data->len;
	do {
		zip->outstream.next_out = buf;
		zip->outstream.avail_out = sizeof(buf);
		zip->counter++;
		if (zip->counter == 1) {
			zip->counter = 0;
			flags = Z_SYNC_FLUSH;
		}
		else flags = Z_NO_FLUSH;
		r = deflate(&zip->outstream, flags);
		if (zip->outstream.avail_out != sizeof(buf)) {
			my_iobuf.data = buf;
			my_iobuf.len = sizeof(buf) - zip->outstream.avail_out;
			my_iobuf.max = sizeof(buf);
			sockbuf_filter(idx, event, level, &my_iobuf);
		}
	} while (r == Z_OK && zip->outstream.avail_in);
	return(0);
}

static sockbuf_event_t zipmode_filter = {
	(Function) 6,
	(Function) "zip-mode",
	zipmode_read,
	NULL,
	zipmode_eof_and_err,
	zipmode_eof_and_err,
	NULL,
	zipmode_write
};

int zipmode_on(int idx)
{
	zipmode_t *zip;

	zip = (zipmode_t *)calloc(sizeof(*zip), 1);
	deflateInit(&zip->outstream, Z_DEFAULT_COMPRESSION);
	inflateInit(&zip->instream);

	sockbuf_attach_filter(idx, zipmode_filter, zip);
	return(0);
}

int zipmode_off(int idx)
{
	zipmode_t *zip;

	sockbuf_detach_filter(idx, zipmode_filter, &zip);
	if (zip) {
		deflateEnd(&zip->outstream);
		inflateEnd(&zip->instream);
		free(zip);
	}
	return(0);
}
