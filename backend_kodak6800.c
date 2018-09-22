/*
 *   Kodak 6800/6850 Photo Printer CUPS backend -- libusb-1.0 version
 *
 *   (c) 2013-2018 Solomon Peachy <pizza@shaftnet.org>
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

#define BACKEND kodak6800_backend

#include "backend_common.h"

#define USB_VID_KODAK       0x040A
#define USB_PID_KODAK_6800  0x4021
#define USB_PID_KODAK_6850  0x402B

/* File header */
struct kodak6800_hdr {
	uint8_t  hdr[7];   /* Always 03 1b 43 48 43 0a 00 */
	uint8_t  jobid;    /* Non-zero */
	uint16_t copies;   /* BE, in BCD format (1-9999) */
	uint16_t columns;  /* BE */
	uint16_t rows;     /* BE */
	uint8_t  size;     /* 0x06 for 6x8, 0x00 for 6x4, 0x07 for 5x7 */
	uint8_t  laminate; /* 0x01 to laminate, 0x00 for not */
	uint8_t  mode;     /* 0x00 or 0x01 (for 4x6 on 6x8 media) */
} __attribute__((packed));

struct kodak68x0_status_readback {
	uint8_t  hdr;      /* Always 01 */
	uint8_t  status;   /* STATUS_* */
	uint8_t  status1;  /* STATUS1_* */
	uint32_t status2;  /* STATUS2_* */
	uint8_t  errcode;  /* Error ## */
	uint32_t lifetime; /* Lifetime Prints (BE) */
	uint32_t maint;    /* Maint Prints (BE) */
	uint32_t media;     /* Media Prints (6850), Unknown (6800) (BE) */
	uint32_t cutter;     /* Cutter Actuations (BE) */
	uint8_t  nullB[2];
	uint8_t  errtype;   /* seen 0x00 or 0xd0 */
	uint8_t  donor;     /* Percentage, 0-100 */
	uint16_t main_boot; /* Always 003 */
	uint16_t main_fw;   /* seen 652, 656, 670, 671 (6850) and 232 (6800) */
	uint16_t dsp_boot;  /* Always 001 */
	uint16_t dsp_fw;    /* Seen 540, 541, 560 (6850) and 131 (6800) */
	uint8_t  b1_jobid;
	uint8_t  b2_jobid;
	uint16_t b1_remain;   /* Remaining prints in job */
	uint16_t b1_complete; /* Completed prints in job */
	uint16_t b1_total;    /* Total prints in job */
	uint16_t b2_remain;   /* Remaining prints in job */
	uint16_t b2_complete; /* Completed prints in job */
	uint16_t b2_total;    /* Total prints in job */
	uint8_t  curve_status; /* Always seems to be 0x00 */
} __attribute__((packed));

enum {
	CMD_CODE_OK = 1,
	CMD_CODE_BAD = 2,
};

enum {
        STATUS_PRINTING = 1,
        STATUS_IDLE = 2,
};

enum {
        STATE_STATUS1_STANDBY = 1,
        STATE_STATUS1_ERROR = 2,
        STATE_STATUS1_WAIT = 3,
};

#define STATE_STANDBY_STATUS2 0x0

enum {
        WAIT_STATUS2_INIT = 0,
        WAIT_STATUS2_RIBBON = 1,
        WAIT_STATUS2_THERMAL = 2,
        WAIT_STATUS2_OPERATING = 3,
        WAIT_STATUS2_BUSY = 4,
};

#define ERROR_STATUS2_CTRL_CIRCUIT   (0x80000000)
#define ERROR_STATUS2_MECHANISM_CTRL (0x40000000)
#define ERROR_STATUS2_SENSOR         (0x00002000)
#define ERROR_STATUS2_COVER_OPEN     (0x00001000)
#define ERROR_STATUS2_TEMP_SENSOR    (0x00000200)
#define ERROR_STATUS2_PAPER_JAM      (0x00000100)
#define ERROR_STATUS2_PAPER_EMPTY    (0x00000040)
#define ERROR_STATUS2_RIBBON_ERR     (0x00000010)

enum {
        CTRL_CIR_ERROR_EEPROM1  = 0x01,
        CTRL_CIR_ERROR_EEPROM2  = 0x02,
        CTRL_CIR_ERROR_DSP      = 0x04,
        CTRL_CIR_ERROR_CRC_MAIN = 0x06,
        CTRL_CIR_ERROR_DL_MAIN  = 0x07,
        CTRL_CIR_ERROR_CRC_DSP  = 0x08,
        CTRL_CIR_ERROR_DL_DSP   = 0x09,
        CTRL_CIR_ERROR_ASIC     = 0x0a,
        CTRL_CIR_ERROR_DRAM     = 0x0b,
        CTRL_CIR_ERROR_DSPCOMM  = 0x29,
};

enum {
        MECH_ERROR_HEAD_UP            = 0x01,
        MECH_ERROR_HEAD_DOWN          = 0x02,
        MECH_ERROR_MAIN_PINCH_UP      = 0x03,
        MECH_ERROR_MAIN_PINCH_DOWN    = 0x04,
        MECH_ERROR_SUB_PINCH_UP       = 0x05,
        MECH_ERROR_SUB_PINCH_DOWN     = 0x06,
        MECH_ERROR_FEEDIN_PINCH_UP    = 0x07,
        MECH_ERROR_FEEDIN_PINCH_DOWN  = 0x08,
        MECH_ERROR_FEEDOUT_PINCH_UP   = 0x09,
        MECH_ERROR_FEEDOUT_PINCH_DOWN = 0x0a,
        MECH_ERROR_CUTTER_LR          = 0x0b,
        MECH_ERROR_CUTTER_RL          = 0x0c,
};

enum {
        SENSOR_ERROR_CUTTER           = 0x05,
        SENSOR_ERROR_HEAD_DOWN        = 0x09,
        SENSOR_ERROR_HEAD_UP          = 0x0a,
        SENSOR_ERROR_MAIN_PINCH_DOWN  = 0x0b,
        SENSOR_ERROR_MAIN_PINCH_UP    = 0x0c,
        SENSOR_ERROR_FEED_PINCH_DOWN  = 0x0d,
        SENSOR_ERROR_FEED_PINCH_UP    = 0x0e,
        SENSOR_ERROR_EXIT_PINCH_DOWN  = 0x0f,
        SENSOR_ERROR_EXIT_PINCH_UP    = 0x10,
        SENSOR_ERROR_LEFT_CUTTER      = 0x11,
        SENSOR_ERROR_RIGHT_CUTTER     = 0x12,
        SENSOR_ERROR_CENTER_CUTTER    = 0x13,
        SENSOR_ERROR_UPPER_CUTTER     = 0x14,
        SENSOR_ERROR_PAPER_FEED_COVER = 0x15,
};

enum {
        TEMP_SENSOR_ERROR_HEAD_HIGH = 0x01,
        TEMP_SENSOR_ERROR_HEAD_LOW  = 0x02,
        TEMP_SENSOR_ERROR_ENV_HIGH  = 0x03,
        TEMP_SENSOR_ERROR_ENV_LOW   = 0x04,
};

enum {
        COVER_OPEN_ERROR_UPPER = 0x01,
        COVER_OPEN_ERROR_LOWER = 0x02,
};

enum {
        PAPER_EMPTY_ERROR = 0x00,
};

enum {
        RIBBON_ERROR = 0x00,
};

enum {
        CURVE_TABLE_STATUS_INITIAL = 0x00,
        CURVE_TABLE_STATUS_USERSET = 0x01,
        CURVE_TABLE_STATUS_CURRENT = 0x02,
};

struct kodak6800_printsize {
	uint8_t  hdr;    /* Always 0x06 */
	uint16_t width;  /* BE */
	uint16_t height; /* BE */
	uint8_t  type;   /* MEDIA_TYPE_* [ ie paper ] */
	uint8_t  code;   /* 00, 01, 02, 03, 04, 05 seen. An index? */
	uint8_t  code2;  /* 00, 01 seen. Alternates every other 4x6 printed, but only 1 on unknown/1844x2490 print size. */
	uint8_t  null[2];
} __attribute__((packed));

#define MAX_MEDIA_LEN 128

struct kodak68x0_media_readback {
	uint8_t  hdr;      /* Always 0x01 */
	uint8_t  type;     /* Media code, KODAK68x0_MEDIA_xxx */
	uint8_t  null[5];
	uint8_t  count;    /* Always 0x04 (6800) or 0x06 (6850)? */
	struct kodak6800_printsize sizes[];
} __attribute__((packed));

#define KODAK68x0_MEDIA_6R   0x0b // 197-4096
#define KODAK68x0_MEDIA_UNK  0x03
#define KODAK68x0_MEDIA_6TR2 0x2c // 396-2941
#define KODAK68x0_MEDIA_NONE 0x00
/* 6R: Also seen: 101-0867, 141-9597, 659-9054, 169-6418, DNP 900-060 */

#define CMDBUF_LEN 17

/* Private data structure */
struct kodak6800_printjob {
	struct kodak6800_hdr hdr;
	uint8_t *databuf;
	int datalen;
	int copies;
};

struct kodak6800_ctx {
	struct libusb_device_handle *dev;
	uint8_t endp_up;
	uint8_t endp_down;

	int type;

	uint8_t jobid;

	struct kodak68x0_media_readback *media;

	struct marker marker;
};

static const char *kodak68xx_mediatypes(int type)
{
	switch(type) {
	case KODAK68x0_MEDIA_NONE:
		return "No media";
	case KODAK68x0_MEDIA_6R:
	case KODAK68x0_MEDIA_6TR2:
		return "Kodak 6R";
	default:
		return "Unknown";
	}
	return "Unknown";
}

/* Baseline commands */
static int kodak6800_do_cmd(struct kodak6800_ctx *ctx,
                              void *cmd, int cmd_len,
                              void *resp, int resp_len,
                              int *actual_len)
{
        int ret;

        /* Write command */
        if ((ret = send_data(ctx->dev, ctx->endp_down,
                             cmd, cmd_len)))
                return (ret < 0) ? ret : -99;

        /* Read response */
        ret = read_data(ctx->dev, ctx->endp_up,
                        resp, resp_len, actual_len);
        if (ret < 0)
                return ret;

        return 0;
}

static void kodak68x0_dump_mediainfo(struct kodak68x0_media_readback *media)
{
	int i;
	if (media->type == KODAK68x0_MEDIA_NONE) {
		INFO("No Media Loaded\n");
		return;
	}

	switch (media->type) {
	case KODAK68x0_MEDIA_6R:
		INFO("Media type: 6R (Kodak 197-4096 or equivalent)\n");
		break;
	case KODAK68x0_MEDIA_6TR2:
		INFO("Media type: 6R (Kodak 396-2941 or equivalent)\n");
		break;
	default:
		INFO("Media type %02x (unknown, please report!)\n", media->type);
		break;
	}
	INFO("Legal print sizes:\n");
	for (i = 0 ; i < media->count ; i++) {
		INFO("\t%d: %dx%d (%02x) %s\n", i,
		     be16_to_cpu(media->sizes[i].width),
		     be16_to_cpu(media->sizes[i].height),
		     media->sizes[i].code,
		     media->sizes[i].code2? "Disallowed?" : "");
	}
	INFO("\n");
}

static int kodak6800_get_mediainfo(struct kodak6800_ctx *ctx, struct kodak68x0_media_readback *media)
{
	uint8_t req[16];
	int ret, num;

	memset(req, 0, sizeof(req));
	memset(media, 0, sizeof(*media));

	req[0] = 0x03;
	req[1] = 0x1b;
	req[2] = 0x43;
	req[3] = 0x48;
	req[4] = 0x43;
	req[5] = 0x1a;
	req[6] = 0x00; /* This can be non-zero for additional "banks" */

	/* Issue command and get response */
	if ((ret = kodak6800_do_cmd(ctx, req, sizeof(req),
				    media, MAX_MEDIA_LEN,
				    &num)))
		return ret;

	/* Validate proper response */
	if (media->hdr != CMD_CODE_OK ||
	    media->null[0] != 0x00) {
		ERROR("Unexpected response from media query!\n");
		return CUPS_BACKEND_STOP;
	}

	return 0;
}

static int kodak68x0_canceljob(struct kodak6800_ctx *ctx,
			       int id)
{
	uint8_t req[16];
	int ret, num;
	struct kodak68x0_status_readback sts;

	memset(req, 0, sizeof(req));

	req[0] = 0x03;
	req[1] = 0x1b;
	req[2] = 0x43;
	req[3] = 0x48;
	req[4] = 0x43;
	req[5] = 0x13;
	req[6] = id;

	/* Issue command and get response */
	if ((ret = kodak6800_do_cmd(ctx, req, sizeof(req),
				    &sts, sizeof(sts),
				    &num)))
		return ret;

	/* Validate proper response */
	if (sts.hdr != CMD_CODE_OK) {
		ERROR("Unexpected response from job cancel!\n");
		return -99;
	}

	return 0;
}

static int kodak68x0_reset(struct kodak6800_ctx *ctx)
{
	uint8_t req[16];
	int ret, num;
	struct kodak68x0_status_readback sts;

	memset(req, 0, sizeof(req));

	req[0] = 0x03;
	req[1] = 0x1b;
	req[2] = 0x43;
	req[3] = 0x48;
	req[4] = 0xc0;

	/* Issue command and get response */
	if ((ret = kodak6800_do_cmd(ctx, req, sizeof(req),
				    &sts, sizeof(sts),
				    &num)))
		return ret;

	/* Validate proper response */
	if (sts.hdr != CMD_CODE_OK) {
		ERROR("Unexpected response from job cancel!\n");
		return -99;
	}

	return 0;
}


/* Structure dumps */
static char *kodak68x0_status_str(struct kodak68x0_status_readback *resp)
{
        switch(resp->status1) {
        case STATE_STATUS1_STANDBY:
                return "Standby (Ready)";
        case STATE_STATUS1_WAIT:
                switch (be32_to_cpu(resp->status2)) {
                case WAIT_STATUS2_INIT:
                        return "Wait (Initializing)";
                case WAIT_STATUS2_RIBBON:
                        return "Wait (Ribbon Winding)";
                case WAIT_STATUS2_THERMAL:
                        return "Wait (Thermal Protection)";
                case WAIT_STATUS2_OPERATING:
                        return "Wait (Operating)";
                case WAIT_STATUS2_BUSY:
                        return "Wait (Busy)";
                default:
                        return "Wait (Unknown)";
                }
        case STATE_STATUS1_ERROR:
                switch (be32_to_cpu(resp->status2)) {
                case ERROR_STATUS2_CTRL_CIRCUIT:
                        switch (resp->errcode) {
                        case CTRL_CIR_ERROR_EEPROM1:
                                return "Error (EEPROM1)";
                        case CTRL_CIR_ERROR_EEPROM2:
                                return "Error (EEPROM2)";
                        case CTRL_CIR_ERROR_DSP:
                                return "Error (DSP)";
                        case CTRL_CIR_ERROR_CRC_MAIN:
                                return "Error (Main CRC)";
                        case CTRL_CIR_ERROR_DL_MAIN:
                                return "Error (Main Download)";
                        case CTRL_CIR_ERROR_CRC_DSP:
                                return "Error (DSP CRC)";
                        case CTRL_CIR_ERROR_DL_DSP:
                                return "Error (DSP Download)";
                        case CTRL_CIR_ERROR_ASIC:
                                return "Error (ASIC)";
                        case CTRL_CIR_ERROR_DRAM:
                                return "Error (DRAM)";
                        case CTRL_CIR_ERROR_DSPCOMM:
                                return "Error (DSP Communincation)";
                        default:
                                return "Error (Unknown Circuit)";
                        }
                case ERROR_STATUS2_MECHANISM_CTRL:
                        switch (resp->errcode) {
                        case MECH_ERROR_HEAD_UP:
                                return "Error (Head Up Mechanism)";
                        case MECH_ERROR_HEAD_DOWN:
                                return "Error (Head Down Mechanism)";
                        case MECH_ERROR_MAIN_PINCH_UP:
                                return "Error (Main Pinch Up Mechanism)";
                        case MECH_ERROR_MAIN_PINCH_DOWN:
                                return "Error (Main Pinch Down Mechanism)";
                        case MECH_ERROR_SUB_PINCH_UP:
                                return "Error (Sub Pinch Up Mechanism)";
                        case MECH_ERROR_SUB_PINCH_DOWN:
                                return "Error (Sub Pinch Down Mechanism)";
                        case MECH_ERROR_FEEDIN_PINCH_UP:
                                return "Error (Feed-in Pinch Up Mechanism)";
                        case MECH_ERROR_FEEDIN_PINCH_DOWN:
                                return "Error (Feed-in Pinch Down Mechanism)";
                        case MECH_ERROR_FEEDOUT_PINCH_UP:
                                return "Error (Feed-out Pinch Up Mechanism)";
                        case MECH_ERROR_FEEDOUT_PINCH_DOWN:
                                return "Error (Feed-out Pinch Down Mechanism)";
                        case MECH_ERROR_CUTTER_LR:
                                return "Error (Left->Right Cutter)";
                        case MECH_ERROR_CUTTER_RL:
                                return "Error (Right->Left Cutter)";
                        default:
                                return "Error (Unknown Mechanism)";
                        }
                case ERROR_STATUS2_SENSOR:
                        switch (resp->errcode) {
                        case SENSOR_ERROR_CUTTER:
                                return "Error (Cutter Sensor)";
                        case SENSOR_ERROR_HEAD_DOWN:
                                return "Error (Head Down Sensor)";
                        case SENSOR_ERROR_HEAD_UP:
                                return "Error (Head Up Sensor)";
                        case SENSOR_ERROR_MAIN_PINCH_DOWN:
                                return "Error (Main Pinch Down Sensor)";
                        case SENSOR_ERROR_MAIN_PINCH_UP:
                                return "Error (Main Pinch Up Sensor)";
                        case SENSOR_ERROR_FEED_PINCH_DOWN:
                                return "Error (Feed Pinch Down Sensor)";
                        case SENSOR_ERROR_FEED_PINCH_UP:
                                return "Error (Feed Pinch Up Sensor)";
                        case SENSOR_ERROR_EXIT_PINCH_DOWN:
                                return "Error (Exit Pinch Up Sensor)";
                        case SENSOR_ERROR_EXIT_PINCH_UP:
                                return "Error (Exit Pinch Up Sensor)";
                        case SENSOR_ERROR_LEFT_CUTTER:
                                return "Error (Left Cutter Sensor)";
                        case SENSOR_ERROR_RIGHT_CUTTER:
                                return "Error (Right Cutter Sensor)";
                        case SENSOR_ERROR_CENTER_CUTTER:
                                return "Error (Center Cutter Sensor)";
                        case SENSOR_ERROR_UPPER_CUTTER:
                                return "Error (Upper Cutter Sensor)";
                        case SENSOR_ERROR_PAPER_FEED_COVER:
                                return "Error (Paper Feed Cover)";
                        default:
                                return "Error (Unknown Sensor)";
                        }
                case ERROR_STATUS2_COVER_OPEN:
                        switch (resp->errcode) {
                        case COVER_OPEN_ERROR_UPPER:
                                return "Error (Upper Cover Open)";
                        case COVER_OPEN_ERROR_LOWER:
                                return "Error (Lower Cover Open)";
                        default:
                                return "Error (Unknown Cover Open)";
                        }
                case ERROR_STATUS2_TEMP_SENSOR:
                        switch (resp->errcode) {
                        case TEMP_SENSOR_ERROR_HEAD_HIGH:
                                return "Error (Head Temperature High)";
                        case TEMP_SENSOR_ERROR_HEAD_LOW:
                                return "Error (Head Temperature Low)";
                        case TEMP_SENSOR_ERROR_ENV_HIGH:
                                return "Error (Environmental Temperature High)";
                        case TEMP_SENSOR_ERROR_ENV_LOW:
                                return "Error (Environmental Temperature Low)";
                        default:
                                return "Error (Unknown Temperature)";
                        }
                case ERROR_STATUS2_PAPER_JAM:
                        return "Error (Paper Jam)";
                case ERROR_STATUS2_PAPER_EMPTY:
                        return "Error (Paper Empty)";
                case ERROR_STATUS2_RIBBON_ERR:
                        return "Error (Ribbon)";
                default:
                        return "Error (Unknown)";
                }
        default:
                return "Unknown!";
        }
}

static void kodak68x0_dump_status(struct kodak6800_ctx *ctx, struct kodak68x0_status_readback *status)
{
	char *detail;

	switch (status->status) {
        case STATUS_PRINTING:
                detail = "Printing";
                break;
        case STATUS_IDLE:
                detail = "Idle";
                break;
        default:
                detail = "Unknown";
                break;
        }
        INFO("Printer Status :  %s\n", detail);

        INFO("Printer State  : %s # %02x %08x %02x\n",
             kodak68x0_status_str(status),
             status->status1, be32_to_cpu(status->status2), status->errcode);

	INFO("Bank 1 ID: %u\n", status->b1_jobid);
	INFO("\tPrints:  %d/%d complete\n",
	     be16_to_cpu(status->b1_complete), be16_to_cpu(status->b1_total));
	INFO("Bank 2 ID: %u\n", status->b2_jobid);
	INFO("\tPrints:  %d/%d complete\n",
	     be16_to_cpu(status->b2_complete), be16_to_cpu(status->b2_total));

	switch (status->curve_status) {
	case CURVE_TABLE_STATUS_INITIAL:
		detail = "Initial/Default";
		break;
	case CURVE_TABLE_STATUS_USERSET:
		detail = "User Stored";
		break;
	case CURVE_TABLE_STATUS_CURRENT:
		detail = "Current";
		break;
	default:
		detail = "Unknown";
		break;
	}
	INFO("Tone Curve Status: %s\n", detail);

	INFO("Counters:\n");
	INFO("\tLifetime      : %u\n", be32_to_cpu(status->lifetime));
	INFO("\tThermal Head  : %u\n", be32_to_cpu(status->maint));
	INFO("\tCutter        : %u\n", be32_to_cpu(status->cutter));

	if (ctx->type == P_KODAK_6850) {
		int max;

		INFO("\tMedia         : %u\n", be32_to_cpu(status->media));

		switch(ctx->media->type) {
		case KODAK68x0_MEDIA_6R:
 		case KODAK68x0_MEDIA_6TR2:
			max = 375;
			break;
		default:
			max = 0;
			break;
		}

		if (max) {
			INFO("\t  Remaining   : %d\n", max - be32_to_cpu(status->media));
		} else {
			INFO("\t  Remaining   : Unknown\n");
		}
	}
	INFO("Main FW version : %d\n", be16_to_cpu(status->main_fw));
	INFO("DSP FW version  : %d\n", be16_to_cpu(status->dsp_fw));
	INFO("Donor           : %u%%\n", status->donor);
	INFO("\n");
}

static int kodak6800_get_status(struct kodak6800_ctx *ctx,
				struct kodak68x0_status_readback *status)
{
	uint8_t req[16];
	int ret, num;

	memset(req, 0, sizeof(req));
	memset(status, 0, sizeof(*status));

	req[0] = 0x03;
	req[1] = 0x1b;
	req[2] = 0x43;
	req[3] = 0x48;
	req[4] = 0x43;
	req[5] = 0x03;

	/* Issue command and get response */
	if ((ret = kodak6800_do_cmd(ctx, req, sizeof(req),
				    status, sizeof(*status),
				    &num)))
		return ret;

	/* Validate proper response */
	if (status->hdr != CMD_CODE_OK) {
		ERROR("Unexpected response from status query!\n");
		return -99;
	}

	return 0;
}


#define UPDATE_SIZE 1536
static int kodak6800_get_tonecurve(struct kodak6800_ctx *ctx, char *fname)
{
	uint8_t cmdbuf[16];
	uint8_t respbuf[64];
	int ret, num = 0;
	int i;

	uint16_t *data = malloc(UPDATE_SIZE);
	if (!data) {
		ERROR("Memory Allocation Failure\n");
		return -1;
	}

	INFO("Dump Tone Curve to '%s'\n", fname);

	/* Initial Request */
	cmdbuf[0] = 0x03;
	cmdbuf[1] = 0x1b;
	cmdbuf[2] = 0x43;
	cmdbuf[3] = 0x48;
	cmdbuf[4] = 0x43;
	cmdbuf[5] = 0x0c;
	cmdbuf[6] = 0x54;
	cmdbuf[7] = 0x4f;
	cmdbuf[8] = 0x4e;
	cmdbuf[9] = 0x45;
	cmdbuf[10] = 0x72;
	cmdbuf[11] = 0x01; /* 01 for user tonecurve, can be 00 or 02 */
	cmdbuf[12] = 0x00; /* param table? */
	cmdbuf[13] = 0x00;
	cmdbuf[14] = 0x00;
	cmdbuf[15] = 0x00;

	respbuf[0] = 0xff;
	/* Issue command and get response */
	if ((ret = kodak6800_do_cmd(ctx, cmdbuf, sizeof(cmdbuf),
				    respbuf, sizeof(respbuf),
				    &num)))

	/* Validate proper response */
	if (respbuf[0] != CMD_CODE_OK) {
		ERROR("Unexpected response from tonecurve query!\n");
		ret = -99;
		goto done;
	}

	/* Then we can poll the data */
	cmdbuf[0] = 0x03;
	cmdbuf[1] = 0x1b;
	cmdbuf[2] = 0x43;
	cmdbuf[3] = 0x48;
	cmdbuf[4] = 0x43;
	cmdbuf[5] = 0x0c;
	cmdbuf[6] = 0x54;
	cmdbuf[7] = 0x4f;
	cmdbuf[8] = 0x4e;
	cmdbuf[9] = 0x45;
	cmdbuf[10] = 0x20;
	for (i = 0 ; i < 24 ; i++) {
		/* Issue command and get response */
		if ((ret = kodak6800_do_cmd(ctx, cmdbuf, sizeof(cmdbuf),
					    respbuf, sizeof(respbuf),
					    &num)))
			goto done;

		if (num != 64) {
			ERROR("Short read! (%d/%d)\n", num, 51);
			ret = 4;
			goto done;
		}

		/* Copy into buffer */
		memcpy(((uint8_t*)data)+i*64, respbuf, 64);
	}

	/* Open file and write it out */
	{
		int tc_fd = open(fname, O_WRONLY|O_CREAT, S_IRUSR|S_IWUSR);
		if (tc_fd < 0) {
			ret = 4;
			goto done;
		}

		for (i = 0 ; i < 768; i++) {
			/* Byteswap appropriately */
			data[i] = cpu_to_be16(le16_to_cpu(data[i]));
			write(tc_fd, &data[i], sizeof(uint16_t));
		}
		close(tc_fd);
	}

 done:
	/* We're done */
	free(data);

	return ret;
}

static int kodak6800_set_tonecurve(struct kodak6800_ctx *ctx, char *fname)
{
	uint8_t cmdbuf[64];
	uint8_t respbuf[64];
	int ret, num = 0;
	int remain;

	uint16_t *data = malloc(UPDATE_SIZE);
	uint8_t *ptr;

	if (!data) {
		ERROR("Memory Allocation Failure\n");
		return -1;
	}

	INFO("Set Tone Curve from '%s'\n", fname);

	/* Read in file */
	int tc_fd = open(fname, O_RDONLY);
	if (tc_fd < 0) {
		ret = -1;
		goto done;
	}
	if (read(tc_fd, data, UPDATE_SIZE) != UPDATE_SIZE) {
	        ret = -2;
		goto done;
	}
	close(tc_fd);

	/* Byteswap data to printer's format */
	for (ret = 0; ret < (UPDATE_SIZE)/2 ; ret++) {
		data[ret] = cpu_to_le16(be16_to_cpu(data[ret]));
	}

	/* Initial Request */
	cmdbuf[0] = 0x03;
	cmdbuf[1] = 0x1b;
	cmdbuf[2] = 0x43;
	cmdbuf[3] = 0x48;
	cmdbuf[4] = 0x43;
	cmdbuf[5] = 0x0c;
	cmdbuf[6] = 0x54;
	cmdbuf[7] = 0x4f;
	cmdbuf[8] = 0x4e;
	cmdbuf[9] = 0x45;
	cmdbuf[10] = 0x77;
	cmdbuf[11] = 0x01; /* User TC.  Can be 00 or 02 */
	cmdbuf[12] = 0x00; /* param table? */
	cmdbuf[13] = 0x00;
	cmdbuf[14] = 0x00;
	cmdbuf[15] = 0x00;

	/* Issue command and get response */
	if ((ret = kodak6800_do_cmd(ctx, cmdbuf, sizeof(cmdbuf),
				    respbuf, sizeof(respbuf),
				    &num)))

	/* Validate proper response */
	if (num != 51) {
		ERROR("Short read! (%d/%d)\n", num, 51);
		ret = 4;
		goto done;
	}

	if (respbuf[0] != CMD_CODE_OK) {
		ERROR("Unexpected response from tonecurve set!\n");
		ret = -99;
		goto done;
	}

	ptr = (uint8_t*) data;
	remain = UPDATE_SIZE;
	while (remain > 0) {
		int count = remain > 63 ? 63 : remain;

		cmdbuf[0] = 0x03;
		memcpy(cmdbuf+1, ptr, count);

		remain -= count;
		ptr += count;

		/* Issue command and get response */
		if ((ret = kodak6800_do_cmd(ctx, cmdbuf, count + 1,
					    respbuf, sizeof(respbuf),
					    &num)))

		if (num != 51) {
			ERROR("Short read! (%d/%d)\n", num, 51);
			ret = 4;
			goto done;
		}
		if (respbuf[0] != CMD_CODE_OK) {
			ERROR("Unexpected response from tonecurve set!\n");
			ret = -99;
			goto done;
		}
	};

done:
	/* We're done */
	free(data);
	return ret;
}

static int kodak6800_query_serno(struct libusb_device_handle *dev, uint8_t endp_up, uint8_t endp_down, char *buf, int buf_len)
{
	struct kodak6800_ctx ctx = {
		.dev = dev,
		.endp_up = endp_up,
		.endp_down = endp_down,
	};

	int ret;
	int num;

	uint8_t resp[33];
	uint8_t req[16];

	memset(req, 0, sizeof(req));
	memset(resp, 0, sizeof(resp));

	req[0] = 0x03;
	req[1] = 0x1b;
	req[2] = 0x43;
	req[3] = 0x48;
	req[4] = 0x43;
	req[5] = 0x12;

	/* Issue command and get response */
	if ((ret = kodak6800_do_cmd(&ctx, req, sizeof(req),
				    resp, sizeof(resp),
				    &num)))
		return ret;

	if (num != 32) {
		ERROR("Short read! (%d/%d)\n", num, 32);
		return -2;
	}

	strncpy(buf, (char*)resp+24, buf_len);
	buf[buf_len-1] = 0;

	return 0;
}

static int kodak6850_send_unk(struct kodak6800_ctx *ctx)
{
	uint8_t cmdbuf[16];
	uint8_t rdbuf[64];
	int ret = 0, num = 0;

	memset(cmdbuf, 0, sizeof(cmdbuf));
	cmdbuf[0] = 0x03;
	cmdbuf[1] = 0x1b;
	cmdbuf[2] = 0x43;
	cmdbuf[3] = 0x48;
	cmdbuf[4] = 0x43;
	cmdbuf[5] = 0x4c;

	/* Issue command and get response */
	if ((ret = kodak6800_do_cmd(ctx, cmdbuf, sizeof(cmdbuf),
				    rdbuf, sizeof(rdbuf),
				    &num)))
		return -1;

	if (num != 51) {
		ERROR("Short read! (%d/%d)\n", num, 51);
		return CUPS_BACKEND_FAILED;
	}

	if (rdbuf[0] != CMD_CODE_OK ||
	    rdbuf[2] != 0x43) {
		ERROR("Unexpected response from printer init!\n");
		return CUPS_BACKEND_FAILED;
	}

#if 0
	// XXX No particular idea what this actually is
	if (rdbuf[1] != 0x01 && rdbuf[1] != 0x00) {
		ERROR("Unexpected status code (0x%02x)!\n", rdbuf[1]);
		return CUPS_BACKEND_FAILED;
	}
#endif
	return ret;
}

static void kodak6800_cmdline(void)
{
	DEBUG("\t\t[ -c filename ]  # Get tone curve\n");
	DEBUG("\t\t[ -C filename ]  # Set tone curve\n");
	DEBUG("\t\t[ -m ]           # Query media\n");
	DEBUG("\t\t[ -s ]           # Query status\n");
	DEBUG("\t\t[ -R ]           # Reset printer\n");
	DEBUG("\t\t[ -X jobid ]     # Cancel Job\n");
}

static int kodak6800_cmdline_arg(void *vctx, int argc, char **argv)
{
	struct kodak6800_ctx *ctx = vctx;
	int i, j = 0;

	if (!ctx)
		return -1;

	while ((i = getopt(argc, argv, GETOPT_LIST_GLOBAL "C:c:mRsX:")) >= 0) {
		switch(i) {
		GETOPT_PROCESS_GLOBAL
		case 'c':
			j = kodak6800_get_tonecurve(ctx, optarg);
			break;
		case 'C':
			j = kodak6800_set_tonecurve(ctx, optarg);
			break;
		case 'm':
			kodak68x0_dump_mediainfo(ctx->media);
			break;
		case 'R':
			kodak68x0_reset(ctx);
			break;
		case 's': {
			struct kodak68x0_status_readback status;
			j = kodak6800_get_status(ctx, &status);
			if (!j)
				kodak68x0_dump_status(ctx, &status);
			break;
		}
		case 'X':
			j = kodak68x0_canceljob(ctx, atoi(optarg));
			break;
		default:
			break;  /* Ignore completely */
		}

		if (j) return j;
	}

	return 0;
}

static void *kodak6800_init(void)
{
	struct kodak6800_ctx *ctx = malloc(sizeof(struct kodak6800_ctx));
	if (!ctx) {
		ERROR("Memory Allocation Failure\n");
		return NULL;
	}
	memset(ctx, 0, sizeof(struct kodak6800_ctx));

	ctx->media = malloc(MAX_MEDIA_LEN);

	return ctx;
}

static int kodak6800_attach(void *vctx, struct libusb_device_handle *dev, int type,
			    uint8_t endp_up, uint8_t endp_down, uint8_t jobid)
{
	struct kodak6800_ctx *ctx = vctx;

	ctx->dev = dev;
	ctx->endp_up = endp_up;
	ctx->endp_down = endp_down;
	ctx->type = type;

        /* Ensure jobid is sane */
        ctx->jobid = jobid & 0x7f;
	if (!ctx->jobid)
		ctx->jobid++;

	if (test_mode < TEST_MODE_NOATTACH) {
		/* Query media info */
		if (kodak6800_get_mediainfo(ctx, ctx->media)) {
			ERROR("Can't query media\n");
			return CUPS_BACKEND_FAILED;
		}
	} else {
		int media_code = KODAK68x0_MEDIA_6TR2;
		if (getenv("MEDIA_CODE"))
			media_code = atoi(getenv("MEDIA_CODE"));

		ctx->media->type = media_code;
	}

	ctx->marker.color = "#00FFFF#FF00FF#FFFF00";
	ctx->marker.name = kodak68xx_mediatypes(ctx->media->type);
	ctx->marker.levelmax = 100; /* Ie percentage */
	ctx->marker.levelnow = -2;

	return CUPS_BACKEND_OK;
}

static void kodak6800_cleanup_job(const void *vjob)
{
	const struct kodak6800_printjob *job = vjob;

	if (job->databuf)
		free(job->databuf);

	free((void*)job);
}

static void kodak6800_teardown(void *vctx) {
	struct kodak6800_ctx *ctx = vctx;

	if (!ctx)
		return;

	free(ctx);
}

static int kodak6800_read_parse(void *vctx, const void **vjob, int data_fd, int copies) {
	struct kodak6800_ctx *ctx = vctx;
	int ret;

	struct kodak6800_printjob *job = NULL;

	if (!ctx)
		return CUPS_BACKEND_FAILED;

	job = malloc(sizeof(*job));
	if (!job) {
		ERROR("Memory allocation failure!\n");
		return CUPS_BACKEND_RETRY_CURRENT;
	}
	memset(job, 0, sizeof(*job));

	/* Read in then validate header */
	ret = read(data_fd, &job->hdr, sizeof(job->hdr));
	if (ret < 0 || ret != sizeof(job->hdr)) {
		if (ret == 0)
			return CUPS_BACKEND_CANCEL;
		ERROR("Read failed (%d/%d/%d)\n",
		      ret, 0, (int)sizeof(job->hdr));
		perror("ERROR: Read failed");
		return CUPS_BACKEND_CANCEL;
	}
	if (job->hdr.hdr[0] != 0x03 ||
	    job->hdr.hdr[1] != 0x1b ||
	    job->hdr.hdr[2] != 0x43 ||
	    job->hdr.hdr[3] != 0x48 ||
	    job->hdr.hdr[4] != 0x43) {
		ERROR("Unrecognized data format!\n");
		return CUPS_BACKEND_CANCEL;
	}

	job->datalen = be16_to_cpu(job->hdr.rows) * be16_to_cpu(job->hdr.columns) * 3;
	job->databuf = malloc(job->datalen);
	if (!job->databuf) {
		ERROR("Memory allocation failure!\n");
		return CUPS_BACKEND_RETRY_CURRENT;
	}

	{
		int remain = job->datalen;
		uint8_t *ptr = job->databuf;
		do {
			ret = read(data_fd, ptr, remain);
			if (ret < 0) {
				ERROR("Read failed (%d/%d/%d)\n",
				      ret, remain, job->datalen);
				perror("ERROR: Read failed");
				return CUPS_BACKEND_CANCEL;
			}
			ptr += ret;
			remain -= ret;
		} while (remain);
	}

        /* Fix max print count. */
        if (copies > 9999) // XXX test against remaining media
                copies = 9999;

	/* Printer handles generating copies.. */
	if (le16_to_cpu(job->hdr.copies) < copies)
		job->hdr.copies = cpu_to_be16(uint16_to_packed_bcd(copies));

	*vjob = job;

	return CUPS_BACKEND_OK;
}

static int kodak6800_main_loop(void *vctx, const void *vjob) {
	struct kodak6800_ctx *ctx = vctx;
	struct kodak68x0_status_readback status;

	int num, ret;

	const struct kodak6800_printjob *job = vjob;

	if (!ctx)
		return CUPS_BACKEND_FAILED;
	if (!job)
		return CUPS_BACKEND_FAILED;

	struct kodak6800_hdr hdr;
	memcpy(&hdr, &job->hdr, sizeof(hdr));

	/* Validate against supported media list */
	for (num = 0 ; num < ctx->media->count; num++) {
		if (ctx->media->sizes[num].height == hdr.rows &&
		    ctx->media->sizes[num].width == hdr.columns &&
		    ctx->media->sizes[num].code2 == 0x00) // XXX code2?
			break;
	}
	if (num == ctx->media->count) {
		ERROR("Print size unsupported by media!\n");
		return CUPS_BACKEND_HOLD;
	}

	INFO("Waiting for printer idle\n");

	while(1) {
		if (kodak6800_get_status(ctx, &status))
			return CUPS_BACKEND_FAILED;

		if (ctx->marker.levelnow != status.donor) {
			ctx->marker.levelnow = status.donor;
			dump_markers(&ctx->marker, 1, 0);
		}

		if (status.status1 == STATE_STATUS1_ERROR) {
			INFO("Printer State: %s # %02x %08x %02x\n",
				kodak68x0_status_str(&status),
				status.status1, be32_to_cpu(status.status2), status.errcode);
			return CUPS_BACKEND_FAILED;
		}

		if (status.status == STATUS_IDLE)
			break;

		/* make sure we're not colliding with an existing
		   jobid */
		while (ctx->jobid == status.b1_jobid ||
		       ctx->jobid == status.b2_jobid) {
			ctx->jobid++;
			ctx->jobid &= 0x7f;
			if (!ctx->jobid)
				ctx->jobid++;
		}

		/* See if we have an open bank */
                if (!status.b1_remain ||
                    !status.b2_remain)
                        break;

		sleep(1);
	}

	/* This command is unknown, sort of a secondary status query */
	if (ctx->type == P_KODAK_6850) {
		ret = kodak6850_send_unk(ctx);
		if (ret)
			return ret;
	}

	hdr.jobid = ctx->jobid;

#if 0
	/* If we want to disable 4x6 rewind on 8x6 media.. */
	// XXX not sure about this...?
	if (hdr.size == 0x00 &&
	    be16_to_cpu(ctx->media->sizes[0].width) == 0x0982) {
		hdr.size = 0x06;
		hdr.mode = 0x01;
	}
#endif

	INFO("Sending Print Job (internal id %u)\n", ctx->jobid);
	if ((ret = kodak6800_do_cmd(ctx, (uint8_t*) &hdr, sizeof(hdr),
				    &status, sizeof(status),
				    &num)))
		return ret;

	if (status.hdr != CMD_CODE_OK) {
		ERROR("Unexpected response from print command!\n");
		return CUPS_BACKEND_FAILED;
	}

//	sleep(1); // Appears to be necessary for reliability
	INFO("Sending image data\n");
	if ((send_data(ctx->dev, ctx->endp_down,
			     job->databuf, job->datalen)) != 0)
		return CUPS_BACKEND_FAILED;

	INFO("Waiting for printer to acknowledge completion\n");
	do {
		sleep(1);
		if (kodak6800_get_status(ctx, &status))
			return CUPS_BACKEND_FAILED;

		if (ctx->marker.levelnow != status.donor) {
			ctx->marker.levelnow = status.donor;
			dump_markers(&ctx->marker, 1, 0);
		}

		if (status.status1 == STATE_STATUS1_ERROR) {
			INFO("Printer State: %s # %02x %08x %02x\n",
				kodak68x0_status_str(&status),
				status.status1, be32_to_cpu(status.status2), status.errcode);
			return CUPS_BACKEND_FAILED;
		}

		/* If all prints are complete, we're done! */
		if (status.b1_jobid == hdr.jobid && status.b1_complete == status.b1_total)
			break;
		if (status.b2_jobid == hdr.jobid && status.b2_complete == status.b2_total)
			break;

		if (fast_return) {
			INFO("Fast return mode enabled.\n");
			break;
		}

	} while (1);

	INFO("Print complete\n");

	return CUPS_BACKEND_OK;
}

static int kodak6800_query_markers(void *vctx, struct marker **markers, int *count)
{
	struct kodak6800_ctx *ctx = vctx;
	struct kodak68x0_status_readback status;

	/* Query printer status */
	if (kodak6800_get_status(ctx, &status))
		return CUPS_BACKEND_FAILED;

	ctx->marker.levelnow = status.donor;

	*markers = &ctx->marker;
	*count = 1;

	return CUPS_BACKEND_OK;
}

static const char *kodak6800_prefixes[] = {
	"kodak68x0", // Family driver, do not nuke.
	"kodak-6800", "kodak-6850",
	// Backwards-compatibility
	"kodak6800", "kodak6850",
	NULL
};

/* Exported */
struct dyesub_backend kodak6800_backend = {
	.name = "Kodak 6800/6850",
	.version = "0.65",
	.uri_prefixes = kodak6800_prefixes,
	.cmdline_usage = kodak6800_cmdline,
	.cmdline_arg = kodak6800_cmdline_arg,
	.init = kodak6800_init,
	.attach = kodak6800_attach,
	.teardown = kodak6800_teardown,
	.cleanup_job = kodak6800_cleanup_job,
	.read_parse = kodak6800_read_parse,
	.main_loop = kodak6800_main_loop,
	.query_serno = kodak6800_query_serno,
	.query_markers = kodak6800_query_markers,
	.devices = {
		{ USB_VID_KODAK, USB_PID_KODAK_6800, P_KODAK_6800, "Kodak", "kodak-6800"},
		{ USB_VID_KODAK, USB_PID_KODAK_6850, P_KODAK_6850, "Kodak", "kodak-6850"},
		{ 0, 0, 0, NULL, NULL}
	}
};

/* Kodak 6800/6850 data format

  Spool file consists of 17-byte header followed by plane-interleaved BGR data.
  Native printer resolution is 1844 pixels per row, and 1240 or 2434 rows.

  6850 Adds support for 5x7, with 1548 pixels per row and 2140 columns.

  All fields are BIG ENDIAN unless otherwise specified.

  Header:

  03 1b 43 48 43 0a 00           Fixed header
  II                             Job ID (1-255)
  NN NN                          Number of copies in BCD form (0001->9999)
  WW WW                          Number of columns (Fixed at 1844 on 6800)
  HH HH                          Number of rows.
  SS                             Print size -- 0x00 (4x6) 0x06 (8x6) 0x07 (5x7 on 6850)
  LL                             Laminate mode -- 0x00 (off) or 0x01 (on)
  UU                             Print mode -- 0x00 (normal) or (0x01) 4x6 on 8x6

  ************************************************************************

  Note:  6800 is Shinko CHC-S1145-5A, 6850 is Shinko CHC-S1145-5B

  Both are very similar to Shinko S1245!

  ************************************************************************

  This command is unique to the 6850:

->  03 1b 43 48 43 4c 00 00  00 00 00 00 00 00 00 00  [???]
<-  [51 octets]

    01 01 43 48 43 4c 00 00  00 00 00 00 00 00 00 00 <-- Everything after this
    00 00 01 29 00 00 3b 0a  00 00 00 0e 00 03 02 90     line is the same as
    00 01 02 1d 03 00 00 00  00 01 00 01 00 00 00 00     the "status" resp.
    00 00 00

    01 00 43 48 43 4c 00 00  00 00 00 00 00 00 00 00
    00 00 00 01 00 00 b7 d3  00 00 00 5c 00 03 02 8c
    00 01 02 1c 00 00 00 00  00 01 00 01 00 00 00 00
    00 00 00

*/
