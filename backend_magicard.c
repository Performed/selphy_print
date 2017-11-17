/*
 *   Magicard card printer family CUPS backend -- libusb-1.0 version
 *
 *   (c) 2017 Solomon Peachy <pizza@shaftnet.org>
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
#include <time.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>

#define BACKEND magicard_backend

#include "backend_common.h"

/* Exported */
#define USB_VID_MAGICARD     0x0C1F
#define USB_PID_MAGICARD_TANGO2E 0x1800

/* Private data structure */
struct magicard_ctx {
	struct libusb_device_handle *dev;
	uint8_t endp_up;
	uint8_t endp_down;
	uint8_t type;

	uint8_t x_gp_8bpp;
	uint8_t x_gp_rk;

	uint8_t *databuf;
	int datalen;
};

struct magicard_cmd_header {
	uint8_t guard[9];  /* 0x05 */
	uint8_t guard2[1]; /* 0x01 */
	uint8_t cmd[4]; /* 'REQ,' */
	uint8_t subcmd[4]; /* '???,' */
	uint8_t arg[4]; /* '???,' */
	uint8_t footer[2]; /* 0x1c 0x03 */
};

struct magicard_cmd_simple_header {
	uint8_t guard[9];  /* 0x05 */
	uint8_t guard2[1]; /* 0x01 */
	uint8_t cmd[]; /* '???' */
//	uint8_t footer[2]; /* 0x1c 0x03 */
};

struct magicard_resp_header {
	uint8_t guard[1]; /* 0x01 */
	uint8_t subcmd_arg[7]; /* '???,???' */
	uint8_t data[0]; /* freeform resp */
//	uint8_t term[2]; /* 0x2c 0x03 terminates! */
};

struct magicard_requests {
	char *key;
	char *desc;
	uint8_t type;
};

enum {
	TYPE_UNKNOWN = 0,
	TYPE_STRING,
	TYPE_STRINGINT,
	TYPE_STRINGINT_HEX,
	TYPE_IPADDR,
	TYPE_YESNO,
	TYPE_MODEL,
};

/* Data definitions */
static struct magicard_requests magicard_sta_requests[] = {
	{ "MSR", "Serial Number", TYPE_STRING },
	{ "VRS", "Firmware Version", TYPE_STRING },
	{ "FDC", "Head Density", TYPE_STRINGINT },
	{ "FSP", "Image Start", TYPE_STRINGINT },
	{ "FEP", "Image End", TYPE_STRINGINT },
	{ "FPP", "Head Position", TYPE_STRINGINT },
	{ "MDL", "Model", TYPE_MODEL },  /* 0 == Standard.  Others? */
	{ "PID", "USB PID", TYPE_STRINGINT_HEX }, /* ASCII integer, but needs to be shown as hex */
	{ "MAC", "Ethernet MAC Address", TYPE_STRING },
	{ "DYN", "Dynamic Address", TYPE_YESNO }, /* 1 == yes, 0 == no */
	{ "IPA", "IP Address", TYPE_IPADDR },  /* ASCII signed integer */
	{ "SNM", "IP Netmask", TYPE_IPADDR },  /* ASCII signed integer */
	{ "GWY", "IP Gateway", TYPE_IPADDR },  /* ASCII signed integer */

	{ "TCQ", "Total Prints", TYPE_STRINGINT },
	{ "TCP", "Total Prints on Head", TYPE_STRINGINT },
	{ "TCN", "Total Cleaning Cycles", TYPE_STRINGINT },
	{ "CCQ", "Prints After Last Cleaning", TYPE_STRINGINT },
	{ NULL, NULL, 0 }
};

/* Helper functions */
static int magicard_build_cmd(uint8_t *buf,
			       char *cmd, char *subcmd, char *arg)
{
	struct magicard_cmd_header *hdr = (struct magicard_cmd_header *) buf;

	memset(hdr->guard, 0x05, sizeof(hdr->guard));
	hdr->guard2[0] = 0x01;
	memcpy(hdr->cmd, cmd, 3);
	hdr->cmd[3] = ',';
	memcpy(hdr->subcmd, subcmd, 3);
	hdr->subcmd[3] = ',';
	memcpy(hdr->arg, arg, 3);
	hdr->arg[3] = ',';
	hdr->footer[0] = 0x1c;
	hdr->footer[1] = 0x03;

	return sizeof(*hdr);
}

static int magicard_build_cmd_simple(uint8_t *buf,
				     char *cmd)
{
	struct magicard_cmd_simple_header *hdr = (struct magicard_cmd_simple_header *) buf;
	int len = strlen(cmd);

	memset(hdr->guard, 0x05, sizeof(hdr->guard));
	hdr->guard2[0] = 0x01;
	strncpy((char*)hdr->cmd, cmd, len);
	hdr->cmd[len] = 0x1c;
	hdr->cmd[len+1] = 0x03;

	return (sizeof(*hdr) + len + 2);
}


static uint8_t * magicard_parse_resp(uint8_t *buf, uint16_t len, uint16_t *resplen)
{
	struct magicard_resp_header *hdr = (struct magicard_resp_header *) buf;

	*resplen = len - sizeof(hdr->guard) - sizeof(hdr->subcmd_arg) - 2;
	
	return hdr->data;
}

static int magicard_query_printer(struct magicard_ctx *ctx)
{
	int ret = 0;
	int i;
	uint8_t buf[256];
	char buf2[24];

	for (i = 1 ; ; i++) {
		int num = 0;

		snprintf(buf2, sizeof(buf2), "QPR%d", i);
		ret = magicard_build_cmd_simple(buf, buf2);

		if ((ret = send_data(ctx->dev, ctx->endp_down,
				     buf, ret)))
			return ret;

		memset(buf, 0, sizeof(buf));

		ret = read_data(ctx->dev, ctx->endp_up,
				buf, sizeof(buf), &num);

		if (ret < 0)
			return ret;

		if (!memcmp(buf, "END", 3))
			break;

		buf[num] = 0;
		INFO("%s\n", buf);
	}
	return 0;
}

static int magicard_query_status(struct magicard_ctx *ctx)
{
	int ret = 0;
	int i;
	uint8_t buf[256];
	
	for (i = 0 ; ; i++) {
		uint16_t resplen = 0;
		uint8_t *resp;
		int num = 0;

		if (magicard_sta_requests[i].key == NULL)
			break;

		ret = magicard_build_cmd(buf, "REQ", "STA",
				   magicard_sta_requests[i].key);

		if ((ret = send_data(ctx->dev, ctx->endp_down,
				     buf, ret)))
			return ret;

		memset(buf, 0, sizeof(buf));
		
		ret = read_data(ctx->dev, ctx->endp_up,
				buf, sizeof(buf), &num);
		
		if (ret < 0)
			return ret;

		resp = magicard_parse_resp(buf, num, &resplen);
		resp[resplen] = 0;
		switch(magicard_sta_requests[i].type) {		
		case TYPE_IPADDR: {
			int32_t ipaddr;
			uint8_t *addr = (uint8_t *) &ipaddr;
			ipaddr = atoi((char*)resp);
			INFO("%s:\t%d.%d.%d.%d\n",
			     magicard_sta_requests[i].desc,
			     addr[3], addr[2], addr[1], addr[0]);
			     break;
		}
		case TYPE_YESNO: {
			int val = atoi((char*)resp);
			INFO("%s:\t%s\n",
			     magicard_sta_requests[i].desc,
			     val? "Yes" : "No");
			break;
		}
		case TYPE_MODEL: {
			int val = atoi((char*)resp);
			INFO("%s:\t%s\n",
			     magicard_sta_requests[i].desc,
			     val == 0? "Standard" : "Unknown");
			break;
		}
		case TYPE_STRINGINT_HEX: {
			int val = atoi((char*)resp);
			INFO("%s:\t%X\n",
			     magicard_sta_requests[i].desc,
			     val);
			break;
		}
		case TYPE_STRINGINT:
			// treat differently?
		case TYPE_STRING:
		case TYPE_UNKNOWN:
		default:
			INFO("%s:\t%s\n",
			     magicard_sta_requests[i].desc,
			     resp);
		}
	}
	
	return ret;
}

/* Main driver */
static void* magicard_init(void)
{
	struct magicard_ctx *ctx = malloc(sizeof(struct magicard_ctx));
	if (!ctx) {
		ERROR("Memory Allocation Failure!");
		return NULL;
	}
	memset(ctx, 0, sizeof(struct magicard_ctx));
	return ctx;
}

static void magicard_attach(void *vctx, struct libusb_device_handle *dev,
			   uint8_t endp_up, uint8_t endp_down, uint8_t jobid)
{
	struct magicard_ctx *ctx = vctx;
	struct libusb_device *device;
	struct libusb_device_descriptor desc;

	UNUSED(jobid);

	ctx->dev = dev;
	ctx->endp_up = endp_up;
	ctx->endp_down = endp_down;

	device = libusb_get_device(dev);
	libusb_get_device_descriptor(device, &desc);

	ctx->type = lookup_printer_type(&magicard_backend,
					desc.idVendor, desc.idProduct);

}

static void magicard_teardown(void *vctx) {
	struct magicard_ctx *ctx = vctx;

	if (!ctx)
		return;

	if (ctx->databuf)
		free(ctx->databuf);

	free(ctx);
}

static void downscale_and_extract(uint32_t pixels,
				  uint8_t *y_i, uint8_t *m_i, uint8_t *c_i,
				  uint8_t *y_o, uint8_t *m_o, uint8_t *c_o, uint8_t *k_o)
{
	uint32_t i;
	uint8_t k_shift = 0;

	for (i = 0 ; i < pixels; i++)
	{
		uint8_t y, m, c;
		uint8_t k = 0;

		/* Downscale color planes from 8bpp -> 6bpp; */
		y = *y_i++ >> 2;
		m = *m_i++ >> 2;
		c = *c_i++ >> 2;

		/* Extract "true black" from ymc data, if enabled */
		if (k_o && y == 0x3f && m == 0x3f && c == 0x3f) {
			k = 1;
			y = m = c = 0;
		}

		/* pack new YMC data from 8bpp to 6bpp.  4 bytes into 3. */
		switch (i & 3) {
		case 0:
			*y_o = ((y & 0x3f) << 2);
			*m_o = ((m & 0x3f) << 2);
			*c_o = ((c & 0x3f) << 2);
			break;
		case 1:
			*y_o++ |= (y >> 4) & 0x3;
			*m_o++ |= (m >> 4) & 0x3;
			*c_o++ |= (c >> 4) & 0x3;
			
			*y_o = ((y & 0xf) << 4);
			*m_o = ((m & 0xf) << 4);
			*c_o = ((c & 0xf) << 4);
			break;
		case 2:
			*y_o++ |= (y >> 2) & 0xf;
			*m_o++ |= (m >> 2) & 0xf;
			*c_o++ |= (c >> 2) & 0xf;

			*y_o = ((y & 0x3) << 6);
			*m_o = ((m & 0x3) << 6);
			*c_o = ((c & 0x3) << 6);
			break;
		case 3:
			*y_o++ |= (y & 0x3f);
			*m_o++ |= (m & 0x3f);
			*c_o++ |= (c & 0x3f);
			break;
		}
		
		/* And resin black, if enabled */
		if (k_o) {
			if (k_shift == 0)
				*k_o = 0;
			*k_o |= (k << k_shift++);
			if (k_shift == 8) {
				k_shift = 0;
				k_o++;
			}
		}
	}
}

#define MAX_PRINTJOB_LEN (1016*672*4) + 1024  /* 1016*672 * 4color */
#define INITIAL_BUF_LEN 1024
static int magicard_read_parse(void *vctx, int data_fd) {
	struct magicard_ctx *ctx = vctx;
	uint8_t initial_buf[INITIAL_BUF_LEN + 1];
	uint32_t buf_offset = 0;
	uint8_t *srcbuf;
	uint32_t srcbuf_offset = 0;
	int i;

	uint8_t *in_y, *in_m, *in_c;
	uint8_t *out_y, *out_m, *out_c, *out_k;
	uint32_t len_y = 0, len_m = 0, len_c = 0, len_k = 0;
	
	if (!ctx)
		return CUPS_BACKEND_FAILED;

	if (ctx->databuf) {
		free(ctx->databuf);
		ctx->databuf = NULL;
	}
	ctx->datalen = 0;
	ctx->databuf = malloc(MAX_PRINTJOB_LEN);
	if (!ctx->databuf) {
		ERROR("Memory allocation failure!\n");
		return CUPS_BACKEND_FAILED;
	}

	srcbuf = malloc(MAX_PRINTJOB_LEN);
	if (!srcbuf) {
		ERROR("Memory allocation failure!\n");
		return CUPS_BACKEND_FAILED;
	}

	/* Read in the first chunk */
	i = read(data_fd, initial_buf, INITIAL_BUF_LEN);
	initial_buf[INITIAL_BUF_LEN] = 0;
	if (i < 0)
		return i;
	if (i == 0)
		return CUPS_BACKEND_OK;  /* Ie no data */
	if (i < INITIAL_BUF_LEN) {
		return CUPS_BACKEND_CANCEL;
	}

	if (initial_buf[64] != 0x01 ||
	    initial_buf[65] != 0x2c) {
		ERROR("Unrecognized header data format @%d!\n", ctx->datalen);
		return CUPS_BACKEND_CANCEL;
	}

	/* Copy over initial header */
	memcpy(ctx->databuf + ctx->datalen, initial_buf + buf_offset, 65);
	ctx->datalen += 65;
	buf_offset += 65;

	/* Start parsing headers */
	ctx->x_gp_8bpp = ctx->x_gp_rk = 0;
	char *ptr;
	ptr = strtok((char*)initial_buf + ++buf_offset, ",\x1c");
	while (ptr && *ptr != 0x1a) {
		if (!strcmp("X-GP-8", ptr)) {
			ctx->x_gp_8bpp = 1;
		} else if (!strncmp("TDT", ptr, 3)) {
			/* Strip out the timestamp, replace it with one from the backend */
		} else if (!strcmp("X-GP-RK", ptr)) {
			ctx->x_gp_rk = 1;
		} else if (!strncmp("SZ", ptr, 2)) {
			if (ptr[2] == 'B') {
				len_y = atoi(ptr + 2);
			} else if (ptr[2] == 'G') {
				len_m = atoi(ptr + 2);
			} else if (ptr[2] == 'R') {
				len_c = atoi(ptr + 2);
			} else if (ptr[2] == 'K') {
				len_k = atoi(ptr + 2);
			}
		} else {
			/* Everything else goes in */
			ctx->datalen += sprintf((char*)ctx->databuf + ctx->datalen, ",%s", ptr);
		}

		/* Keep going */
		buf_offset += strlen(ptr) + 1;
		/* Peek ahead to see if this is it */
		if (initial_buf[buf_offset + 1] == 0x1a)
			break;
		/* Otherwise continue to the next token */
		ptr = strtok(NULL, ptr);
	}

	/* Sanity checks */
	if (!len_y || !len_m || !len_c) {
		ERROR("Plane lengths missing? %u/%u/%u!\n", len_y, len_m, len_c);
		return CUPS_BACKEND_CANCEL;
	}
	if (len_y != len_m || len_y != len_c) {
		ERROR("Inconsistent data plane lengths! %u/%u/%u!\n", len_y, len_m, len_c);
		return CUPS_BACKEND_CANCEL;
	}
	if (ctx->x_gp_rk && len_k) {
		ERROR("Data stream already has a K layer!\n");
		return CUPS_BACKEND_CANCEL;
	}

	/* Add in corrected SZB/G/R rows */
	if (ctx->x_gp_8bpp) {
		ctx->datalen += sprintf((char*)ctx->databuf + ctx->datalen, ",TDT%08X", (uint32_t) time(NULL));
		ctx->datalen += sprintf((char*)ctx->databuf + ctx->datalen, ",SZB%u", 1016 * 672 * 6 / 8);
		ctx->datalen += sprintf((char*)ctx->databuf + ctx->datalen, ",SZG%u", 1016 * 672 * 6 / 8);
		ctx->datalen += sprintf((char*)ctx->databuf + ctx->datalen, ",SZR%u", 1016 * 672 * 6 / 8);
		/* Add in a SZK length indication if requested */
		if (ctx->x_gp_rk == 1) {
			ctx->datalen += sprintf((char*)ctx->databuf + ctx->datalen, ",SZK%u", 1016 * 672 / 8);
		}
	}
	
	/* Terminate command stream */
	ctx->databuf[ctx->datalen++] = 0x1a;
	buf_offset++;

	/* Let's figure out what to do next. */
	uint32_t remain = len_y + len_m + len_c + 3 * 3 + 1;
	srcbuf_offset = INITIAL_BUF_LEN - buf_offset;
	if (ctx->x_gp_8bpp) {
		memcpy(srcbuf, initial_buf + buf_offset, srcbuf_offset);
		remain -= srcbuf_offset;

		/* Finish loading the data */
		while (remain > 0) {
			i = read(data_fd, srcbuf + srcbuf_offset, remain);
			if (i < 0) {
				ERROR("Data Read Error: %d (%d) @%d)\n", i, remain, srcbuf_offset);
				return i;
			}
			if (i == 0) {
				ERROR("Short read! (%d/%d)\n", i, remain);
				return CUPS_BACKEND_CANCEL;
			}
			srcbuf_offset += i;
			remain -= i;
		}

		/* set up source pointers */
		in_y = srcbuf;
		in_m = in_y + len_y + 3;
		in_c = in_m + len_m + 3;

		/* Set up destination pointers */
		out_y = ctx->databuf + buf_offset;
		out_m = out_y + (len_y * 6 / 8) + 3;
		out_c = out_m + (len_m * 6 / 8) + 3;
		out_k = out_c + (len_c * 6 / 8) + 3;

		/* Termination of each plane */
		memcpy(out_m - 3, in_m - 3, 3);
		memcpy(out_c - 3, in_c - 3, 3);
		memcpy(out_k - 3, in_c + len_c - 3, 3);

		if (!ctx->x_gp_rk)
			out_k = NULL;
		downscale_and_extract(len_y, in_y, in_m, in_c,
				      out_y, out_m, out_c, out_k);

		/* Terminate the K plane */
		if (out_k) {
			ctx->databuf[ctx->datalen++] = 0x1c;
			ctx->databuf[ctx->datalen++] = 0x4b;
			ctx->databuf[ctx->datalen++] = 0x3a;
		}

		/* Terminate the entire stream */
		ctx->databuf[ctx->datalen++] = 0x03;
	} else {
		/* We can use the original stream as-is.  Let's just dump it over. */
		memcpy(ctx->databuf, initial_buf, buf_offset);
		ctx->datalen = buf_offset;
		memcpy(ctx->databuf + ctx->datalen, initial_buf + buf_offset, srcbuf_offset);
		remain -= srcbuf_offset;

		/* Finish loading the data */
		while (remain > 0) {
			i = read(data_fd, ctx->databuf + ctx->datalen, remain);
			if (i < 0) {
				ERROR("Data Read Error: %d (%d) @%d)\n", i, remain, ctx->datalen);
				return i;
			}
			if (i == 0) {
				ERROR("Short read! (%d/%d)\n", i, remain);
				return CUPS_BACKEND_CANCEL;
			}
			ctx->datalen += i;
			remain -= i;
		}
	}

	/* Clean up */
	free(srcbuf);
	
	return CUPS_BACKEND_OK;
}

static int magicard_main_loop(void *vctx, int copies) {
	struct magicard_ctx *ctx = vctx;
	int ret;

	if (!ctx)
		return CUPS_BACKEND_FAILED;

top:
	if ((ret = send_data(ctx->dev, ctx->endp_down,
			     ctx->databuf, ctx->datalen)))
		return CUPS_BACKEND_FAILED;
	
	/* Clean up */
	if (terminate)
		copies = 1;

	INFO("Print complete (%d copies remaining)\n", copies - 1);

	if (copies && --copies) {
		goto top;
	}

	return CUPS_BACKEND_OK;
}

static void magicard_cmdline(void)
{
	DEBUG("\t\t[ -s ]           # Query status\n");
}

static int magicard_cmdline_arg(void *vctx, int argc, char **argv)
{
	struct magicard_ctx *ctx = vctx;
	int i, j = 0;

	if (!ctx)
		return -1;

	while ((i = getopt(argc, argv, GETOPT_LIST_GLOBAL "sq")) >= 0) {
		switch(i) {
		GETOPT_PROCESS_GLOBAL
		case 's':
			j = magicard_query_status(ctx);
			break;
		case 'q':
			j = magicard_query_printer(ctx);
			break;
		}

		if (j) return j;
	}

	return 0;
}

struct dyesub_backend magicard_backend = {
	.name = "Magicard family",
	.version = "0.03WIP",
	.uri_prefix = "magicard",
	.cmdline_arg = magicard_cmdline_arg,
	.cmdline_usage = magicard_cmdline,
	.init = magicard_init,
	.attach = magicard_attach,
	.teardown = magicard_teardown,
	.read_parse = magicard_read_parse,
	.main_loop = magicard_main_loop,
	.devices = {
	{ USB_VID_MAGICARD, USB_PID_MAGICARD_TANGO2E, P_MAGICARD, NULL},
	{ USB_VID_MAGICARD, 0xFFFF, P_MAGICARD, NULL},
	{ 0, 0, 0, NULL}
	}
};

/* Magicard family Spool file format

*/
