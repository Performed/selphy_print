/*
 *   Shinko/Sinfonia CHC-S1245 CUPS backend -- libusb-1.0 version
 *
 *   (c) 2013-2015 Solomon Peachy <pizza@shaftnet.org>
 *
 *   Development of this backend was sponsored by:
 *
 *     LiveLink Technology [ www.livelinktechnology.net ]
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


enum {
	S_IDLE = 0,
	S_PRINTER_READY_CMD,
	S_PRINTER_SENT_DATA,
	S_FINISHED,
};

/* Structure of printjob header.  All fields are LITTLE ENDIAN */
struct s1245_printjob_hdr {
	uint32_t len1;   /* Fixed at 0x10 */
	uint32_t model;  /* Equal to the printer model (eg '1245' or '2145' decimal) */
	uint32_t unk2;   /* Null */
	uint32_t unk3;   /* Fixed at 0x01 */

	uint32_t len2;   /* Fixed at 0x64 */
	uint32_t unk5;   /* Null */
	uint32_t media;  /* Fixed at 0x10 */
	uint32_t unk6;   /* Null */
	
	uint32_t method; /* Print Method */
	uint32_t mode;   /* Print Mode */
	uint32_t unk7;   /* Null */
	 int32_t mattedepth; /* 0x7fffffff for glossy, 0x00 +- 25 for matte */

	uint32_t dust;   /* Dust control */
	uint32_t columns;
	uint32_t rows;
	uint32_t copies;

	uint32_t unk10;  /* Null */
	uint32_t unk11;  /* Null */
	uint32_t unk12;  /* Null */
	uint32_t unk13;  /* 0xceffffff */

	uint32_t unk14;  /* Null */
	uint32_t unk15;  /* 0xceffffff */
	uint32_t dpi; /* Fixed at '300' (decimal) */
	uint32_t unk16;  /* 0xceffffff */

	uint32_t unk17;  /* Null */
	uint32_t unk18;  /* 0xceffffff */
	uint32_t unk19;  /* Null */
	uint32_t unk20;  /* Null */

	uint32_t unk21;  /* Null */
} __attribute__((packed));

/* Private data stucture */
struct shinkos1245_ctx {
	struct libusb_device_handle *dev;
	uint8_t endp_up;
	uint8_t endp_down;
	uint8_t jobid;
	uint8_t fast_return;

	struct s1245_printjob_hdr hdr;

	uint8_t *databuf;
	int datalen;
};

static void shinkos1245_cmdline(void)
{
}

int shinkos1245_cmdline_arg(void *vctx, int argc, char **argv)
{
	struct shinkos1245_ctx *ctx = vctx;
	int i, j = 0;

	/* Reset arg parsing */
	optind = 1;
	opterr = 0;
	while ((i = getopt(argc, argv, "")) >= 0) {
		switch(i) {
		default:
			break;  /* Ignore completely */
		}

		if (j) return j;
	}

	return 0;
}

static void *shinkos1245_init(void)
{
	struct shinkos1245_ctx *ctx = malloc(sizeof(struct shinkos1245_ctx));
	if (!ctx)
		return NULL;
	memset(ctx, 0, sizeof(struct shinkos1245_ctx));

	/* Use Fast return by default in CUPS mode */
	if (getenv("DEVICE_URI") || getenv("FAST_RETURN"))
		ctx->fast_return = 1;

	return ctx;
}

static void shinkos1245_attach(void *vctx, struct libusb_device_handle *dev, 
			       uint8_t endp_up, uint8_t endp_down, uint8_t jobid)
{
	struct shinkos1245_ctx *ctx = vctx;

	ctx->dev = dev;
	ctx->endp_up = endp_up;
	ctx->endp_down = endp_down;

	/* Ensure jobid is sane */
	ctx->jobid = (jobid & 0x7f) + 1;
}


static void shinkos1245_teardown(void *vctx) {
	struct shinkos1245_ctx *ctx = vctx;

	if (!ctx)
		return;

	if (ctx->databuf)
		free(ctx->databuf);

	free(ctx);
}

static int shinkos1245_read_parse(void *vctx, int data_fd) {
	struct shinkos1245_ctx *ctx = vctx;
	int ret;
	uint8_t tmpbuf[4];

	if (!ctx)
		return CUPS_BACKEND_FAILED;

	/* Read in then validate header */
	ret = read(data_fd, &ctx->hdr, sizeof(ctx->hdr));
	if (ret < 0)
		return ret;
	if (ret < 0 || ret != sizeof(ctx->hdr))
		return CUPS_BACKEND_CANCEL;

	if (le32_to_cpu(ctx->hdr.len1) != 0x10 ||
	    le32_to_cpu(ctx->hdr.len2) != 0x64 ||
	    le32_to_cpu(ctx->hdr.dpi) != 300) {
		ERROR("Unrecognized header data format!\n");
		return CUPS_BACKEND_CANCEL;
	}

	ctx->hdr.model = le32_to_cpu(ctx->hdr.model);

	switch(ctx->hdr.model != 1245) {
		ERROR("Unrecognized printer (%d)!\n", ctx->hdr.model);
		return CUPS_BACKEND_CANCEL;
	} 

	/* Allocate space */
	if (ctx->databuf) {
		free(ctx->databuf);
		ctx->databuf = NULL;
	}

	ctx->datalen = le32_to_cpu(ctx->hdr.rows) * le32_to_cpu(ctx->hdr.columns) * 3;
	ctx->databuf = malloc(ctx->datalen);
	if (!ctx->databuf) {
		ERROR("Memory allocation failure!\n");
		return CUPS_BACKEND_FAILED;
	}

	{
		int remain = ctx->datalen;
		uint8_t *ptr = ctx->databuf;
		do {
			ret = read(data_fd, ptr, remain);
			if (ret < 0) {
				ERROR("Read failed (%d/%d/%d)\n", 
				      ret, remain, ctx->datalen);
				perror("ERROR: Read failed");
				return ret;
			}
			ptr += ret;
			remain -= ret;
		} while (remain);
	}

	/* Make sure footer is sane too */
	ret = read(data_fd, tmpbuf, 4);
	if (ret != 4) {
		ERROR("Read failed (%d/%d/%d)\n", 
		      ret, 4, 4);
		perror("ERROR: Read failed");
		return ret;
	}
	if (tmpbuf[0] != 0x04 ||
	    tmpbuf[1] != 0x03 ||
	    tmpbuf[2] != 0x02 ||
	    tmpbuf[3] != 0x01) {
		ERROR("Unrecognized footer data format!\n");
		return CUPS_BACKEND_FAILED;
	}

	return CUPS_BACKEND_OK;
}

static int shinkos1245_main_loop(void *vctx, int copies) {
	struct shinkos1245_ctx *ctx = vctx;

	return CUPS_BACKEND_FAILED;
}

static int shinkos1245_query_serno(struct libusb_device_handle *dev, uint8_t endp_up, uint8_t endp_down, char *buf, int buf_len)
{

	buf[buf_len-1] = 0; /* ensure it's null terminated */

	return CUPS_BACKEND_OK;
}

/* Exported */
#define USB_VID_SHINKO       0x10CE
#define USB_PID_SHINKO_S1245 0x0007

struct dyesub_backend shinkos1245_backend = {
	.name = "Shinko/Sinfonia CHC-S1245",
	.version = "0.01WIP",
	.uri_prefix = "shinkos1245",
	.cmdline_usage = shinkos1245_cmdline,
	.cmdline_arg = shinkos1245_cmdline_arg,
	.init = shinkos1245_init,
	.attach = shinkos1245_attach,
	.teardown = shinkos1245_teardown,
	.read_parse = shinkos1245_read_parse,
	.main_loop = shinkos1245_main_loop,
	.query_serno = shinkos1245_query_serno,
	.devices = {
	{ USB_VID_SHINKO, USB_PID_SHINKO_S1245, P_SHINKO_S1245, ""},
	{ 0, 0, 0, ""}
	}
};

/* CHC-S1245 data format

  Spool file consists of an 116-byte header, followed by RGB-packed data,
  followed by a 4-byte footer.  Header appears to consist of a series of
  4-byte Little Endian words.

   10 00 00 00 MM MM 00 00  00 00 00 00 01 00 00 00  MM == Model (ie 1245d)
   64 00 00 00 00 00 00 00  TT 00 00 00 00 00 00 00  TT == Media Size (0x10 fixed)
   MM 00 00 00 PP 00 00 00  00 00 00 00 ZZ ZZ ZZ ZZ  MM = Print Method (aka cut control), PP = Default/Glossy/Matte (0x01/0x03/0x05), ZZ == matte intensity (0x7fffffff for glossy, else 0x00000000 +- 25 for matte)
   VV 00 00 00 WW WW 00 00  HH HH 00 00 XX 00 00 00  VV == dust; 0x00 default, 0x01 off, 0x02 on, XX == Copies
   00 00 00 00 00 00 00 00  00 00 00 00 ce ff ff ff
   00 00 00 00 ce ff ff ff  QQ QQ 00 00 ce ff ff ff  QQ == DPI, ie 300.
   00 00 00 00 ce ff ff ff  00 00 00 00 00 00 00 00
   00 00 00 00 

   [[Packed RGB payload of WW*HH*3 bytes]]

   04 03 02 01  [[ footer ]]


*/
