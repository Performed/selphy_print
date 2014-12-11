/*
 *   Mitsubishi CP-9550DW[-S] Photo Printer CUPS backend
 *
 *   (c) 2014 Solomon Peachy <pizza@shaftnet.org>
 *
 *   The latest version of this program can be found at:
 *
 *     http://git.shaftnet.org/cgit/selphy_print.git
 *
 *   This program is free software; you can redistribute it and/or modify it
 *   under the terms of the GNU General Public License as published by the Free
 *   Software Foundation; either version 3 of the License, or (at your option)
 *   any later version.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 *   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *   for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *          [http://www.gnu.org/licenses/gpl-3.0.html]
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

#include "backend_common.h"

#define USB_VID_MITSU       0x06D3
#define USB_PID_MITSU_9550D  0x03A1
#define USB_PID_MITSU_9550DZ 0x03A5

/* Private data stucture */
struct mitsu9550_ctx {
	struct libusb_device_handle *dev;
	uint8_t endp_up;
	uint8_t endp_down;

	uint8_t *databuf;
	int datalen;

	uint16_t rows;
	uint16_t cols;
};

/* Spool file structures */
struct mitsu9550_hdr1 {
	uint8_t  cmd[4]; /* 1b 57 20 2e */
	uint8_t  unk[10]; 
	uint16_t rows; /* BE */
	uint16_t cols; /* BE */
	uint8_t  null[32];
} __attribute__((packed));

struct mitsu9550_hdr2 {
	uint8_t  cmd[4]; /* 1b 57 22 2e */
	uint8_t  unk[28];
	uint8_t  cut; /* 00 == normal, 83 == 2x6*2 */
	uint8_t  unkb[6];
	uint8_t  mode; /* 00 == normal, 80 == fine */
	uint8_t  unkc[11];
} __attribute__((packed));

struct mitsu9550_hdr3 {
	uint8_t  cmd[4]; /* 1b 57 22 2e */
	uint8_t  unk[7];
	uint8_t  mode2;  /* 00 == normal, 01 == finedeep */
	uint8_t  unkb[38];
} __attribute__((packed));

struct mitsu9550_hdr4 {
	uint8_t  cmd[4]; /* 1b 57 26 2e */
	uint8_t  unk[46];
} __attribute__((packed));

struct mitsu9550_plane {
	uint8_t  cmd[4]; /* 1b 5a 54 00 */
	uint8_t  null[2];
	uint16_t rem_rows;  /* BE, normally 0 */
	uint16_t columns;   /* BE */
	uint16_t rows;      /* BE */
} __attribute__((packed));

struct mitsu9550_footer {
	uint8_t cmd[4]; /* 1b 50 46 00 */
} __attribute__((packed));

/* Printer data structures */
struct mitsu9550_media {

} __attribute__((packed));

struct mitsu9550_status {

} __attribute__((packed));

struct mitsu9550_status2 {

} __attribute__((packed));

#define CMDBUF_LEN   64
#define READBACK_LEN 128

static void *mitsu9550_init(void)
{
	struct mitsu9550_ctx *ctx = malloc(sizeof(struct mitsu9550_ctx));
	if (!ctx)
		return NULL;
	memset(ctx, 0, sizeof(struct mitsu9550_ctx));

	return ctx;
}

static void mitsu9550_attach(void *vctx, struct libusb_device_handle *dev,
			    uint8_t endp_up, uint8_t endp_down, uint8_t jobid)
{
	struct mitsu9550_ctx *ctx = vctx;

	UNUSED(jobid);

	ctx->dev = dev;
	ctx->endp_up = endp_up;
	ctx->endp_down = endp_down;
}


static void mitsu9550_teardown(void *vctx) {
	struct mitsu9550_ctx *ctx = vctx;

	if (!ctx)
		return;

	if (ctx->databuf)
		free(ctx->databuf);
	free(ctx);
}

static int mitsu9550_read_parse(void *vctx, int data_fd) {
	struct mitsu9550_ctx *ctx = vctx;
	struct mitsu9550_hdr1 hdr;

	int remain, i;

	if (!ctx)
		return CUPS_BACKEND_FAILED;

	if (ctx->databuf) {
		free(ctx->databuf);
		ctx->databuf = NULL;
	}

	/* Read in initial header */
	remain = sizeof(hdr);
	while (remain > 0) {
		i = read(data_fd, ((uint8_t*)&hdr) + sizeof(hdr) - remain, remain);
		if (i == 0)
			return CUPS_BACKEND_CANCEL;
		if (i < 0)
			return CUPS_BACKEND_CANCEL;
		remain -= i;
	}

	/* Sanity check */
	if (hdr.cmd[0] != 0x1b ||
	    hdr.cmd[1] != 0x57 ||
	    hdr.cmd[2] != 0x20 ||
	    hdr.cmd[3] != 0x2e) {
		ERROR("Unrecognized data format!\n");
		return CUPS_BACKEND_CANCEL;
	}

	/* Work out printjob size */
	ctx->rows = be16_to_cpu(hdr.rows);
	ctx->cols = be16_to_cpu(hdr.cols);

	remain = ctx->rows * ctx->cols + sizeof(struct mitsu9550_plane);
	remain *= 3;
	remain += sizeof(struct mitsu9550_hdr2) + sizeof(struct mitsu9550_hdr3)+ sizeof(struct mitsu9550_hdr4) + sizeof(struct mitsu9550_footer);

	/* Allocate buffer */
	ctx->databuf = malloc(remain + sizeof(struct mitsu9550_hdr1));
	if (!ctx->databuf) {
		ERROR("Memory allocation failure!\n");
		return CUPS_BACKEND_FAILED;
	}

	memcpy(ctx->databuf, &hdr, sizeof(struct mitsu9550_hdr1));
	ctx->datalen = sizeof(struct mitsu9550_hdr1);

	/* Read in the spool data */
	while(remain) {
		i = read(data_fd, ctx->databuf + ctx->datalen, remain);
		if (i == 0)
			return CUPS_BACKEND_CANCEL;
		if (i < 0)
			return CUPS_BACKEND_CANCEL;
		ctx->datalen += i;
		remain -= i;
	}

	return CUPS_BACKEND_OK;
}

static int mitsu9550_get_status(struct mitsu9550_ctx *ctx, struct mitsu9550_status *resp)
{
	uint8_t cmdbuf[CMDBUF_LEN];
	int num, ret;

	/* Send Printer Query */
	memset(cmdbuf, 0, CMDBUF_LEN);
	cmdbuf[0] = 0x1b;
	cmdbuf[1] = 0x56;
	cmdbuf[2] = 0x32;
	cmdbuf[3] = 0x30;
	if ((ret = send_data(ctx->dev, ctx->endp_down,
			     cmdbuf, 4)))
		return ret;
	memset(resp, 0, sizeof(*resp));
	ret = read_data(ctx->dev, ctx->endp_up,
			(uint8_t*) resp, sizeof(*resp), &num);

	if (ret < 0)
		return ret;
	if (num != sizeof(*resp)) {
		ERROR("Short Read! (%d/%d)\n", num, (int)sizeof(*resp));
		return 4;
	}

	return 0;
}

static int mitsu9550_main_loop(void *vctx, int copies) {
	struct mitsu9550_ctx *ctx = vctx;

	int ret;

	if (!ctx)
		return CUPS_BACKEND_FAILED;

top:

	// query state, start streaming over chunks...?
	// blablabla

#if 0
        /* This printer handles copies internally */
	copies = 1;
#endif

	/* Clean up */
	if (terminate)
		copies = 1;

	INFO("Print complete (%d copies remaining)\n", copies - 1);

	if (copies && --copies) {
		goto top;
	}

	return CUPS_BACKEND_OK;
}

static void mitsu9550_dump_status(struct mitsu9550_status *resp)
{
	if (dyesub_debug) {
#if 0
		uint8_t *ptr;
		unsigned int i;

		DEBUG("Status Dump:\n");
		for (i = 0 ; i < sizeof(resp->unk) ; i++) {
			DEBUG2("%02x ", resp->unk[i]);
		}
		DEBUG2("\n");
		DEBUG("Lower Deck:\n");
		ptr = (uint8_t*) &resp->lower;
		for (i = 0 ; i < sizeof(resp->lower) ; i++) {
			DEBUG2("%02x ", *ptr++);
		}
		DEBUG2("\n");
		ptr = (uint8_t*) &resp->upper;
		DEBUG("Upper Deck:\n");
		for (i = 0 ; i < sizeof(resp->upper) ; i++) {
			DEBUG2("%02x ", *ptr++);
		}
		DEBUG2("\n");
#endif
	}
#if 0
	if (resp->upper.present & 0x80) {  /* Not present */
		INFO("Prints remaining:  %d\n",
		     be16_to_cpu(resp->lower.remain));
	} else {
		INFO("Prints remaining:  Lower: %d Upper: %d\n",
		     be16_to_cpu(resp->lower.remain),
		     be16_to_cpu(resp->upper.remain));
	}
#endif
}

static int mitsu9550_query_status(struct mitsu9550_ctx *ctx)
{
	struct mitsu9550_status resp;
	int ret;

	ret = mitsu9550_get_status(ctx, &resp);

	if (!ret)
		mitsu9550_dump_status(&resp);

	return ret;
}

static void mitsu9550_cmdline(void)
{
	DEBUG("\t\t[ -s ]           # Query status\n");
}

static int mitsu9550_cmdline_arg(void *vctx, int argc, char **argv)
{
	struct mitsu9550_ctx *ctx = vctx;
	int i, j = 0;

	/* Reset arg parsing */
	optind = 1;
	opterr = 0;
	while ((i = getopt(argc, argv, "s")) >= 0) {
		switch(i) {
 		case 's':
			if (ctx) {
				j = mitsu9550_query_status(ctx);
				break;
			}
			return 1;
		default:
			break;  /* Ignore completely */
		}

		if (j) return j;
	}

	return 0;
}

/* Exported */
struct dyesub_backend mitsu9550_backend = {
	.name = "Mitsubishi CP-9550DW-S ***WIP***",
	.version = "-0.001WIP",
	.uri_prefix = "mitsu9550",
	.cmdline_usage = mitsu9550_cmdline,
	.cmdline_arg = mitsu9550_cmdline_arg,
	.init = mitsu9550_init,
	.attach = mitsu9550_attach,
	.teardown = mitsu9550_teardown,
	.read_parse = mitsu9550_read_parse,
	.main_loop = mitsu9550_main_loop,
	.devices = {
	{ USB_VID_MITSU, USB_PID_MITSU_9550DZ, P_MITSU_9550, ""},
	{ USB_VID_MITSU, USB_PID_MITSU_9550D, P_MITSU_9550, ""},
	{ 0, 0, 0, ""}
	}
};

/* Mitsubish CP-9550D/DW spool data format 

   Spool file consists of four 50-byte headers, followed by three image
   planes (BGR, each with a 12-byte header), and a 4-byte footer.

   All multi-byte numbers are big endian.

   ~~~ Printer Init: 4x 50-byte blocks:

   1b 57 20 2e 00 0a 10 00  00 00 00 00 00 00 07 14 :: 0714 = 1812 = X res
   04 d8 00 00 00 00 00 00  00 00 00 00 00 00 00 00 :: 04d8 = 1240 = Y res
   00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
   00 00 

   1b 57 21 2e 00 80 00 22  08 03 00 00 00 00 00 00
   00 00 00 00 00 00 00 00  00 00 00 00 00 01 00 00 :: YY 00 = normal, 80 = Fine
   XX 00 00 00 00 00 YY 00  00 00 00 00 00 00 00 00 :: XX 00 = normal, 83 = Cut 2x6
   00 01 

   1b 57 22 2e 00 40 00 00  00 00 00 XX 00 00 00 00 :: 00 = normal, 01 = FineDeep
   00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00 
   00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00 
   00 00 

   1b 57 26 2e 00 70 00 00  00 00 00 00 01 01 00 00 
   00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00 
   00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00 
   00 00  

  ~~~~ Data follows:   Appears to be RGB (or BGR?)

   1b 5a 54 00 00 00 00 00  07 14 04 d8  :: 0714 == row len, 04d8 == rows
                     ^^ ^^               :: 0000 == remaining rows

   Data follows immediately, no padding.

   1b 5a 54 00 00 00 00 00  07 14 04 d8  :: Another plane.

   Data follows immediately, no padding.

   1b 5a 54 00 00 00 00 00  07 14 04 d8  :: Another plane.

   Data follows immediately, no padding.

  ~~~~ Footer:

   1b 50 46 00


  ~~~~ QUESTIONS:

   * Lamination?
   * Other multi-cut modes (6x9 media, 4x6*2, 4.4x6*2, 3x6*3, 2x6*4)
   * Printer-generated copies (the "first 01" in hdr2?)

 */
