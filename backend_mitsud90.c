/*
 *   Mitsubishi CP-D90DW Photo Printer CUPS backend
 *
 *   (c) 2018 Solomon Peachy <pizza@shaftnet.org>
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
 *   SPDX-License-Identifier: GPL-3.0+
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

#define BACKEND mitsud90_backend

#include "backend_common.h"

#define USB_VID_MITSU       0x06D3
#define USB_PID_MITSU_D90   0x3B60

const char *mitsu70x_media_types(uint8_t brand, uint8_t type);

/* Printer data structures */
struct mitsud90_media_resp {
	uint8_t  hdr[4];  /* e4 47 44 30 */
	uint8_t  media_brand;
	uint8_t  media_type;
	uint8_t  unk_a[2];
	uint16_t media_capacity; /* BE */
	uint16_t media_remain;  /* BE */
	uint8_t  unk_b[2];
};

struct mitsud90_status_resp {
	uint8_t  hdr[4];  /* e4 47 44 30 */
	uint8_t  unk[11];
};

struct mitsud90_job_hdr {
	uint8_t  hdr[6]; /* 1b 53 50 30 00 33 */
	uint16_t cols;   /* BE */
	uint16_t rows;   /* BE */
	uint8_t  unk[5]; /* 64 00 00 01 00 */
	uint8_t  mcut;   /* 0x01 for 8x6div2 */

	union {
		struct {
			uint16_t pos;
			uint16_t flag;
		} cuts[2];
		uint8_t cutzero[6];
	};
	uint8_t  zero[26];

	uint8_t  overcoat;
	uint8_t  quality;
	uint8_t  sharp_h;
	uint8_t  sharp_v;
	uint8_t  zero_b[5];
	union {
		struct {
			uint16_t pano_on; /* 0x0001 (BE) when pano is on,  */
			uint8_t  pano_tot;  /* 2 or 3 */
			uint8_t  pano_pg;   /* 1, 2, 3 */
			uint16_t pano_rows; /* always 0x097c (BE), ie 2428 ie 8" print */
			uint16_t pano_rows2; /* Always 0x30 less than pano_rows */
			uint16_t pano_zero; /* 0x0000 */
			uint8_t  pano_unk[6];  /* 02 58 00 0c 00 06 */
		} pano;
		uint8_t zero_c[16];
	};
	uint8_t zero_d[6];
	uint8_t zero_fill[432];
};

struct mitsud90_plane_hdr {
	uint8_t  hdr[10]; /* 1b 5a 54 01 00 09 00 00 00 00 */
	uint16_t cols;  /* BE */
	uint16_t rows;  /* BE */
	uint8_t  zero_fill[498];
};

struct mitsud90_job_footer {
	uint8_t hdr[4]; /* 1b 42 51 31 */
	uint16_t seconds; /* 0x0005 by default (windows) BE */
};

struct mitsud90_memcheck {
	uint8_t  hdr[4]; /* 1b 47 44 33 */
	uint8_t  unk[2]; /* 00 33 */
	uint16_t cols;   /* BE */
	uint16_t rows;   /* BE */
	uint8_t  unk_b[4]; /* 64 00 00 01  */
	uint8_t  zero_fill[498];
};

struct mitsud90_memcheck_resp {
	uint8_t  hdr[4];   /* e4 47 44 43 */
	uint8_t  size_bad; /* 0x00 is ok */
	uint8_t  mem_bad;  /* 0x00 is ok */
};

/* Private data structure */
struct mitsud90_ctx {
	struct libusb_device_handle *dev;
	uint8_t endp_up;
	uint8_t endp_down;

	int type;

	uint8_t *databuf;
	int datalen;

	struct marker marker;
};

int mitsud90_query_media(struct mitsud90_ctx *ctx, struct mitsud90_media_resp *resp)
{
	uint8_t cmdbuf[8];
	int ret, num;

	cmdbuf[0] = 0x1b;
	cmdbuf[1] = 0x47;
	cmdbuf[2] = 0x44;
	cmdbuf[3] = 0x30;
	cmdbuf[4] = 0;
	cmdbuf[5] = 0;
	cmdbuf[6] = 0x01;  /* Number of commands */
	cmdbuf[7] = 0x2a;  /* Query Media commmand */

	if ((ret = send_data(ctx->dev, ctx->endp_down,
			     cmdbuf, sizeof(cmdbuf))))
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

	return CUPS_BACKEND_OK;
}

int mitsud90_query_status(struct mitsud90_ctx *ctx, struct mitsud90_status_resp *resp)
{
	uint8_t cmdbuf[8];
	int ret, num;

	cmdbuf[0] = 0x1b;
	cmdbuf[1] = 0x47;
	cmdbuf[2] = 0x44;
	cmdbuf[3] = 0x30;
	cmdbuf[4] = 0;
	cmdbuf[5] = 0;
	cmdbuf[6] = 0x01;  /* Number of commands */
	cmdbuf[7] = 0x16;  /* Query status commmand */

	if ((ret = send_data(ctx->dev, ctx->endp_down,
			     cmdbuf, sizeof(cmdbuf))))
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

	return CUPS_BACKEND_OK;
}

/* Generic functions */

static void *mitsud90_init(void)
{
	struct mitsud90_ctx *ctx = malloc(sizeof(struct mitsud90_ctx));
	if (!ctx) {
		ERROR("Memory Allocation Failure!\n");
		return NULL;
	}
	memset(ctx, 0, sizeof(struct mitsud90_ctx));

	return ctx;
}

static int mitsud90_attach(void *vctx, struct libusb_device_handle *dev, int type,
			    uint8_t endp_up, uint8_t endp_down, uint8_t jobid)
{
	struct mitsud90_ctx *ctx = vctx;
	struct mitsud90_media_resp resp;

	UNUSED(jobid);

	ctx->dev = dev;
	ctx->endp_up = endp_up;
	ctx->endp_down = endp_down;
	ctx->type = type;

	if (test_mode < TEST_MODE_NOATTACH) {
		if (mitsud90_query_media(ctx, &resp))
			return CUPS_BACKEND_FAILED;
	} else {
		resp.media_brand = 0xff;
		resp.media_type = 0x0f;
		resp.media_capacity = cpu_to_be16(230);
		resp.media_remain = cpu_to_be16(200);
	}

	ctx->marker.color = "#00FFFF#FF00FF#FFFF00";
	ctx->marker.name = mitsu70x_media_types(resp.media_brand, resp.media_type);
	ctx->marker.levelmax = be16_to_cpu(resp.media_capacity);
	ctx->marker.levelnow = be16_to_cpu(resp.media_remain);

	return CUPS_BACKEND_OK;
}

static void mitsud90_teardown(void *vctx) {
	struct mitsud90_ctx *ctx = vctx;

	if (!ctx)
		return;

	if (ctx->databuf) {
		free(ctx->databuf);
		ctx->databuf = NULL;
	}

	free(ctx);
}

static int mitsud90_read_parse(void *vctx, int data_fd) {
	struct mitsud90_ctx *ctx = vctx;
	int i, remain;
	struct mitsud90_job_hdr *hdr;

	if (!ctx)
		return CUPS_BACKEND_FAILED;

	if (ctx->databuf) {
		free(ctx->databuf);
		ctx->databuf = NULL;
	}

	/* Just allocate a worst-case buffer */
	ctx->datalen = 0;
	ctx->databuf = malloc(sizeof(struct mitsud90_job_hdr) +
			      sizeof(struct mitsud90_plane_hdr) +
			      sizeof(struct mitsud90_job_footer) +
			      1852*2729*3);

	if (!ctx->databuf) {
		ERROR("Memory allocation failure!\n");
		return CUPS_BACKEND_RETRY_CURRENT;
	}

	/* Read in first header */
	remain = sizeof(struct mitsud90_job_hdr);
	while (remain) {
		i = read(data_fd, (ctx->databuf + ctx->datalen), remain);
		if (i == 0)
			return CUPS_BACKEND_CANCEL;
		if (i < 0)
			return CUPS_BACKEND_CANCEL;
		remain -= i;
		ctx->datalen += i;
	}

	/* Sanity check header */
	hdr = (struct mitsud90_job_hdr *) ctx->databuf;
	if (hdr->hdr[0] != 0x1b ||
	    hdr->hdr[1] != 0x53 ||
	    hdr->hdr[2] != 0x50 ||
	    hdr->hdr[3] != 0x30 ) {
		ERROR("Unrecognized data format!\n");
		return CUPS_BACKEND_CANCEL;
	}

	/* Now read in the rest */
	remain = sizeof(struct mitsud90_plane_hdr) + be16_to_cpu(hdr->cols) * be16_to_cpu(hdr->rows) * 3 + sizeof(struct mitsud90_job_footer);
	while(remain) {
		i = read(data_fd, ctx->databuf + ctx->datalen, remain);
		if (i == 0)
			return CUPS_BACKEND_CANCEL;
		if (i < 0)
			return CUPS_BACKEND_CANCEL;
		ctx->datalen += i;
		remain -= i;
	}

	/* Sanity check */
	if (hdr->pano.pano_on) {
		ERROR("Unable to handle panorama jobs yet\n");
		return CUPS_BACKEND_CANCEL;
	}

	return CUPS_BACKEND_OK;
}

static int mitsud90_main_loop(void *vctx, int copies) {
	struct mitsud90_ctx *ctx = vctx;
	struct mitsud90_job_hdr *hdr;
	struct mitsud90_status_resp resp;

	int sent;
	int ret;

	if (!ctx)
		return CUPS_BACKEND_FAILED;

	hdr = (struct mitsud90_job_hdr*) ctx->databuf;

	INFO("Waiting for printer idle...\n");

top:
	sent = 0;

	/* Query status, wait for idle or error out */
	do {
		if (mitsud90_query_status(ctx, &resp))
			return CUPS_BACKEND_FAILED;
		// XXX TODO:  parse status so we can break!
		break; // XXX
	} while(1);


	/* Send memory check */
	{
		struct mitsud90_memcheck mem;
		struct mitsud90_memcheck_resp mem_resp;
		int num;

		memcpy(hdr, &mem, sizeof(mem));
		mem.hdr[0] = 0x1b;
		mem.hdr[1] = 0x47;
		mem.hdr[2] = 0x44;
		mem.hdr[3] = 0x33;
		mem.unk[0] = 0x00;
		mem.unk[1] = 0x33;

		if ((ret = send_data(ctx->dev, ctx->endp_down,
				     (uint8_t*) &mem, sizeof(mem))))
			return CUPS_BACKEND_FAILED;

		ret = read_data(ctx->dev, ctx->endp_up,
				(uint8_t*)&mem_resp, sizeof(mem_resp), &num);

		if (ret < 0)
			return ret;
		if (num != sizeof(mem_resp)) {
			ERROR("Short Read! (%d/%d)\n", num, (int)sizeof(mem_resp));
			return 4;
		}
		if (mem_resp.size_bad || mem_resp.mem_bad == 0xff) {
			ERROR("Printer reported bad print params (%02x)\n", mem_resp.size_bad);
			return CUPS_BACKEND_CANCEL;
		}
		if (mem_resp.mem_bad) {
			ERROR("Printer buffers full, retrying!\n");
			sleep(1);
			goto top;
		}
	}

	/* Send header */
	if ((ret = send_data(ctx->dev, ctx->endp_down,
			     ctx->databuf + sent, sizeof(*hdr))))
		return CUPS_BACKEND_FAILED;
	sent += sizeof(*hdr);

	/* Send Plane header */
	if ((ret = send_data(ctx->dev, ctx->endp_down,
			     ctx->databuf + sent, sizeof(*hdr))))
		return CUPS_BACKEND_FAILED;
	sent += sizeof(*hdr);

	/* Send payload + footer */
	if ((ret = send_data(ctx->dev, ctx->endp_down,
			     ctx->databuf + sent, ctx->datalen - sent)))
		return CUPS_BACKEND_FAILED;
	sent += (ctx->datalen - sent);

	/* Wait for completion */
	do {
		sleep(1);

		if (mitsud90_query_status(ctx, &resp))
			return CUPS_BACKEND_FAILED;

		// XXX TODO:  parse status so we can break!
		break; // XXX

		if (fast_return && copies <= 1) { /* Copies generated by backend? */
			INFO("Fast return mode enabled.\n");
			break;
		}
	} while(1);

	/* Clean up */
	if (terminate)
		copies = 1;

	INFO("Print complete (%d copies remaining)\n", copies - 1);

	if (copies && --copies) {
		goto top;
	}

	return CUPS_BACKEND_OK;
}

static int mitsud90_dump_media(struct mitsud90_ctx *ctx)
{
	struct mitsud90_media_resp resp;

	if (mitsud90_query_media(ctx, &resp))
		return CUPS_BACKEND_FAILED;

	INFO("Media Type:  %s (%02x/%02x)\n",
	     mitsu70x_media_types(resp.media_brand, resp.media_type),
	     resp.media_brand,
	     resp.media_type);
	INFO("Prints Remaining:  %03d/%03d\n",
	     be16_to_cpu(resp.media_remain),
	     be16_to_cpu(resp.media_capacity));

	return CUPS_BACKEND_OK;
}

static int mitsud90_dump_status(struct mitsud90_ctx *ctx)
{
	struct mitsud90_status_resp resp;

	if (mitsud90_query_status(ctx, &resp))
		return CUPS_BACKEND_FAILED;

	INFO("Status: %02x %02x %02x %02x  %02x %02x %02x %02x  %02x %02x %02x\n",
	     resp.unk[0], resp.unk[1], resp.unk[2], resp.unk[3],
	     resp.unk[4], resp.unk[5], resp.unk[6], resp.unk[7],
	     resp.unk[8], resp.unk[9], resp.unk[10]);

	return CUPS_BACKEND_OK;
}

static void mitsud90_cmdline(void)
{
	DEBUG("\t\t[ -m ]           # Query printer media\n");
	DEBUG("\t\t[ -s ]           # Query printer status\n");
}

static int mitsud90_cmdline_arg(void *vctx, int argc, char **argv)
{
	struct mitsud90_ctx *ctx = vctx;
	int i, j = 0;

	if (!ctx)
		return -1;

	while ((i = getopt(argc, argv, GETOPT_LIST_GLOBAL "ms")) >= 0) {
		switch(i) {
		GETOPT_PROCESS_GLOBAL
		case 'm':
			j = mitsud90_dump_media(ctx);
			break;
		case '2':
			j = mitsud90_dump_status(ctx);
			break;
		default:
			break;  /* Ignore completely */
		}

		if (j) return j;
	}

	return 0;
}

static int mitsud90_query_markers(void *vctx, struct marker **markers, int *count)
{
	struct mitsud90_ctx *ctx = vctx;
	struct mitsud90_media_resp resp;

	*markers = &ctx->marker;
	*count = 1;

	if (mitsud90_query_media(ctx, &resp))
		return CUPS_BACKEND_FAILED;

	ctx->marker.levelnow = be16_to_cpu(resp.media_remain);

	return CUPS_BACKEND_OK;
}

static const char *mitsud90_prefixes[] = {
	"mitsud90",
	NULL
};

/* Exported */
struct dyesub_backend mitsud90_backend = {
	.name = "Mitsubishi CP-D90DW",
	.version = "0.01WIP",
	.uri_prefixes = mitsud90_prefixes,
	.cmdline_arg = mitsud90_cmdline_arg,
	.cmdline_usage = mitsud90_cmdline,
	.init = mitsud90_init,
	.attach = mitsud90_attach,
	.teardown = mitsud90_teardown,
	.read_parse = mitsud90_read_parse,
	.main_loop = mitsud90_main_loop,
	.query_markers = mitsud90_query_markers,
	.devices = {
		{ USB_VID_MITSU, USB_PID_MITSU_D90, P_MITSU_D90, NULL, "mitsud90"},
		{ 0, 0, 0, NULL, NULL}
	}
};


/*
   Mitsubishi CP-D90DW data format

   All multi-byte values are BIG endian

 [[HEADER 1]]

   1b 53 50 30 00 33 XX XX  YY YY 64 00 00 01 00 ZZ  XX XX == COLS, YY XX ROWS (BE), ZZ == 01 for 8x6div2
   ?? ?? ?? ?? ?? ?? 00 00  00 00 00 00 00 00 00 00  <-- cut position, see below
   00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
   QQ RR SS HH VV 00 00 00  00 00 01 00 03 II 09 7c  QQ == 02 matte, 00 glossy,
   09 4c 00 00 02 58 00 0c  00 06                    RR == 00 auto, 03 == fine, 02 == superfine.
                                                  SS == 00 colorcorr, 01 == none
                                                  HH/VV sharpening for Horiz/Vert, 0-8, 0 is off, 4 is normal
  [pad to 512b]

                normal  == 00 00 00 00 00 00
                4x6div2 == 02 65 01 00 00 01
                8x6div2 == 04 be 00 00 00 00  <-- suspect XX XX ?? XX XX ??  where XX XX is the row of a cut.

   from [01 00 03 03] onwards, only shows in 8x20" PANORAMA prints.  Assume 2" overlap.
    II == 01 02 03 (which panel # in panorama!)
    [02 58] == 600
    [09 4c] == 2380  (??)
    [09 7c] == 2428  (ie 8" print)

     (6x20 == 1852x6036)
     (6x14 == 1852x4232)

     3*8" panels == 2428*3=7284.  -6036 = 1248.  /3 = 416 (0x1a0)

 [[DATA PLANE HEADER]]

   1b 5a 54 01 00 09 00 00  00 00 XX XX YY YY 00 00
   ...
   [pad to 512b]

    data, BGR packed, 8bpp.  No padding to 512b!

 [[FOOTER]]

   1b 42 51 31 00 TT                  ## TT == secs to wait for second print


 ****************************************************

Comms Protocol for D90:

 [[ STATUS QUERIES ]]

-> 1b 47 44 30 00 00 01 16
<- e4 47 44 30 00 00 00 00  00 00 00 00 00 00 00

-> 1b 47 44 30 00 00 01 2a
<- e4 47 44 30 ff 0f 50 00  01 ae 01 9b 01 00

  [[ looks like 0x16 and 0x2a return different parameters
     that can stack.  0x2a is MEDIA STATUS/TYPE/REMAIN ]]

-> 1b 47 44 30 00 00 02 16  2a
<- e4 47 44 30 00 00 00 00  00 00 00 00 00 00 00 VV
   TT ?? 00 XX XX YY YY 01  00

   ??    == 0x50 or 0x00 (seen, no idea what it means)
   VV    == Media vendor (0xff etc)
   TT    == Media type, 0x02/0x0f etc (see mitsu70x_media_types!)
   XX XX == Media total, BE (0x190 == 400)
   YY YY == Media remain, BE (0x119 == 281)

 [[ SANITY CHECK PRINT ARGUMENTS? ]]

-> 1b 47 44 33 00 33 07 3c  04 ca 64 00 00 01 00 00
   00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
   00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
   00 00 00 04 04 00 00 00  00 00 00 00 00 00 00 00
   [[ pad to 512 ]]

   ... 07 3c onwards is the same as main payload header.

<- e4 47 44 43 XX YY

   ... possibly the same as the D70's "memorystatus"
       XX == size ok (non-zero if bad size)
       YY == memory ok (non-zero or 0xff if full?)

 [[ SEND OVER HDRs and DATA ]]

   ... Print arguments:

-> 1b 53 50 30 00 33 07 3c  04 ca 64 00 00 01 00 00
   00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
   00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
   00 00 00 04 04 00 00 00  00 00 00 00 00 00 00 00
   [[ pad to 512 ]]

   ... Data transfer.  Plane header:

-> 1b 5a 54 01 00 09 00 00  00 00 07 3c 04 ca 00 00
   [[ pad to 512 ]]

-> [[print data]] [[ padded? ]]
-> [[print data]]

-> 1b 42 51 31 00 ZZ

   ... Footer.
   ZZ == Seconds to wait for follow-up print (0x05)


 */
