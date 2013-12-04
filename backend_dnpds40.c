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
	memset(cmd, 0x20, sizeof(*cmd));
	cmd->esc = 0x1b;
	cmd->p = 0x50;
	memcpy(cmd->arg1, arg1, min(strlen(arg1), sizeof(cmd->arg1)));
	memcpy(cmd->arg2, arg2, min(strlen(arg2), sizeof(cmd->arg2)));
	if (arg3_len)
		snprintf((char*)cmd->arg3, 8, "%08d", arg3_len);
}

static void dnpds40_cleanup_string(char *start, int len)
{
	char *ptr = strchr(start, 0x0d);

	if (ptr)
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
#define READBACK_LEN 1024

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
	uint8_t rdbuf[READBACK_LEN];
	char tmp[9];

	int ret, i, num = 0;
	
	/* Get Firmware Version */
	dnpds40_build_cmd(&cmd, "INFO", "FVER", 0);

	if ((ret = send_data(ctx->dev, ctx->endp_down,
			     (uint8_t*)&cmd, sizeof(cmd))))
		return ret;

	/* Read in the response */
	memset(rdbuf, 0, sizeof(rdbuf));
	ret = libusb_bulk_transfer(ctx->dev, ctx->endp_up,
				   rdbuf,
				   READBACK_LEN,
				   &num,
				   5000);

	if (ret < 0 || num < 8) {
		ERROR("Failure to receive data from printer (libusb error %d: (%d/%d from 0x%02x))\n", ret, num, (int)sizeof(rdbuf), ctx->endp_up);
		if (ret < 0)
			return ret;
		return 4;
	}

	memcpy(tmp, rdbuf, 8);
	tmp[8] = 0;
	i = atoi(tmp);  /* Length of payload in bytes, possibly padded */

	dnpds40_cleanup_string((char*)rdbuf + 8, i);

	INFO("Firmware Version: %s\n", (char*)rdbuf + 8);

	/* *************************** */

	/* Get Sensor Info */
	dnpds40_build_cmd(&cmd, "INFO", "SENSOR", 0);

	if ((ret = send_data(ctx->dev, ctx->endp_down,
			     (uint8_t*)&cmd, sizeof(cmd))))
		return ret;

	/* Read in the response */
	memset(rdbuf, 0, sizeof(rdbuf));
	ret = libusb_bulk_transfer(ctx->dev, ctx->endp_up,
				   rdbuf,
				   READBACK_LEN,
				   &num,
				   5000);

	if (ret < 0 || num < 8) {
		ERROR("Failure to receive data from printer (libusb error %d: (%d/%d from 0x%02x))\n", ret, num, (int)sizeof(rdbuf), ctx->endp_up);
		if (ret < 0)
			return ret;
		return 4;
	}

	memcpy(tmp, rdbuf, 8);
	tmp[8] = 0;
	i = atoi(tmp);  /* Length of payload in bytes, possibly padded */

	dnpds40_cleanup_string((char*)rdbuf + 8, i);

	INFO("Sensor Info: %s\n", (char*)rdbuf + 8);

	/* *************************** */

	/* Get Media Info */
	dnpds40_build_cmd(&cmd, "INFO", "MEDIA", 0);

	if ((ret = send_data(ctx->dev, ctx->endp_down,
			     (uint8_t*)&cmd, sizeof(cmd))))
		return ret;

	/* Read in the response */
	memset(rdbuf, 0, sizeof(rdbuf));
	ret = libusb_bulk_transfer(ctx->dev, ctx->endp_up,
				   rdbuf,
				   READBACK_LEN,
				   &num,
				   5000);

	if (ret < 0 || num < 8) {
		ERROR("Failure to receive data from printer (libusb error %d: (%d/%d from 0x%02x))\n", ret, num, (int)sizeof(rdbuf), ctx->endp_up);
		if (ret < 0)
			return ret;
		return 4;
	}

	memcpy(tmp, rdbuf, 8);
	tmp[8] = 0;
	i = atoi(tmp);  /* Length of payload in bytes, possibly padded */

	dnpds40_cleanup_string((char*)rdbuf + 8, i);

	INFO("Media Type: %s\n", (char*)rdbuf + 8);

	INFO("  %s\n", dnpds40_media_types((char*)rdbuf+8));	
	switch (*(rdbuf+8+4)) {
	case '1':
		INFO("   Stickier paper\n");
		break;
	case '0':
		INFO("   Standard paper\n");
		break;
	default:
		INFO("   Unknown paper(%c)\n", *(rdbuf+8+4));
		break;
	}
	switch (*(rdbuf+8+7)) {
	case '1':
		INFO("   With mark\n");
		break;
	case '0':
		INFO("   Without mark\n");
		break;
	default:
		INFO("   Unknown mark(%c)\n", *(rdbuf+8+7));
		break;
	}

	/* Get Media remaining */
	dnpds40_build_cmd(&cmd, "INFO", "MQTY", 0);

	if ((ret = send_data(ctx->dev, ctx->endp_down,
			     (uint8_t*)&cmd, sizeof(cmd))))
		return ret;

	/* Read in the response */
	memset(rdbuf, 0, sizeof(rdbuf));
	ret = libusb_bulk_transfer(ctx->dev, ctx->endp_up,
				   rdbuf,
				   READBACK_LEN,
				   &num,
				   5000);

	if (ret < 0 || num < 8) {
		ERROR("Failure to receive data from printer (libusb error %d: (%d/%d from 0x%02x))\n", ret, num, (int)sizeof(rdbuf), ctx->endp_up);
		if (ret < 0)
			return ret;
		return 4;
	}

	memcpy(tmp, rdbuf, 8);
	tmp[8] = 0;
	i = atoi(tmp);  /* Length of payload in bytes, possibly padded */

	dnpds40_cleanup_string((char*)rdbuf + 8, i);

	INFO("Prints remaining: %s\n", (char*)rdbuf + 8 + 4);

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

#if 0
	if (!strcmp("-qs", arg1))
		return dnpds40_get_status(ctx);
#endif
	if (!strcmp("-qi", arg1))
		return dnpds40_get_info(ctx);

	return -1;
}

/* Exported */
struct dyesub_backend dnpds40_backend = {
	.name = "DNP DS40/DS80",
	.version = "0.01",
	.uri_prefix = "dnpds40",
	.cmdline_usage = dnpds40_cmdline,
	.cmdline_arg = dnpds40_cmdline_arg,
	.init = dnpds40_init,
	.attach = dnpds40_attach,
	.teardown = dnpds40_teardown,
	.read_parse = dnpds40_read_parse,
	.main_loop = dnpds40_main_loop,
	.devices = { 
	{ USB_VID_DNP, USB_PID_DNP_DS40, P_DNP_DS40, "Kodak"},
	{ 0, 0, 0, ""}
	}
};
