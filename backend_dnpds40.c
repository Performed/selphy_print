/*
 *   DNP DS40/DS80 Photo Printer CUPS backend -- libusb-1.0 version
 *
 *   (c) 2013 Solomon Peachy <pizza@shaftnet.org>
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

#define USB_VID_DNP       0x1343
#define USB_PID_DNP_DS40  0x0003
#define USB_PID_DNP_DS80  0x0004

/* Private data stucture */
struct dnpds40_ctx {
	struct libusb_device_handle *dev;
	uint8_t endp_up;
	uint8_t endp_down;

        int type;

	uint8_t *databuf;
	int datalen;
};

struct dnpds40_cmd {
	uint8_t esc;
	uint8_t p;
	uint8_t arg1[6];
	uint8_t arg2[16];
	uint8_t arg3[8]; /* Decimal value of arg4's length, or empty */

	/* All unused elements are set to ' ' (ie ascii space) */
};

#define DS40_CMD_LEN 32

#define min(__x, __y) ((__x) < (__y)) ? __x : __y

static void dnpds40_build_cmd(struct dnpds40_cmd *cmd, char *arg1, char *arg2, uint32_t arg3_len)
{
	uint i;

	memset(cmd, 0x20, sizeof(*cmd));
	cmd->esc = 0x1b;
	cmd->p = 0x50;
	memcpy(cmd->arg1, arg1, min(strlen(arg1), sizeof(cmd->arg1)));
	memcpy(cmd->arg2, arg2, min(strlen(arg2), sizeof(cmd->arg2)));
	if (arg3_len)
		snprintf((char*)cmd->arg3, 8, "%08d", arg3_len);

	DEBUG("command: '%s' ", (char*)cmd);
	for (i = 0 ; i < sizeof(*cmd); i++) {
		DEBUG2("%02x ", *(((uint8_t*)cmd)+i));
	}
	DEBUG2("\n");
}

static void dnpds40_cleanup_string(char *start, int len)
{
	char *ptr = strchr(start, 0x0d);

	if (ptr && (ptr - start < len))
		*ptr = 0x00; /* If there is a <CR>, terminate there */
	else
		*(start + len - 1) = 0x00;  /* force null-termination */
}

static char *dnpds40_media_types(char *str)
{
	char tmp[4];
	int i;

	memcpy(tmp, str + 4, 3);
	tmp[3] = 0;

	i = atoi(tmp);

	switch (i) {
	case 200: return "5x3.5 (L)";
	case 210: return "5x7 (2L)";
	case 300: return "6x4 (PC)";
	case 310: return "6x8 (A5)";
	case 400: return "6x9 (A5W)";
	case 500: return "8x10";
	case 510: return "8x12";
	default:
		break;
	}

	return "Unknown type";
}

static char *dnpds40_statuses(char *str)
{
	char tmp[6];
	int i;
	memcpy(tmp, str, 5);
	tmp[5] = 0;

	i = atoi(tmp);
	
	switch (i) {
	case 0:	return "Idle";
	case 1:	return "Printing";
	case 500: return "Cooling Print Head";
	case 510: return "Cooling Paper Motor";
	case 1000: return "Cover Open";
	case 1010: return "No Scrap Box";
	case 1100: return "Paper End";
	case 1200: return "Ribbon End";
	case 1300: return "Paper jam";
	case 1400: return "Ribbon error";
	case 1500: return "Paper Definition Error";
	case 1600: return "Data Error";
	case 2000: return "Head Voltage Error";
	case 2100: return "Head Position Error";
	case 2200: return "Power Supply Fan Error";
	case 2300: return "Cutter Error";
	case 2400: return "Pinch Roller Error";
	case 2500: return "Abnormal Head Temperature";
	case 2600: return "Abnormal Media Temperature";
	case 2610: return "Abnormal Paper Motor Temperature";
	case 2700: return "Ribbon Tension Error";
	case 2800: return "RF-ID Module Error";
	case 3000: return "System Error";
	default:
		break;
	}

	return "Unkown type";
}

static uint8_t * dnpds40_resp_cmd(struct dnpds40_ctx *ctx,
				  struct dnpds40_cmd *cmd,
				  int *len)
{
	char tmp[9];
	uint8_t *respbuf;

	int ret, i, num = 0;
	
	if ((ret = send_data(ctx->dev, ctx->endp_down,
			     (uint8_t*)cmd, sizeof(*cmd))))
		return NULL;

	/* Read in the response header */
	memset(tmp, 0, sizeof(tmp));
	ret = libusb_bulk_transfer(ctx->dev, ctx->endp_up,
				   (uint8_t*)tmp,
				   8,
				   &num,
				   5000);

	if (ret < 0 || num != 8) {
		ERROR("Failure to receive data from printer (libusb error %d: (%d/%d from 0x%02x))\n", ret, num, 8, ctx->endp_up);
		return NULL;
	}

	i = atoi(tmp);  /* Length of payload in bytes, possibly padded */
	DEBUG("readback: '%s' len %d/%d\n", (char*) tmp, i, num);

	respbuf = malloc(i);

	/* Read in the actual response */
	memset(respbuf, 0, i);
	ret = libusb_bulk_transfer(ctx->dev, ctx->endp_up,
				   respbuf,
				   i,
				   &num,
				   5000);

	if (ret < 0 || num != i) {
		ERROR("Failure to receive data from printer (libusb error %d: (%d/%d from 0x%02x))\n", ret, num, i, ctx->endp_up);

		free(respbuf);
		return NULL;
	}

	DEBUG("response: ");
	for (i = 0 ; i < num; i++) {
		DEBUG2("%02x ", respbuf[i]);
	}
	DEBUG2("\n");

	*len = num;
	return respbuf;
}

static void *dnpds40_init(void)
{
	struct dnpds40_ctx *ctx = malloc(sizeof(struct dnpds40_ctx));
	if (!ctx)
		return NULL;
	memset(ctx, 0, sizeof(struct dnpds40_ctx));

	ctx->type = P_ANY;

	return ctx;
}

static void dnpds40_attach(void *vctx, struct libusb_device_handle *dev, 
			      uint8_t endp_up, uint8_t endp_down, uint8_t jobid)
{
	struct dnpds40_ctx *ctx = vctx;
	struct libusb_device *device;
	struct libusb_device_descriptor desc;

	UNUSED(jobid);

	ctx->dev = dev;	
	ctx->endp_up = endp_up;
	ctx->endp_down = endp_down;

	device = libusb_get_device(dev);
	libusb_get_device_descriptor(device, &desc);

	/* Map out device type */
	if (desc.idProduct == USB_PID_DNP_DS40)
		ctx->type = P_DNP_DS40;
	else
		ctx->type = P_DNP_DS80;

}

static void dnpds40_teardown(void *vctx) {
	struct dnpds40_ctx *ctx = vctx;

	if (!ctx)
		return;

	if (ctx->databuf)
		free(ctx->databuf);
	free(ctx);
}

#define MAX_PRINTJOB_LEN (27927714+1024) // Add a little bit of padding

static int dnpds40_read_parse(void *vctx, int data_fd) {
	struct dnpds40_ctx *ctx = vctx;
	int i;

	if (!ctx)
		return 1;

	ctx->databuf = malloc(MAX_PRINTJOB_LEN);
	if (!ctx->databuf) {
		ERROR("Memory allocation failure!\n");
		return 2;
	}

	while((i = read(data_fd, ctx->databuf + ctx->datalen, 4096)) > 0) {
		ctx->datalen += i;
	}

	return 0;
}

static int dnpds40_main_loop(void *vctx, int copies) {
	struct dnpds40_ctx *ctx = vctx;
	int ret;

	if (!ctx)
		return 1;

	// XXX printer probably supports making copies.
	while (copies--) {
		DEBUG("Sending %d bytes to printer\n", ctx->datalen);
		if ((ret = send_data(ctx->dev, ctx->endp_down,
				     ctx->databuf, ctx->datalen)))
			return ret;
		INFO("Print complete (%d remaining)\n", copies);
	}

	/* Just dump the whole damn thing over */

	return 0;
}

static int dnpds40_get_info(struct dnpds40_ctx *ctx)
{
	struct dnpds40_cmd cmd;
	uint8_t *resp;
	int len = 0;

	/* Get Firmware Version */
	dnpds40_build_cmd(&cmd, "INFO", "FVER", 0);

	resp = dnpds40_resp_cmd(ctx, &cmd, &len);
	if (!resp)
		return -1;

	dnpds40_cleanup_string((char*)resp, len);

	INFO("Firmware Version: '%s'\n", (char*)resp);

	free(resp);

	/* Get Sensor Info */
	dnpds40_build_cmd(&cmd, "INFO", "SENSOR", 0);

	resp = dnpds40_resp_cmd(ctx, &cmd, &len);
	if (!resp)
		return -1;

	dnpds40_cleanup_string((char*)resp, len);

	INFO("Sensor Info: '%s'\n", (char*)resp);

	free(resp);

	/* Get Media Info */
	dnpds40_build_cmd(&cmd, "INFO", "MEDIA", 0);

	resp = dnpds40_resp_cmd(ctx, &cmd, &len);
	if (!resp)
		return -1;

	dnpds40_cleanup_string((char*)resp, len);

	INFO("Media Type: '%s'\n", (char*)resp);

	INFO("  %s\n", dnpds40_media_types((char*)resp));	
	switch (*(resp+4)) {
	case '1':
		INFO("   Stickier paper\n");
		break;
	case '0':
		INFO("   Standard paper\n");
		break;
	default:
		INFO("   Unknown paper(%c)\n", *(resp+4));
		break;
	}
	switch (*(resp+7)) {
	case '1':
		INFO("   With mark\n");
		break;
	case '0':
		INFO("   Without mark\n");
		break;
	default:
		INFO("   Unknown mark(%c)\n", *(resp+7));
		break;
	}

	free(resp);

	/* Get Media remaining */
	dnpds40_build_cmd(&cmd, "INFO", "MQTY", 0);

	resp = dnpds40_resp_cmd(ctx, &cmd, &len);
	if (!resp)
		return -1;

	dnpds40_cleanup_string((char*)resp, len);

	INFO("Prints Remaining: '%s'\n", (char*)resp + 4);

	free(resp);

	return 0;
}

static int dnpds40_get_status(struct dnpds40_ctx *ctx)
{
	struct dnpds40_cmd cmd;
	uint8_t *resp;
	int len = 0;

	/* Generate command */
	dnpds40_build_cmd(&cmd, "STATUS", "", 0);

	/* Send command over */
	resp = dnpds40_resp_cmd(ctx, &cmd, &len);
	if (!resp)
		return -1;

	dnpds40_cleanup_string((char*)resp, len);

	INFO("Printer Status: %s => %s\n", (char*)resp, dnpds40_statuses((char*)resp));

	free(resp);

	return 0;
}

static void dnpds40_cmdline(char *caller)
{
	DEBUG("\t\t%s [ -qs | -qi ]\n", caller);
}

static int dnpds40_cmdline_arg(void *vctx, int run, char *arg1, char *arg2)
{
	struct dnpds40_ctx *ctx = vctx;

	UNUSED(arg2);

	if (!run || !ctx)
		return (!strcmp("-qs", arg1) ||
			!strcmp("-qi", arg1));

	if (!strcmp("-qs", arg1))
		return dnpds40_get_status(ctx);
	if (!strcmp("-qi", arg1))
		return dnpds40_get_info(ctx);

	return -1;
}

/* Exported */
struct dyesub_backend dnpds40_backend = {
	.name = "DNP DS40/DS80",
	.version = "0.04",
	.uri_prefix = "dnpds40",
	.cmdline_usage = dnpds40_cmdline,
	.cmdline_arg = dnpds40_cmdline_arg,
	.init = dnpds40_init,
	.attach = dnpds40_attach,
	.teardown = dnpds40_teardown,
	.read_parse = dnpds40_read_parse,
	.main_loop = dnpds40_main_loop,
	.devices = { 
	{ USB_VID_DNP, USB_PID_DNP_DS40, P_DNP_DS40, ""},
	{ USB_VID_DNP, USB_PID_DNP_DS80, P_DNP_DS80, ""},
	{ 0, 0, 0, ""}
	}
};

/* DNP DS40 Windows Driver printer spool format:

   NOTE:  This backend (and gutenprint) do *NOT* use this format.

   UNKNOWN variables/offsets:

    - 300 vs 600dpi selection
    - number of copies
    - lamination type
    - media type

  4x6, 300dpi, 1 copy, 0 sharpen, glossy

  Page header:

  01 00 01 00  <- page setup?
  28 58 24 00  <- Total plane len == 40 + x*y + 1024  (2381864)
  00 00 00 00  <- ??

  Plane header (ie one for each plane)

  28 00 00 00
  80 07 00 00  <- X res (1920)  ( = 6.4" @ 300dpi)
  d8 04 00 00  <- Y res (1240)  ( = 4.13" @ 300dpi)
  01 00 08 00
  00 00 00 00
  00 00 00 00
  20 2e 00 00  <- 11808 = ??
  20 2e 00 00  <- 11808
  00 01 00 00
  00 00 00 00 

 [ folowed by 256 entries of color mapping starting with 0xff -> ff ff ff 00 ]
 [ followed by x*y bytes of plane data ]



  5x7, "600x600dpi", 2 copies, 0 sharpen, matte

  Page header:

  02 02 02 00 <- page setup ??
  28 4a 7d 00 <- Total plane len == 40 + x*y + 1024
  02 00 00 00 <- ??

  Plane header (ie one for each plane)

  28 00 00 00
  80 07 00 00 <- X res (1920)  ( = 6.4" @ 300dpi)
  b4 10 00 00 <- Y res (4276)  ( =~ 7.13" @ 600 dpi )
  01 00 08 00
  00 00 00 00
  00 00 00 00
  40 5c 00 00 <- 23615 = ?
  40 5c 00 00 <- 23615
  00 01 00 00
  00 00 00 00

 [ folowed by 256 entries of color mapping starting with 0xff -> ff ff ff 00 ]
 [ followed by x*y bytes of plane data ]

*/
