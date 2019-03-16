/*
 *   Sony UP-DR150 Photo Printer CUPS backend -- libusb-1.0 version
 *
 *   (c) 2013-2018 Solomon Peachy <pizza@shaftnet.org>
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

#define BACKEND updr150_backend

#include "backend_common.h"

/* Printer status
  --> 1b e0 00 00 00 00 XX 00   [[ XX is 0xe on UPD895, 0xf on others ]]
  <-- this struct
*/
struct sony_updsts {
	uint8_t  hdr[2];   /* 0x0d 0x00 */
	uint8_t  prinitng; /* 0xe0 if printing, 0x00 otherwise */
	uint8_t  remain;   /* Number of remaining pages */
	uint8_t  zero1;
	uint8_t  sts1;     /* primary status */
	uint8_t  sts2;     /* seconday status */
	uint8_t  sts3;     /* tertiary status */
	uint8_t  zero2[2];
	uint16_t max_cols; /* BE */
	uint16_t max_rows; /* BE */
	uint8_t  percent;  /* 0-99, if job is printing */
} __attribute__((packed));


/* Private data structure */
struct updr150_printjob {
	uint8_t *databuf;
	int datalen;
	int copies;
};

struct updr150_ctx {
	struct libusb_device_handle *dev;
	uint8_t endp_up;
	uint8_t endp_down;
	int type;

	uint8_t stsbuf[16];

	struct marker marker;
};

static void* updr150_init(void)
{
	struct updr150_ctx *ctx = malloc(sizeof(struct updr150_ctx));
	if (!ctx) {
		ERROR("Memory Allocation Failure!");
		return NULL;
	}
	memset(ctx, 0, sizeof(struct updr150_ctx));
	return ctx;
}

static int updr150_attach(void *vctx, struct libusb_device_handle *dev, int type,
			  uint8_t endp_up, uint8_t endp_down, uint8_t jobid)
{
	struct updr150_ctx *ctx = vctx;

	UNUSED(jobid);

	ctx->dev = dev;
	ctx->endp_up = endp_up;
	ctx->endp_down = endp_down;
	ctx->type = type;

	if (ctx->type == P_SONY_UPD895 || ctx->type == P_SONY_UPD897)
		ctx->marker.color = "#000000";  /* Ie black! */
	else
		ctx->marker.color = "#00FFFF#FF00FF#FFFF00";

	ctx->marker.name = "Unknown";
	ctx->marker.levelmax = -1;
	ctx->marker.levelnow = -2;

	return CUPS_BACKEND_OK;
}

static void updr150_cleanup_job(const void *vjob)
{
	const struct updr150_printjob *job = vjob;

	if (job->databuf)
		free(job->databuf);

	free((void*)job);
}

static void updr150_teardown(void *vctx) {
	struct updr150_ctx *ctx = vctx;

	if (!ctx)
		return;

	free(ctx);
}

static char* upd895_statuses(uint8_t code)
{
	switch (code) {
	case 0x00:
		return "Idle";
	case 0x08:
		return "Door open";
	case 0x40:
		return "No paper";
	case 0x80:
		return "Idle";
	default:
		return "Unknown";
	}
}

static int sony_get_status(struct updr150_ctx *ctx)
{
	int ret, num = 0;
	uint8_t query[7] = { 0x1b, 0xe0, 0, 0, 0, 0x0e, 0 };

	if (ctx->type != P_SONY_UPD895)
		query[5] = 0x0f;

	if ((ret = send_data(ctx->dev, ctx->endp_down,
			     query, sizeof(query))))
		return CUPS_BACKEND_FAILED;

	ret = read_data(ctx->dev, ctx->endp_up, ctx->stsbuf, sizeof(ctx->stsbuf), &num);

	if (ret < 0)
		return CUPS_BACKEND_FAILED;
#if 0
	if (ctx->type == P_SONY_UPD895 && ret != 14)
		return CUPS_BACKEND_FAILED;
	else if (ret != 15)
		return CUPS_BACKEND_FAILED;
#endif

	return CUPS_BACKEND_OK;
}

#define MAX_PRINTJOB_LEN (2048*2764*3 + 2048)

static int updr150_read_parse(void *vctx, const void **vjob, int data_fd, int copies) {
	struct updr150_ctx *ctx = vctx;
	int len, run = 1;
	uint32_t copies_offset = 0;

	struct updr150_printjob *job = NULL;

	if (!ctx)
		return CUPS_BACKEND_FAILED;

	job = malloc(sizeof(*job));
	if (!job) {
		ERROR("Memory allocation failure!\n");
		return CUPS_BACKEND_RETRY_CURRENT;
	}
	memset(job, 0, sizeof(*job));
	job->copies = copies;

	job->datalen = 0;
	job->databuf = malloc(MAX_PRINTJOB_LEN);
	if (!job->databuf) {
		ERROR("Memory allocation failure!\n");
		updr150_cleanup_job(job);
		return CUPS_BACKEND_RETRY_CURRENT;
	}

	while(run) {
		int i;
		int keep = 0;
		i = read(data_fd, job->databuf + job->datalen, 4);
		if (i < 0) {
			updr150_cleanup_job(job);
			return CUPS_BACKEND_CANCEL;
		}
		if (i == 0)
			break;

		memcpy(&len, job->databuf + job->datalen, sizeof(len));
		len = le32_to_cpu(len);

		/* Filter out chunks we don't send to the printer */
		if (len & 0xf0000000) {
			switch (len) {
			case 0xfffffff3:
				if(dyesub_debug)
					DEBUG("Block ID '%08x' (len %d)\n", len, 0);
				len = 0;
				if (ctx->type == P_SONY_UPDR150)
					run = 0;
				break;
			case 0xfffffff7:
				if(dyesub_debug)
					DEBUG("Block ID '%08x' (len %d)\n", len, 0);
				len = 0;
				if (ctx->type == P_SONY_UPCR10)
					run = 0;
				break;
			case 0xfffffff8: // 895
			case 0xfffffff4: // 897
				if(dyesub_debug)
					DEBUG("Block ID '%08x' (len %d)\n", len, 0);
				len = 0;
				if (ctx->type == P_SONY_UPD895 || ctx->type == P_SONY_UPD897)
					run = 0;
				break;
			case 0xffffff97:
				if(dyesub_debug)
					DEBUG("Block ID '%08x' (len %d)\n", len, 12);
				len = 12;
				break;
			case 0xffffffef:
				if (ctx->type == P_SONY_UPD895 || ctx->type == P_SONY_UPD897) {
					if(dyesub_debug)
						DEBUG("Block ID '%08x' (len %d)\n", len, 0);
					len = 0;
					break;
				}
				/* Intentional Fallthrough */
			case 0xffffffeb:
			case 0xffffffee:
			case 0xfffffff5:
				if(dyesub_debug)
					DEBUG("Block ID '%08x' (len %d)\n", len, 4);
				len = 4;
				break;
			default:
				if(dyesub_debug)
					DEBUG("Block ID '%08x' (len %d)\n", len, 0);
				len = 0;
				break;
			}
		} else {
			/* Only keep these chunks */
			if(dyesub_debug)
				DEBUG("Data block (len %d)\n", len);
			if (len > 0)
				keep = 1;
		}
		if (keep)
			job->datalen += sizeof(uint32_t);

		/* Read in the data chunk */
		while(len > 0) {
			i = read(data_fd, job->databuf + job->datalen, len);
			if (i < 0) {
				updr150_cleanup_job(job);
				return CUPS_BACKEND_CANCEL;
			}
			if (i == 0)
				break;

			/* Work out offset of copies command */
			if (job->databuf[job->datalen] == 0x1b &&
			    job->databuf[job->datalen + 1] == 0xee) {
				if (i == 7)
					copies_offset = job->datalen + 11;
				else
					copies_offset = job->datalen + 7;
			}

			if (keep)
				job->datalen += i;
			len -= i;
		}
	}
	if (!job->datalen) {
		updr150_cleanup_job(job);
		return CUPS_BACKEND_CANCEL;
	}

	/* Some models specify copies in the print job */
	if (copies_offset) {
		uint16_t tmp = copies;
		tmp = cpu_to_be16(copies);
		memcpy(job->databuf + copies_offset, &tmp, sizeof(tmp));
		job->copies = 1;
	}

	*vjob = job;

	return CUPS_BACKEND_OK;
}

static int updr150_main_loop(void *vctx, const void *vjob) {
	struct updr150_ctx *ctx = vctx;
	int i, ret;
	int copies;

	const struct updr150_printjob *job = vjob;

	if (!ctx)
		return CUPS_BACKEND_FAILED;
	if (!job)
		return CUPS_BACKEND_FAILED;

	copies = job->copies;

top:
	/* Check for idle, if appropriate */
	if (ctx->type == P_SONY_UPD895 || ctx->type == P_SONY_UPD897) {
		ret = sony_get_status(ctx);

		if (ret)
			return ret;

		if (ctx->stsbuf[5] != 0x00) {
			if (ctx->stsbuf[5] == 0x80) {
				INFO("Waiting for printer idle...\n");
				sleep(1);
				goto top;
			}
		}
	}

	i = 0;
	while (i < job->datalen) {
		uint32_t len;
		memcpy(&len, job->databuf + i, sizeof(len));
		len = le32_to_cpu(len);

		i += sizeof(uint32_t);

		if ((ret = send_data(ctx->dev, ctx->endp_down,
				     job->databuf + i, len)))
			return CUPS_BACKEND_FAILED;

		i += len;
	}

	/* Check for idle, if appropriate */
	if (ctx->type == P_SONY_UPD895 || ctx->type == P_SONY_UPD897) {
	retry:
		sleep(1);

		ret = sony_get_status(ctx);
		if (ret)
			return ret;

		switch (ctx->stsbuf[5]) {
		case 0x00:
			goto done;
		case 0x80:
			break;
		default:
			ERROR("Printer error: %s (%02x)\n", upd895_statuses(ctx->stsbuf[5]),
			      ctx->stsbuf[5]);
			return CUPS_BACKEND_STOP;
		}

		if (fast_return && ctx->stsbuf[3] > 0) {
			INFO("Fast return mode enabled.\n");
		} else {
			goto retry;
		}
	}

	/* Clean up */
	if (terminate)
		copies = 1;

done:
	INFO("Print complete (%d copies remaining)\n", copies - 1);

	if (copies && --copies) {
		goto top;
	}

	return CUPS_BACKEND_OK;
}

static int upd895_dump_status(struct updr150_ctx *ctx)
{
	int ret = sony_get_status(ctx);
	if (ret < 0)
		return CUPS_BACKEND_FAILED;

	INFO("Printer status: %s (%02x)\n", upd895_statuses(ctx->stsbuf[5]), ctx->stsbuf[5]);
	if (ctx->stsbuf[2] == 0x0e0 && ctx->stsbuf[5] == 0x80)
		INFO("Remaining copies: %d\n", ctx->stsbuf[3]);

	return CUPS_BACKEND_OK;
}


static void updr150_cmdline(void)
{
	DEBUG("\t\t[ -s ]           # Query printer status (only UP-D895)\n");
}

static int updr150_cmdline_arg(void *vctx, int argc, char **argv)
{
	struct updr150_ctx *ctx = vctx;
	int i, j = 0;

	if (!ctx)
		return -1;

	while ((i = getopt(argc, argv, GETOPT_LIST_GLOBAL "s")) >= 0) {
		switch(i) {
		GETOPT_PROCESS_GLOBAL
		case 's':
			if (ctx->type == P_SONY_UPD895 ||
			    ctx->type == P_SONY_UPD897)
				j = upd895_dump_status(ctx);
			break;
		}

		if (j) return j;
	}

	return 0;
}

static int updr150_query_markers(void *vctx, struct marker **markers, int *count)
{
	struct updr150_ctx *ctx = vctx;

	*markers = &ctx->marker;
	*count = 1;

	if (ctx->type == P_SONY_UPD895 ||
	    ctx->type == P_SONY_UPD897) {
		int ret = sony_get_status(ctx);

		if (ret)
			return CUPS_BACKEND_FAILED;

		if (ctx->stsbuf[5] == 0x40 ||
		    ctx->stsbuf[5] == 0x08) {
			ctx->marker.levelnow = 0;
			STATE("+media-empty");
		} else {
			ctx->marker.levelnow = -3;
			STATE("-media-empty");
		}
	}



	return CUPS_BACKEND_OK;
}

static const char *sonyupdr150_prefixes[] = {
	"sonyupdr150",  // Family name.
	"sony-updr150", "sony-updr200", "sony-upcr10l",
	// Backwards compatibility
	"sonyupdr200", "sonyupcr10",
	"sony-upd895", "sony-upd897",
//	"sony-upd898",
	NULL
};

/* Exported */
#define USB_VID_SONY         0x054C
#define USB_PID_SONY_UPDR150 0x01E8
#define USB_PID_SONY_UPDR200 0x035F
#define USB_PID_SONY_UPCR10  0x0226
#define USB_PID_SONY_UPD895  0x0049
#define USB_PID_SONY_UPD897  0x01E7
//#define USB_PID_SONY_UPD898 XXXXX // 0x589a?

struct dyesub_backend updr150_backend = {
	.name = "Sony UP-DR150/UP-DR200/UP-CR10/UP-D895/UP-D897",
	.version = "0.31",
	.uri_prefixes = sonyupdr150_prefixes,
	.cmdline_arg = updr150_cmdline_arg,
	.cmdline_usage = updr150_cmdline,
	.init = updr150_init,
	.attach = updr150_attach,
	.teardown = updr150_teardown,
	.cleanup_job = updr150_cleanup_job,
	.read_parse = updr150_read_parse,
	.main_loop = updr150_main_loop,
	.query_markers = updr150_query_markers,
	.devices = {
		{ USB_VID_SONY, USB_PID_SONY_UPDR150, P_SONY_UPDR150, NULL, "sony-updr150"},
		{ USB_VID_SONY, USB_PID_SONY_UPDR200, P_SONY_UPDR150, NULL, "sony-updr200"},
		{ USB_VID_SONY, USB_PID_SONY_UPCR10, P_SONY_UPCR10, NULL, "sony-upcr10l"},
		{ USB_VID_SONY, USB_PID_SONY_UPD895, P_SONY_UPD895, NULL, "sonyupd895"},
		{ USB_VID_SONY, USB_PID_SONY_UPD897, P_SONY_UPD897, NULL, "sony-upd897"},
//		{ USB_VID_SONY, USB_PID_SONY_UPD898MD, P_SONY_UPD89x, NULL, "sonyupd898"},
		{ 0, 0, 0, NULL, NULL}
	}
};

/* Sony spool file format

   The spool file is a series of 4-byte commands, followed by optional
   arguments.  The purpose of the commands is unknown, but they presumably
   instruct the driver to perform certain things.

   If you treat these 4 bytes as a 32-bit little-endian number, if any of the
   least significant 4 bits are non-zero, the value is is to
   be interpreted as a driver command.  If the most significant bits are
   zero, the value signifies that the following N bytes of data should be
   sent to the printer as-is.

   Known driver "commands":

    97 ff ff ff
    eb ff ff ff  ?? 00 00 00
    ec ff ff ff  ?? 00 00 00
    ed ff ff ff  ?? 00 00 00
    ee ff ff ff  ?? 00 00 00
    ef ff ff ff  XX 00 00 00   # XX == print size (0x01/0x02/0x03/0x04)
    ef ff ff ff                # On UP-D895/897
    f3 ff ff ff
    f4 ff ff ff                # End of job on UP-D897
    f5 ff ff ff  YY 00 00 00   # YY == ??? (seen 0x01)
    f7 ff ff ff                # End of job on UP-D895

   All printer commands start with 0x1b, and are at least 7 bytes long.
   General Command format:

    1b XX ?? ?? ?? LL 00       # XX is cmd, LL is data or response length.

   UNKNOWN QUERY

 <- 1b 03 00 00 00 13 00
 -> 70 00 00 00 00 00 00 0b  00 00 00 00 00 00 00 00
    00 00 00

   UNKNOWN CMD (UP-DR & SL)

 <- 1b 0a 00 00 00 00 00

   PRINT DIMENSIONS

 <- 1b 15 00 00 00 0d 00
 <- 00 00 00 00 ZZ QQ QQ WW  WW YY YY XX XX

    QQ/WW/YY/XX are (origin_cols/origin_rows/cols/rows) in BE.
    ZZ is 0x07 on UP-DR series, 0x01 on UP-D89x series.

   RESET

 <- 1b 16 00 00 00 00 00

   UNKNOWN CMD (UP-DR & SL)

 <- 1b 17 00 00 00 00 00

   SET PARAM

 <- 1b c0 00 NN LL 00 00    # LL is response length, NN is number.
 <- [ NN bytes]

   QUERY PARAM

 <- 1b c1 00 NN LL 00 00    # LL is response length, NN is number.
 -> [ NN bytes ]

      PARAMS SEEN:

    02, len 06   [ 02 02 00 03 00 00 ]
    01, len 10   [ 02 01 00 06 00 02 00 00 00 00 ]
    00, len 5    [ 02 01 00 01 00 ]                      GAMMA TBL SET

   STATUS QUERY

 <- 1b e0 00 00 00 XX 00       # XX = 0xe (UP-D895), 0xf (All others)
 -> [14 or 15 bytes]

   PRINT DIMENSIONS II

 <- 1b e1 00 00 00 0b 00
 <- 00 80 00 00 00 00 00 XX XX YY YY  # XX = cols, YY == rows

   UNKNOWN

 <- 1b e5 00 00 00 08 00
 <- 00 00 00 00 00 00 00 XX  00  #  XX = 0 on UP-D89x & SL, 1 on up-dr series.

   DATA TRANSFER

 <- 1b ea 00 00 00 00 ZZ ZZ ZZ ZZ 00  # ZZ is BIG ENDIAN
 <- [ ZZ ZZ ZZ ZZ bytes of data ]

   UNKNOWN  (UPDR series)

 <- 1b ef 00 00 00 06 00
 -> 05 00 00 00 00 22

   COPIES

 <- 1b ee 00 00 00 02 00
 <- NN NN                        # Number of copies (BE, 1-???)


  ************************************************************************

  The data stream sent to the printer consists of all the commands in the
  spool file, plus a couple other ones that generate responses.  It is
  unknown if those additional commands are necessary.  This is a typical
  sequence:

[[ Sniff start of a UP-DR150 ]]

<- 1b e0 00 00 00 0f 00   [ STATUS QUERY ]
-> 0e 00 00 00 00 00 00 00  04 a8 08 00 0a a4 00
                                  ----- -----
                                  MAX_C MAX_R
<- 1b 16 00 00 00 00 00
-> "reset" ??

[[ begin job ]]

<- 1b ef 00 00 00 06 00
-> 05 00 00 00 00 22

<- 1b e5 00 00 00 08 00       ** In spool file
<- 00 00 00 00 00 00 01 00

<- 1b c1 00 02 06 00 00       [ Query Param 2, length 6 ]
-> 02 02 00 03 00 00

<- 1b ee 00 00 00 02 00       ** In spool file
<- 00 01

<- 1b 15 00 00 00 0d 00       ** In spool file
<- 00 00 00 00 07 00 00 00  00 08 00 0a a4

<- 1b 03 00 00 00 13 00
-> 70 00 00 00 00 00 00 0b  00 00 00 00 00 00 00 00
   00 00 00

<- 1b e1 00 00 00 0b 00       ** In spool file
<- 00 80 00 00 00 00 00 08  00 0a a4

<- 1b 03 00 00 00 13 00
-> 70 00 00 00 00 00 00 0b  00 00 00 00 00 00 00 00
   00 00 00

<- 1b ea 00 00 00 00 00 ff  60 00 00   ** In spool file
<- [[ 0x0060ff00 bytes of data ]]

<- 1b e0 00 00 00 0f 00
-> 0e 00 00 00 00 00 00 00  04 a8 08 00 0a a4 00

<- 1b 0a 00 00 00 00 00   ** In spool file
<- 1b 17 00 00 00 00 00   ** In spool file

[[fin]]

 **************

  Sony UP-CL10 / DNP SL-10 spool format:

60 ff ff ff f8 ff ff ff  fd ff ff ff
14 00 00 00
1b 15 00 00 00 0d 00 00  00 00 00 07 00 00 00 00  WW WW HH HH
fb ff ff ff f4 ff ff ff
0b 00 00 00
1b ea 00 00 00 00 SH SH  SH SH 00
SL SL SL SL

 [[ Data, rows * cols * 3 bytes ]]

f3 ff ff ff
0f 00 00 00
1b e5 00 00 00 08 00 00  00 00 00 00 00 00 00
12 00 00 00
1b e1 00 00 00 0b 00 00  80 00 00 00 00 00 WW WW  HH HH
fa ff ff ff
09 00 00 00
1b ee 00 00 00 02 00 00  NN
07 00 00 00
1b 0a 00 00 00 00 00
f9 ff ff ff fc ff ff ff
07 00 00 00
1b 17 00 00 00 00 00
f7 ff ff ff

 WW WW == Columns, Big Endian
 HH HH == Rows, Big Endian
 SL SL SL SL == Plane size, Little Endian (Rows * Cols * 3)
 SH SH SH SH == Plane size, Big Endian (Rows * Cols * 3)
 NN == Copies

 **************

  Sony UP-D895 spool format:

 XX XX == cols, BE (fixed at 1280/0x500)
 YY YY == rows, BE (798/0x031e,1038/0x040e,1475/0x05c3, 2484/09b4) @ 960/1280/1920/3840+4096
 SS SS SS SS == data len (rows * cols, LE)
 S' S' S' S' == data len (rows * cols, BE)
 NN  == copies (1 -> ??)
 GG GG == ???  0000/0050/011b/04aa/05aa at each resolution.
 G' == Gamma  01 (soft), 03 (hard), 02 (normal)

 9c ff ff ff 97 ff ff ff  00 00 00 00 00 00 00 00  00 00 00 00 ff ff ff ff

 14 00 00 00
 1b 15 00 00 00  0d 00  00 00 00 00 01 GG GG 00 00 YY YY XX XX
 0b 00 00 00
 1b ea 00 00 00  00 S' S' S' S' 00
 SS SS SS SS
 ...DATA... (rows * cols)
 ff ff ff ff
 09 00 00 00
 1b ee 00 00 00  02 00  00 NN
 0f 00 00 00
 1b e5 00 00 00  08 00  00 00 00 00 00 00 00 00
 0c 00 00 00
 1b c0 00 00 00  05 00  02 00  00 01  G'
 11 00 00 00
 1b c0 00 01 00  0a 00  02 01  00 06  00 00 00 00 00 00
 12 00 00 00
 1b e1 00 00 00  0b 00  00 08 00 GG GG 00 00 YY YY XX XX
 07 00 00 00
 1b 0a 00 00 00  00 00
 fd ff ff ff f7 ff ff ff  f8 ff ff ff

 **************

  Sony UP-D897 spool format:

 NN NN == copies  (00 for printer selected)
 XX XX == cols (fixed @ 1280)
 YY YY == rows
 GG    == gamma -- Table 2 == 2, Table 1 == 3, Table 3 == 1, Table 4 == 4
 DD    == "dark"  +- 64.
 LL    == "light" +- 64.
 AA    == "advanced" +- 32.
 SS    == Sharpness 0-14
 ZZ ZZ ZZ ZZ == Data length (BE)
 Z` Z` Z` Z` == Data length (LE)


 83 ff ff ff fc ff ff ff
 fb ff ff ff f5 ff ff ff
 f1 ff ff ff f0 ff ff ff
 ef ff ff ff

 14 00 00 00
 1b 15 00 00 00  0d 00  00 00 00 00 01 00 a2 00 00 YY YY XX XX

 0b 00 00 00
 1b ea 00 00 00  00 ZZ ZZ  ZZ ZZ 00

 Z` Z` Z` Z`
 ...DATA...

 ea ff ff ff

 09 00 00 00
 1b ee 00 00 00  02 00  NN NN

 ee ff ff ff 01 00 00 00

 0e 00 00 00
 1b e5 00 00 00  08 00  00 00 00 00 DD LL SS AA

 eb ff ff ff ?? 00 00 00   <--- 02/05   5 at #3, 2 otherwise.  Sharpness?

 0c 00 00 00
 1b c0 00 00 00  05 00  02 00  00 01  GG

 ec ff ff ff ?? 00 00 00   <--- 01/00/02/01/01  Seen.  Unknown.

 11 00 00 00
 1b c0 00 01 00  0a 00  02 01  00 06  00 00 00 00 00 00

 ed ff ff ff 00 00 00 00

 12 00 00 00
 1b e1 00 00 00  0b 00  00 08 00 00 a2 00 00 YY YY XX XX

 fa ff ff ff

 07 00 00 00
 1b 0a 00 00 00 00 00

 fc ff ff ff fd ff ff ff ff ff ff ff

 07 00 00 00
 1b 17 00 00 00 00 00

 f4 ff ff ff

 ****************

 UP-D895 comms protocol:

 <-- 1b e0 00 00 00 0e 00
 --> 0d 00 XX YY 00 SS 00 ZZ  00 00 10 00 05 00

  XX : 0xe0 when printing, 0x00 otherwise.
  YY : Number of remaining copies
  SS : Status
       0x00 = Idle
       0x08 = Door open
       0x40 = Paper empty
       0x80 = Printing
       ??   = Cooling down
       ??   = Busy / Waiting
  ZZ : Status2
       0x01 = Print complete
       0x02 = no prints yet

 UP-D897 comms protocol:

 <-- 1b e0 00 00 00 0f 00
 --> 0e 00 XX YY 00 SS RR 01  02 02 10 00 05 00 PP

  XX : 0xe0 when printing, 0x00 otherwise.
  YY : Number of remaining copies
  SS : Status
       0x00 = Idle
??       0x08 = Door open
       0x40 = Paper empty
       0x80 = Printing
       ??   = Cooling down
       ??   = Busy / Waiting
  RR : Status 2
       0x00 = Okay
       0x08 = ?? Error state?
       0x80 = Printing
  PP : Percentage complete (0-99)

Other commands seen:

 <-- 1b 16 00 00 00 00 00   -- Reset

 <-- 1b c1 00 01 00 0a 00   -- Query ID 1, legth 10
 --> 02 01 00 06 00 02 00 00  00 00

 <-- 1b c1 00 00 00 05 00   -- Query id 0, length 5
 --> 02 01 00 01 03

 <-- 1b e6 00 00 00 08 00
 --> 07 00 00 00 00 00 00 00

 <-- 1b 17 00 00 00 00 00   -- Unknown?

*/
