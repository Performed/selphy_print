/*
 *   Citizen CW-01 Photo Printer CUPS backend -- libusb-1.0 version
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

#define USB_VID_CITIZEN      0x1343
#define USB_PID_CITIZEN_CW01 0x0002 // Maybe others?
//#define USB_PID_OLMEC_OP900 XXXX

/* Private data stucture */
struct cw01_spool_hdr {
	uint8_t  type; /* 0x00 -> 0x06 */
	uint8_t  res; /* vertical resolution; 0x00 == 334dpi, 0x01 == 600dpi */
	uint8_t  copies; /* number of prints */
	uint8_t  null0;
	uint32_t plane_len; /* LE */
	uint8_t  null1[4];
};
#define DPI_334 0
#define DPI_600 1

#define TYPE_DSC  0
#define TYPE_L    1
#define TYPE_PC   2
#define TYPE_2DSC 3
#define TYPE_3L   4
#define TYPE_A5   5
#define TYPE_A6   6

#define SPOOL_PLANE_HDR_LEN 1064
#define PRINTER_PLANE_HDR_LEN 1088

struct cw01_ctx {
	struct libusb_device_handle *dev;
	uint8_t endp_up;
	uint8_t endp_down;

	uint8_t *databuf;
	struct cw01_spool_hdr hdr;
};

struct cw01_cmd {
	uint8_t esc; /* Fixed at ascii ESC, aka 0x1B */
	uint8_t p;   /* Fixed at ascii 'P' aka 0x50 */
	uint8_t arg1[6];
	uint8_t arg2[16];
	uint8_t arg3[8]; /* Decimal value of arg4's length, or empty */
	uint8_t arg4[0]; /* Extra payload if arg3 is non-empty
			    Doesn't have to be sent in the same URB */

	/* All unused elements are set to 0x20 (ie ascii space) */
};

#define min(__x, __y) ((__x) < (__y)) ? __x : __y

static void cw01_build_cmd(struct cw01_cmd *cmd, char *arg1, char *arg2, uint32_t arg3_len)
{
	memset(cmd, 0x20, sizeof(*cmd));
	cmd->esc = 0x1b;
	cmd->p = 0x50;
	memcpy(cmd->arg1, arg1, min(strlen(arg1), sizeof(cmd->arg1)));
	memcpy(cmd->arg2, arg2, min(strlen(arg2), sizeof(cmd->arg2)));
	if (arg3_len) {
		char buf[9];
		snprintf(buf, sizeof(buf), "%08u", arg3_len);
		memcpy(cmd->arg3, buf, 8);
	}

}

static void cw01_cleanup_string(char *start, int len)
{
	char *ptr = strchr(start, 0x0d);

	if (ptr && (ptr - start < len)) {
		*ptr = 0x00; /* If there is a <CR>, terminate there */
		len = ptr - start;
	} else {
		start[--len] = 0x00;  /* force null-termination */
	}

	/* Trim trailing spaces */
	while (len && start[len-1] == ' ') {
		start[--len] = 0;
	}
}

static int cw01_do_cmd(struct cw01_ctx *ctx,
			  struct cw01_cmd *cmd,
			  uint8_t *data, int len)
{
	int ret;

	if ((ret = send_data(ctx->dev, ctx->endp_down,
			     (uint8_t*)cmd, sizeof(*cmd))))
		return ret;

	if (data && len) 
		if ((ret = send_data(ctx->dev, ctx->endp_down,
				     data, len)))
			return ret;

	return CUPS_BACKEND_OK;
}

static uint8_t * cw01_resp_cmd(struct cw01_ctx *ctx,
				  struct cw01_cmd *cmd,
				  int *len)
{
	char tmp[9];
	uint8_t *respbuf;

	int ret, i, num = 0;

	memset(tmp, 0, sizeof(tmp));

	if ((ret = cw01_do_cmd(ctx, cmd, NULL, 0)))
		return NULL;

	/* Read in the response header */
	ret = read_data(ctx->dev, ctx->endp_up,
			(uint8_t*)tmp, 8, &num);
	if (ret < 0)
		return NULL;

	if (num != 8) {
		ERROR("Short read! (%d/%d)\n", num, 8);
		return NULL;
	}

	i = atoi(tmp);  /* Length of payload in bytes, possibly padded */
	respbuf = malloc(i);

	/* Read in the actual response */
	ret = read_data(ctx->dev, ctx->endp_up,
			respbuf, i, &num);
	if (ret < 0) {
		free(respbuf);
		return NULL;
	}

	if (num != i) {
		ERROR("Short read! (%d/%d)\n", num, i);
		free(respbuf);
		return NULL;
	}

	*len = num;
	return respbuf;
}

static void *cw01_init(void)
{
	struct cw01_ctx *ctx = malloc(sizeof(struct cw01_ctx));
	if (!ctx)
		return NULL;
	memset(ctx, 0, sizeof(struct cw01_ctx));

	return ctx;
}

static void cw01_attach(void *vctx, struct libusb_device_handle *dev,
			      uint8_t endp_up, uint8_t endp_down, uint8_t jobid)
{
	struct cw01_ctx *ctx = vctx;

	UNUSED(jobid);

	ctx->dev = dev;
	ctx->endp_up = endp_up;
	ctx->endp_down = endp_down;
}

static void cw01_teardown(void *vctx) {
	struct cw01_ctx *ctx = vctx;

	if (!ctx)
		return;

	if (ctx->databuf)
		free(ctx->databuf);
	free(ctx);
}

static int cw01_read_parse(void *vctx, int data_fd) {
	struct cw01_ctx *ctx = vctx;
	int i, j, remain;

	if (!ctx)
		return CUPS_BACKEND_FAILED;

	if (ctx->databuf) {
		free(ctx->databuf);
		ctx->databuf = NULL;
	}

	i = read(data_fd, (uint8_t*) &ctx->hdr, sizeof(struct cw01_spool_hdr));
	
	if (i < 0)
		return i;
	if (i == 0)
		return CUPS_BACKEND_CANCEL;

	if (i < (int)sizeof(struct cw01_spool_hdr))
		return CUPS_BACKEND_CANCEL;
	
	if (ctx->hdr.type > 0x06 || ctx->hdr.res > 0x01) {
		ERROR("Unrecognized header data format!\n");
		return CUPS_BACKEND_CANCEL;
	}
	ctx->hdr.plane_len = le32_to_cpu(ctx->hdr.plane_len);
	remain = ctx->hdr.plane_len * 3;
	ctx->databuf = malloc(remain);
	if (!ctx->databuf) {
		ERROR("Memory allocation failure!\n");
		return CUPS_BACKEND_CANCEL;
	}

	j = 0;
	while (remain) {
		i = read(data_fd, ctx->databuf + j, remain);

		if (i < 0)
			return i;

		remain -= i;
		j += i;
	}

	return CUPS_BACKEND_OK;
}

static int cw01_main_loop(void *vctx, int copies) {
	struct cw01_ctx *ctx = vctx;
	int ret;
	struct cw01_cmd cmd;
	uint8_t *resp = NULL;
	int len = 0;

	uint8_t *ptr;
	char buf[9];
	uint8_t plane_hdr[PRINTER_PLANE_HDR_LEN];

	if (!ctx)
		return CUPS_BACKEND_FAILED;

top:

	if (resp) free(resp);

	/* Query buffer state */
	cw01_build_cmd(&cmd, "INFO", "FREE_PBUFFER", 0);
	resp = cw01_resp_cmd(ctx, &cmd, &len);
	if (!resp)
		return CUPS_BACKEND_FAILED;
	cw01_cleanup_string((char*)resp, len);
	
	/* Check to see if we have sufficient buffers */
	// XXX audit these rules...?
	if (!strcmp("FBP00", (char*)resp) ||
	    (ctx->hdr.res == DPI_600 && !strcmp("FBP01", (char*)resp))) {
		INFO("Insufficient printer buffers, retrying...\n");
		sleep(1);
		goto top;
	}

	/* Get Vertical resolution */
	cw01_build_cmd(&cmd, "INFO", "RESOLUTION_V", 0);

	resp = cw01_resp_cmd(ctx, &cmd, &len);
	if (!resp)
		return CUPS_BACKEND_FAILED;

	cw01_cleanup_string((char*)resp, len);
	INFO("Vertical Resolution: '%s' dpi\n", (char*)resp + 3);
	free(resp);
	/* Get Color Control Data Version */
	cw01_build_cmd(&cmd, "TBL_RD", "Version", 0);

	resp = cw01_resp_cmd(ctx, &cmd, &len);
	if (!resp)
		return CUPS_BACKEND_FAILED;

	cw01_cleanup_string((char*)resp, len);

	INFO("Color Data Version: '%s'\n", (char*)resp);

	free(resp);

	/* Get Color Control Data Checksum */
	cw01_build_cmd(&cmd, "MNT_RD", "CTRLD_CHKSUM", 0);

	resp = cw01_resp_cmd(ctx, &cmd, &len);
	if (!resp)
		return CUPS_BACKEND_FAILED;

	cw01_cleanup_string((char*)resp, len);

	INFO("Color Data Checksum: '%s'\n", (char*)resp);

	free(resp);

	cw01_build_cmd(&cmd, "CNTRL", "QTY", 0);
	snprintf(buf, sizeof(buf), "%07d\r", copies);
	ret = cw01_do_cmd(ctx, &cmd, (uint8_t*) buf, 8);
	if (ret)
		return CUPS_BACKEND_FAILED;

	/* Start sending image data */
	ptr = ctx->databuf;

	/* Generate plane header (same for all planes) */
	memset(plane_hdr, 0, PRINTER_PLANE_HDR_LEN);
	plane_hdr[0] = 0x42;
	plane_hdr[1] = 0x4d;
	plane_hdr[2] = 0x40;
	plane_hdr[3] = 0x44;
	plane_hdr[4] = 0xab;
	plane_hdr[10] = 0x40;
	plane_hdr[11] = 0x04;
	memcpy(ptr, plane_hdr + 14, SPOOL_PLANE_HDR_LEN);

	/******** Plane 1 */
	cw01_build_cmd(&cmd, "IMAGE", "YPLANE", 0);
	snprintf(buf, sizeof(buf), "%08d", ctx->hdr.plane_len - SPOOL_PLANE_HDR_LEN + PRINTER_PLANE_HDR_LEN);
	ret = cw01_do_cmd(ctx, &cmd, (uint8_t*) buf, 8);
	if (ret)
		return CUPS_BACKEND_FAILED;

	/* Send plane header */
	if ((ret = send_data(ctx->dev, ctx->endp_down,
				     plane_hdr, PRINTER_PLANE_HDR_LEN)))
			return CUPS_BACKEND_FAILED;

	/* Send plane data */
	if ((ret = send_data(ctx->dev, ctx->endp_down,
				     ptr + SPOOL_PLANE_HDR_LEN, ctx->hdr.plane_len - SPOOL_PLANE_HDR_LEN)))
			return CUPS_BACKEND_FAILED;

	ptr += ctx->hdr.plane_len;

	/******** Plane 2 */
	cw01_build_cmd(&cmd, "IMAGE", "MPLANE", 0);
	snprintf(buf, sizeof(buf), "%08d", ctx->hdr.plane_len - SPOOL_PLANE_HDR_LEN + PRINTER_PLANE_HDR_LEN);
	ret = cw01_do_cmd(ctx, &cmd, (uint8_t*) buf, 8);
	if (ret)
		return CUPS_BACKEND_FAILED;

	/* Send plane header */
	if ((ret = send_data(ctx->dev, ctx->endp_down,
				     plane_hdr, PRINTER_PLANE_HDR_LEN)))
			return CUPS_BACKEND_FAILED;

	/* Send plane data */
	if ((ret = send_data(ctx->dev, ctx->endp_down,
				     ptr + SPOOL_PLANE_HDR_LEN, ctx->hdr.plane_len - SPOOL_PLANE_HDR_LEN)))
			return CUPS_BACKEND_FAILED;

	ptr += ctx->hdr.plane_len;

	/******** Plane 3 */
	cw01_build_cmd(&cmd, "IMAGE", "CPLANE", 0);
	snprintf(buf, sizeof(buf), "%08d", ctx->hdr.plane_len - SPOOL_PLANE_HDR_LEN + PRINTER_PLANE_HDR_LEN);
	ret = cw01_do_cmd(ctx, &cmd, (uint8_t*) buf, 8);
	if (ret)
		return CUPS_BACKEND_FAILED;

	/* Send plane header */
	if ((ret = send_data(ctx->dev, ctx->endp_down,
				     plane_hdr, PRINTER_PLANE_HDR_LEN)))
			return CUPS_BACKEND_FAILED;

	/* Send plane data */
	if ((ret = send_data(ctx->dev, ctx->endp_down,
				     ptr + SPOOL_PLANE_HDR_LEN, ctx->hdr.plane_len - SPOOL_PLANE_HDR_LEN)))
			return CUPS_BACKEND_FAILED;

	ptr += ctx->hdr.plane_len;

	/* Start print */
	cw01_build_cmd(&cmd, "CNTRL", "START", 0);
	ret = cw01_do_cmd(ctx, &cmd, NULL, 0);
	if (ret)
		return CUPS_BACKEND_FAILED;

	/* This printer handles copies internally */
	copies = 1;

	/* Clean up */
	if (terminate)
		copies = 1;
	
	INFO("Print complete (%d copies remaining)\n", copies - 1);

	if (copies && --copies) {
		goto top;
	}

	if (resp) free(resp);

	return CUPS_BACKEND_OK;
}

/* Exported */
struct dyesub_backend cw01_backend = {
	.name = "Citizen CW-01",
	.version = "0.01",
	.uri_prefix = "cw01",
	.init = cw01_init,
	.attach = cw01_attach,
	.teardown = cw01_teardown,
	.read_parse = cw01_read_parse,
	.main_loop = cw01_main_loop,
	.devices = {
	{ USB_VID_CITIZEN, USB_PID_CITIZEN_CW01, P_CITIZEN_CW01, ""},
//	{ USB_VID_CITIZEN, USB_PID_OLMEC_OP900, P_CITIZEN_CW01, ""},
	{ 0, 0, 0, ""}
	}
};

/* 

Basic spool file format:

TT RR NN 00 XX XX XX XX  00 00 00 00              <- FILE header.

  NN          : copies (0x01 or more)
  RR          : resolution; 0 == 334 dpi, 1 == 600dpi
  TT          : type 0x02 == 4x6, 0x01 == 5x3.5
  XX XX XX XX : plane length (LE)
                plane length * 3 + 12 == file length.

Followed by three planes, each with this header:

28 00 00 00 00 08 00 00  RR RR 00 00 01 00 08 00 
00 00 00 00 00 00 00 00  5a 33 00 00 YY YY 00 00
00 01 00 00 00 00 00 00

  RR RR       : rows in LE format
  YY YY       : 0x335a (334dpi) or 0x5c40 (600dpi)

Followed by 1024 bytes of color tables:

 ff ff ff 00 ... 00 00 00 00 

1024+40 = 1064 bytes of header per plane.

Always have 2048 columns of data.

followed by (2048 * rows) bytes of data.

*/
