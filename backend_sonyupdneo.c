/*
 *   Sony UP-D series (new) Photo Printer CUPS backend -- libusb-1.0 version
 *
 *   (c) 2019 Solomon Peachy <pizza@shaftnet.org>
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

#define BACKEND sonyupdneo_backend

#include "backend_common.h"

/* Private data structures */
struct updneo_printjob {
	uint8_t *databuf;
	int datalen;

	int copies_offset;
	int payload_offset;

	int copies;

	uint16_t rows;
	uint16_t cols;
};

struct updneo_ctx {
	struct libusb_device_handle *dev;
	uint8_t endp_up;
	uint8_t endp_down;
	int type;

	int native_bpp;

	struct marker marker;
};

/* Now for the code */
static void* updneo_init(void)
{
	struct updneo_ctx *ctx = malloc(sizeof(struct updneo_ctx));
	if (!ctx) {
		ERROR("Memory Allocation Failure!");
		return NULL;
	}
	memset(ctx, 0, sizeof(struct updneo_ctx));
	return ctx;
}

static int updneo_attach(void *vctx, struct libusb_device_handle *dev, int type,
			  uint8_t endp_up, uint8_t endp_down, uint8_t jobid)
{
	struct updneo_ctx *ctx = vctx;

	UNUSED(jobid);

	ctx->dev = dev;
	ctx->endp_up = endp_up;
	ctx->endp_down = endp_down;
	ctx->type = type;

	if (ctx->type == P_SONY_UPD898) {
		ctx->marker.color = "#000000";  /* Ie black! */
		ctx->native_bpp = 1;
	} else {
		ctx->marker.color = "#00FFFF#FF00FF#FFFF00";
		ctx->native_bpp = 3;
	}

	ctx->marker.name = "Unknown";
	ctx->marker.levelmax = -1;
	ctx->marker.levelnow = -2;

	return CUPS_BACKEND_OK;
}

static void updneo_cleanup_job(const void *vjob)
{
	const struct updneo_printjob *job = vjob;

	if (job->databuf)
		free(job->databuf);

	free((void*)job);
}

static void updneo_teardown(void *vctx) {
	struct updneo_ctx *ctx = vctx;

	if (!ctx)
		return;

	free(ctx);
}

#define MAX_PRINTJOB_LEN (3400*2392*3 + 2048)

static int updneo_read_parse(void *vctx, const void **vjob, int data_fd, int copies) {
	struct updneo_ctx *ctx = vctx;
	int len, run = 1;

	struct updneo_printjob *job = NULL;

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
		updneo_cleanup_job(job);
		return CUPS_BACKEND_RETRY_CURRENT;
	}

	/* Read in data chunks. */
	while(run) {
		int i;

		/* Read in data block header (256 bytes) */
		i = read(data_fd, job->databuf + job->datalen, 256);
		if (i < 0) {
			updneo_cleanup_job(job);
			return CUPS_BACKEND_CANCEL;
		}
		if (i == 0)
			break;

		/* Explicitly null terminate */
		job->databuf[job->datalen + 255] = 0;

		/* Parse header.  Format:

		   JOBSIZE=pdlname,blocklen,printsize,arg1,..,argN<NULL>

		*/

		if (strncmp("JOBSIZE=", (char*) job->databuf + job->datalen, 8)) {
			ERROR("Invalid spool format!\n");
			return CUPS_BACKEND_CANCEL;
		}
		i = 0;

		/* PDL */
		char *tok = strtok((char*)job->databuf + job->datalen + 8, "\r\n,");
		if (!tok) {
			ERROR("Invalid spool format (PDL)!\n");
			return CUPS_BACKEND_CANCEL;
		}

		/* Behavior based on the various blocks */
		if (!strncmp("PJL-T", tok, 5))
			run = 1;
		else if (!strncmp("SONY-PDL-DS2", tok, 12))
			job->payload_offset = job->datalen;

//		DEBUG("Read block '%s' @ %d ...\n", tok, job->datalen);

		/* Payload length */
		tok = strtok(NULL, "\r\n,");
		if (!tok) {
			ERROR("Invalid spool format (block length missing)!\n");
			return CUPS_BACKEND_CANCEL;
		}
		len = atoi(tok);
		if (len == 0 || len > MAX_PRINTJOB_LEN) {
			ERROR("Invalid spool format (block length %d)!\n", len);
			return CUPS_BACKEND_CANCEL;
		}
//		DEBUG("...len '%d'\n", len);

		// parse the rest?
		// 898MD: 6,0,0,0
		// D80MD: 4
		// CR20L: 64,0,0,0

		/* Read in the data chunk */
		while(len > 0) {
			i = read(data_fd, job->databuf + job->datalen, len);
			if (i < 0) {
				updneo_cleanup_job(job);
				return CUPS_BACKEND_CANCEL;
			}
			if (i == 0)
				break;

			job->datalen += i;
			len -= i;
		}
	}

	if (!job->datalen) {
		updneo_cleanup_job(job);
		return CUPS_BACKEND_CANCEL;
	}

	/* Sanity check job parameters */
	// rows * cols lines up with imgsize, and others?

	*vjob = job;

	return CUPS_BACKEND_OK;
}

static int updneo_main_loop(void *vctx, const void *vjob) {
	struct updneo_ctx *ctx = vctx;
	int ret;
	int copies;

	const struct updneo_printjob *job = vjob;

	if (!ctx)
		return CUPS_BACKEND_FAILED;
	if (!job)
		return CUPS_BACKEND_FAILED;

	copies = job->copies;

top:

	// Query printer status
	// Sanity check job parameters vs printer status
	// Check for idle

	// send over job
	if ((ret = send_data(ctx->dev, ctx->endp_down,
			     job->databuf, job->datalen)))
		return CUPS_BACKEND_FAILED;

	/* Wait for completion! */
retry:
	sleep(1);

	// Check for idle
	if (fast_return /*&& ctx->stsbuf.printing > 0 */) {
		INFO("Fast return mode enabled.\n");
	} else {
		goto retry;
	}

	/* Clean up */
	if (terminate)
		copies = 1;

// done:
	INFO("Print complete (%d copies remaining)\n", copies - 1);

	if (copies && --copies) {
		goto top;
	}

	return CUPS_BACKEND_OK;
}

static int updneo_cmdline_arg(void *vctx, int argc, char **argv)
{
	struct updneo_ctx *ctx = vctx;
	int i, j = 0;

	if (!ctx)
		return -1;

	while ((i = getopt(argc, argv, GETOPT_LIST_GLOBAL "s")) >= 0) {
		switch(i) {
		GETOPT_PROCESS_GLOBAL
		}

		if (j) return j;
	}

	return 0;
}

static int updneo_query_markers(void *vctx, struct marker **markers, int *count)
{
	struct updneo_ctx *ctx = vctx;

	*markers = &ctx->marker;
	*count = 1;

	// Do something?

	return CUPS_BACKEND_OK;
}

static const char *sonyupdneo_prefixes[] = {
	"sonyupdneo",
	"sony-upd898", "sony-upcr20l", "sony-updr80",
	"dnp-sl20",
	NULL
};

/* Exported */
#define USB_VID_SONY         0x054C
#define USB_PID_SONY_UPD898MD 0xabcd // 0x589a?
#define USB_PID_SONY_UPCR20L  0xbcde
#define USB_PID_SONY_UPDR80MD 0xcdef

struct dyesub_backend sonyupdneo_backend = {
	.name = "Sony UP-D Neo",
	.version = "0.01WIP",
	.uri_prefixes = sonyupdneo_prefixes,
	.cmdline_arg = updneo_cmdline_arg,
	.init = updneo_init,
	.attach = updneo_attach,
	.teardown = updneo_teardown,
	.cleanup_job = updneo_cleanup_job,
	.read_parse = updneo_read_parse,
	.main_loop = updneo_main_loop,
	.query_markers = updneo_query_markers,
	.devices = {
		{ USB_VID_SONY, USB_PID_SONY_UPD898MD, P_SONY_UPD898, NULL, "sony-upd898"},
		{ USB_VID_SONY, USB_PID_SONY_UPCR20L, P_SONY_UPCR20L, NULL, "sony-upcr20l"},
		{ USB_VID_SONY, USB_PID_SONY_UPDR80MD, P_SONY_UPDR80, NULL, "sony-upd80"},
		{ 0, 0, 0, NULL, NULL}
	}
};
