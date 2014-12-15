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
#define USB_PID_MITSU_9550DS 0x03A5  // or DZ/DZS/DZU

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
	uint8_t  cmd[4]; /* 1b 57 21 2e */
	uint8_t  unk[24];
	uint16_t copies; /* BE, 1-580 */
	uint8_t  null[2];
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

struct mitsu9550_cmd {
	uint8_t cmd[4];
} __attribute__((packed));

/* Printer data structures */
struct mitsu9550_media {
	uint8_t  hdr[2];  /* 24 2e */
	uint8_t  unk[12];
	uint8_t  type;
	uint8_t  unka[13];
	uint16_t max;  /* BE, prints per media */
	uint8_t  unkb[2];
	uint8_t  remain; /* BE, prints remaining */
	uint8_t  unkc[14];
} __attribute__((packed));

struct mitsu9550_status {
	uint8_t  hdr[2]; /* 30 2e */
	uint8_t  null[4];
	uint8_t  sts1;
	uint8_t  nullb[2];
	uint8_t  sts2;
	uint8_t  nullc[6];
	uint8_t  sts3;
	uint8_t  sts4;
	uint8_t  sts5;
	uint8_t  nulld[25];
	uint8_t  sts6;
	uint8_t  sts7;
	uint8_t  nulle[2];
} __attribute__((packed));

struct mitsu9550_status2 {
	uint8_t  hdr[2]; /* 21 2e */
	uint8_t  unk[40];
	uint8_t  remain; /* BE, prints remaining */
	uint8_t  unkb[4];
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
	remain += sizeof(struct mitsu9550_hdr2) + sizeof(struct mitsu9550_hdr3)+ sizeof(struct mitsu9550_hdr4) + sizeof(struct mitsu9550_cmd);

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

static int mitsu9550_get_media(struct mitsu9550_ctx *ctx, struct mitsu9550_media *resp)
{
	uint8_t cmdbuf[CMDBUF_LEN];
	int num, ret;

	/* Send Printer Query */
	memset(cmdbuf, 0, CMDBUF_LEN);
	cmdbuf[0] = 0x1b;
	cmdbuf[1] = 0x56;
	cmdbuf[2] = 0x24;
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
	struct mitsu9550_hdr2 *hdr2;
	
	int ret;

	if (!ctx)
		return CUPS_BACKEND_FAILED;
	
	/* This printer handles copies internally */
	hdr2 = (struct mitsu9550_hdr2 *) (ctx->databuf + sizeof(struct mitsu9550_hdr1));
	hdr2->copies = cpu_to_be16(copies);
	
top:

	// query state, start streaming over chunks...?
	// blablabla

        /* This printer handles copies internally */
	copies = 1;

	/* Clean up */
	if (terminate)
		copies = 1;

	INFO("Print complete (%d copies remaining)\n", copies - 1);

	if (copies && --copies) {
		goto top;
	}

	return CUPS_BACKEND_OK;
}

static char *mitsu9550_media_types(uint8_t type)
{
	switch (type) {
	case 0x01:
		return "3.5x5";
	case 0x02:
		return "4x6";
	case 0x03:
		return "PC";
	case 0x04:
		return "5x7";
	case 0x05:
		return "6x9";
	case 0x06:
		return "V";
	default:
		return "Unknown";
	}
	return NULL;
}

static void mitsu9550_dump_media(struct mitsu9550_media *resp)
{
	INFO("Media Type       : %02x (%s)\n",
	     resp->type, mitsu9550_media_types(resp->type));
	INFO("Prints remaining : %03d/%03d\n",
	     be16_to_cpu(resp->remain), be16_to_cpu(resp->max));
}

static int mitsu9550_query_media(struct mitsu9550_ctx *ctx)
{
	struct mitsu9550_media resp;
	int ret;

	ret = mitsu9550_get_media(ctx, &resp);

	if (!ret)
		mitsu9550_dump_media(&resp);

	return ret;
}

static int mitsu9550_query_serno(struct libusb_device_handle *dev, uint8_t endp_up, uint8_t endp_down, char *buf, int buf_len)
{
	struct mitsu9550_cmd cmd;
	uint8_t rdbuf[READBACK_LEN];
	int ret, num, i;

	cmd.cmd[0] = 0x1b;
	cmd.cmd[1] = 0x72;
	cmd.cmd[2] = 0x6e;
	cmd.cmd[3] = 0x00;

	if ((ret = send_data(dev, endp_down,
                             (uint8_t*) &cmd, sizeof(cmd))))
                return (ret < 0) ? ret : -99;

	ret = read_data(dev, endp_up,
			rdbuf, READBACK_LEN, &num);
	
	if (ret < 0)
		return ret;

	if ((unsigned int)num < sizeof(cmd)) /* Short read */
		return -1;
	
	if (rdbuf[0] != 0xe4 ||
	    rdbuf[1] != 0x72 ||
	    rdbuf[2] != 0x6e ||
	    rdbuf[3] != 0x00) /* Bad response */
		return -2;

	/* If response is truncated, handle it */
	if ((unsigned int) num < sizeof(cmd) + rdbuf[4] + 1)
		rdbuf[4] = num - sizeof(cmd) - 1;

	/* model and serial number are encoded as 16-bit unicode, 
	   little endian */
	for (i = 5 ; i < rdbuf[4] ; i+= 2) {
		if (rdbuf[i] != 0x20)
			continue;
		if (--buf_len)
			break;
		*buf++ = rdbuf[i];
	}
	*buf = 0; /* Null-terminate the returned string */
	
	return ret;
}

static void mitsu9550_cmdline(void)
{
	DEBUG("\t\t[ -m ]           # Query media\n");
}

static int mitsu9550_cmdline_arg(void *vctx, int argc, char **argv)
{
	struct mitsu9550_ctx *ctx = vctx;
	int i, j = 0;

	/* Reset arg parsing */
	optind = 1;
	opterr = 0;
	while ((i = getopt(argc, argv, "m")) >= 0) {
		switch(i) {
 		case 's':
			if (ctx) {
				j = mitsu9550_query_media(ctx);
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
	.query_serno = mitsu9550_query_serno,
	.devices = {
	{ USB_VID_MITSU, USB_PID_MITSU_9550D, P_MITSU_9550, ""},
	{ USB_VID_MITSU, USB_PID_MITSU_9550DS, P_MITSU_9550, ""},
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

   1b 57 21 2e 00 80 00 22  08 03 00 00 00 00 00 00 :: ZZ = num copies (>= 0x01)
   00 00 00 00 00 00 00 00  00 00 00 00 ZZ ZZ 00 00 :: YY 00 = normal, 80 = Fine
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

  ~~~~ Data follows:   Data is 8-bit BGR.

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

   * Lamination control?
   * Other multi-cut modes (on 6x9 media: 4x6*2, 4.4x6*2, 3x6*3, 2x6*4)

 ***********************************************************************

 * Mitsubishi ** CP-9550DW-S ** Communications Protocol:

  [[ Unknown ]]

 -> 1b 53 c5 9d

  [[ Unknown ]]

 -> 1b 4b 7f 00
 <- eb 4b 8f 00 02 00 5e  [[ '02' seems to be a length ]] 

  [[ Unknown ]]

 -> 1b 53 00 00

  Query Model & Serial number

 -> 1b 72 6e 00
 <- e4 82 6e 00 LL 39 00 35  00 35 00 30 00 5a 00 20
    00 41 00 32 00 30 00 30  00 36 00 37 00

     'LL' is length.  Data is returned in 16-bit unicode, LE.
     Contents are model ('9550Z'), then space, then serialnum ('A20067')

  Media Query

 -> 1b 56 24 00
 <- 24 2e 00 00 00 00 00 00  00 00 00 00 00 00 TT 00 :: TT = Type
    00 00 00 00 00 00 00 00  00 00 00 00 MM MM 00 00 :: MM MM = Max prints
    NN NN 00 00 00 00 00 00  00 00 00 00 00 00 00 00 :: NN NN = Remaining

  Status Query
 
 -> 1b 56 30 00
 -> 30 2e 00 00 00 00 MM 00  00 NN 00 00 00 00 00 00 :: MM, NN
    QQ RR SS 00 00 00 00 00  00 00 00 00 00 00 00 00 :: QQ, RR, SS
    00 00 00 00 00 00 00 00  00 00 00 00 TT UU 00 00 :: TT, UU 

  Status Query B (not sure what to call this)

 -> 1b 56 21 00
 <- 21 2e 00 80 00 22 a8 0b  00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00  00 00 00 QQ 00 00 00 00 :: QQ == Prints in job?
    00 00 00 00 00 00 00 00  00 00 NN NN 0A 00 00 01 :: NN NN = Remaining media

  [[ Unknown ]]

 -> 1b 44

  [[ Header 1 -- See above ]]

 -> 1b 57 20 2e ....

  [[ Header 2 -- See above ]]

 -> 1b 57 21 2e ....

  [[ Header 3 -- See above ]]

 -> 1b 57 22 2e ....

  [[ Unknown -- Start Data ? ]]

 -> 1b 5a 43 00

  [[ Plane header #1 (Blue) ]]

 -> 1b 5a 54 00 00 00 00 00  XX XX YY YY :: XX XX == Columns, YY YY == Rows

    Followed by image plane #1 (Blue), XXXX * YYYY bytes

  [[ Plane header #2 (Green) ]]

 -> 1b 5a 54 00 00 00 00 00  XX XX YY YY :: XX XX == Columns, YY YY == Rows

    Followed by image plane #2 (Green), XXXX * YYYY bytes

  [[ Plane header #3 (Red) ]]

 -> 1b 5a 54 00 00 00 00 00  XX XX YY YY :: XX XX == Columns, YY YY == Rows

    Followed by image plane #3 (Red), XXXX * YYYY bytes

  [[ Unknown -- End Data aka START print? ]]

 -> 1b 50 47 00

  [[ At this point, loop status/status b/media queries until printer idle ]]

    MM, NN, QQ RR SS, TT UU

 <- 00  00  3e 00 00  8a 44  :: Idle.
    00  00  7e 00 00  8a 44  :: Plane data submitted, pre "end data" cmd
    00  00  7e 40 01  8a 44  :: "end data" sent
    30  01  7e 40 01  8a 44
    38  01  7e 40 01  8a 44
    59  01  7e 40 01  8a 44
    59  01  7e 40 00  8a 44
    4d  01  7e 40 00  8a 44
     [...]
    43  01  7e 40 00  82 44
     [...]
    50  01  7e 40 00  80 44
     [...]
    31  01  7e 40 00  7d 44
     [...]
    00  00  3e 00 00  80 44

  Also seen: 

    00  00  3e 00 00  96 4b  :: Idle
    00  00  be 00 00  96 4b  :: Data submitted, pre "start"
    00  00  be 80 01  96 4b  :: print start sent
    30  00  be 80 01  96 4c
     [...]
    30  03  be 80 01  89 4b
    38  03  be 80 01  8a 4b
    59  03  be 80 01  8b 4b
     [...]
    4d  03  be 80 01  89 4b
     [...]
    43  03  be 80 01  89 4b
     [...]
    50  03  be 80 01  82 4b
     [...]
    31  03  be 80 01  80 4b
     [...]

 Working theory of interpreting the status flags:

  MM :: 00 is idle, else mechanical printer state.
  NN :: Remaining prints, or 0x00 when idle.
  QQ :: ?? 0x3e + 0x40 or 0x80 (see below)
  RR :: ?? 0x00 is idle, 0x40 or 0x80 is "printing"?
  SS :: ?? 0x00 means "ready for another print" but 0x01 is "busy"
  TT :: ?? seen values between 0x7c through 0x96)
  UU :: ?? seen values between 0x44 and 0x4c
 
  *** 

   Other printer commands seen:

  [[ Set error policy ?? aka "header 4" ]]

 -> 1b 57 26 2e 00 QQ 00 00  00 00 00 00 RR SS 00 00 :: QQ/RR 00 00 00
    00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00 ::       20 01 00
    00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00 ::       70 01 01
    00 00

 */
