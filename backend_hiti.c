/*
 *   HiTi Photo Printer CUPS backend -- libusb-1.0 version
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

#define BACKEND hiti_backend

#include "backend_common.h"

/* Private structures */
struct hiti_cmd {
	uint8_t hdr;    /* 0xa5 */
	uint16_t len;   /* (BE) everything after this field, minimum 3, max 6 */
	uint8_t unk;    /* 0x50 */
	uint16_t cmd;   /* CMD_*  (BE) */
	uint8_t payload[];  /* 0-3 items */
};

/* Request Device Characteristics */
#define CMD_RDC_RS     0x0100 /* Request Summary */
#define CMD_RDC_ROC    0x0104 /* Request Option Characteristics XX */

/* Printer Configuratio Control */
#define CMD_PCC_RP     0x0301 /* Reset Printer (1 arg) XX */
#define CMD_PCC_STP    0x030F /* Set Target Printer (1 arg) XX */

/* Request Device Status */
#define CMD_RDS_RSS    0x0400 /* Request Status Summary */
#define CMD_RDS_RIS    0x0401 /* Request Input Status */
#define CMD_RDS_RIA    0x0403 /* Request Input Alert */
#define CMD_RDS_RJA    0x0405 /* Request Jam Alert */
#define CMD_RDS_ROIRA  0x0406 /* Request Operator Intervention Alert */
#define CMD_RDS_RW     0x0407 /* Request Warnings */
#define CMD_RDS_DSRA   0x0408 /* Request Device Serviced Alerts */
#define CMD_RDS_SA     0x040A /* Request Service Alerts */
#define CMD_RDS_RPS    0x040B /* Request Printer Statistics */
#define CMD_RDS_RSUS   0x040C /* Request Supplies Status */

/* Job Control */
#define CMD_JC_SJ      0x0500 /* Start Job (2 arg) XX */
#define CMD_JC_EJ      0x0501 /* End Job (2 arg) XX */
#define CMD_JC_QJC     0x0502 /* Query Job Completed (3 arg) XX */
#define CMD_JC_QQA     0x0503 /* Query Jobs Queued or Active (2 arg) XX */
#define CMD_JC_RSJ     0x0510 /* Resume Suspended Job (2 arg) XX */

/* Extended Read Device Characteristics */
#define CMD_ERDC_RS    0x8000 /* Request Summary */
#define CMD_ERDC_RCC   0x8001 /* Read Calibration Charcteristics */
#define CMD_ERDC_RPC   0x8005 /* Request Print Count (1 arg) XX */
#define CMD_ERDC_RLC   0x8006 /* Request LED calibration */
#define CMD_ERDC_RSN   0x8007 /* Read Serial Number (1 arg) */
#define CMD_ERDC_RPIDM 0x8009 /* Request PID and Model Code */
#define CMD_ERDC_RTLV  0x800E /* Request T/L Voltage */
#define CMD_ERDC_RRVC  0x800F /* Read Ribbon Vendor Code */

// 8008 seen in Windows Comm @ 3211  (0 len response)
// 8010 seen in Windows Comm @ 84 (14 len response)
// 8011 seen in Windows Comm @ 3369 (4 len response)
// 801c seen in Windows comm @ 3293 (6 len response, all zero..?)

/* Extended Format Data */
#define CMD_EFD_SF     0x8100 /* Sublimation Format */
#define CMD_EFD_CHS    0x8101 /* Color & Heating Setting (2 arg) */

/* Extended Page Control */
#define CMD_EPC_SP     0x8200 /* Start Page */
#define CMD_EPC_EP     0x8201 /* End Page */
#define CMD_EPC_SYP    0x8202 /* Start Yellow Plane */
#define CMD_EPC_SMP    0x8204 /* Start Magenta Plane */
#define CMD_EPC_SCP    0x8206 /* Start Cyan Plane */

/* Extended Send Data */
#define CMD_ESD_SEHT2  0x8303 /* Send Ext Heating Table (2 arg) XX */
#define CMD_ESD_SEHT   0x8304 /* Send Ext Heating Table XX */
#define CMD_ESD_SEPD   0x8309 /* Send Ext Print Data (2 arg) + struct */
#define CMD_ESD_SHPTC  0x830B /* Send Heating Parameters & Tone Curve XX (n arg) */
#define CMD_ESD_C_SHPTC  0x830C /* Send Heating Parameters & Tone Curve XX (n arg) */

/* Extended Flash/NVram */
#define CMD_EFM_RNV    0x8405 /* Read NVRam (1 arg) XX */
#define CMD_EFM_RD     0x8408 /* Read single location (2 arg) -- XXX RE */

/* Extended ??? */
#define CMD_EDM_CVD    0xE002 /* Common Voltage Drop Values (n arg) XX */
#define CMD_EDM_CPP    0xE023 /* Clean Paper Path (1 arg) XX */

/* CMD_ERDC_RCC */
struct hiti_calibration {
	uint8_t horiz;
	uint8_t vert;
} __attribute__((packed));

/* CMD_ERDC_RPIDM */
struct hiti_rpidm {
	uint16_t usb_pid;  /* BE */
	uint8_t  region;   /* See hiti_regions */
} __attribute__((packed));

/* CMD_EDRC_RS */
struct hiti_erdc_rs {      /* All are BIG endian */
	uint8_t  unk;      // 1e == 30, but struct is 29 length.
	uint16_t stride;   /* fixed at 0x0780/1920? Head width? */
	uint16_t dpi_cols; /* fixed at 300 */
	uint16_t dpi_rows; /* fixed at 300 */
	uint16_t cols;     /* 1844 for 6" media */
	uint16_t rows;     /* 1240 for 6x4" media */
	uint8_t  unk2[18];  // ff ff 4b 4b 4b 4b af 3c  4f 7b 19 08 5c 0a b4 64  af af
} __attribute__((packed));

#define PRINT_TYPE_4x6  0
#define PRINT_TYPE_5x7  2
#define PRINT_TYPE_6x8  3
#define PRINT_TYPE_6x9  6
#define PRINT_TYPE_6x9_2UP 7
#define PRINT_TYPE_5x3_5 8
#define PRINT_TYPE_6x4_2UP 9
#define PRINT_TYPE_6x2  10
#define PRINT_TYPE_5x7_2UP 11

/* CMD_EFD_SF */
struct hiti_efd_sf {
/*@0 */	uint8_t  mediaType; /* PRINT_TYPE_?? */
/*@1 */	uint16_t cols_res;  /* BE, always 300dpi */
/*@3 */	uint16_t rows_res;  /* BE, always 300dpi */
/*@5 */	uint16_t cols;      /* BE */
/*@7 */	uint16_t rows;      /* BE */
/*@9 */	 int8_t  rows_offset; /* Has to do with H_Offset calibration */
/*@10*/	 int8_t  cols_offset; /* Has to do wiwth V_Offset calibration */
/*@11*/	uint8_t  colorSeq;  /* always 0x87, but |= 0xc0 for matte. */
/*@12*/	uint8_t  copies;
/*@13*/	uint8_t  printMode; /* 0x08 baseline, |= 0x02 fine mode */
} __attribute__((packed));

/* CMD_ESD_SEPD -- Note it's different from the usual command flow */
struct hiti_extprintdata {
	uint8_t  hdr; /* 0xa5 */
	uint16_t len; /* 24bit data length (+8) in BE format, first two bytes */
	uint8_t  unk; /* 0x50 */
	uint16_t cmd; /* 0x8309, BE */
	uint8_t  lenb; /* LSB of length */
	uint16_t startLine;  /* Starting line number, BE */
	uint16_t numLines; /* Number of lines in block, BE, 3000 max. */
	uint8_t  payload[];  /* ie data length bytes */
} __attribute__((packed));

/* All multi-byte fields here are LE */
struct hiti_matrix {
/*@00*/	uint8_t  row0[16]; // all 00

/*@10*/	uint8_t  row1[6];  // 01 00 00 00 00 00
	uint16_t cuttercount;
	uint8_t  align_v;
	uint8_t  aligh_h;
	uint8_t  row1_2[6]; // all 00

/*@20*/	uint8_t  row2[16]; // no idea

/*@30*/	uint8_t  error_index0;  /* Value % 31 == NEWEST. Count back */
	uint8_t  errorcode[31];

/*@50*/	uint8_t  row5[16]; // all 00, except [8] which is a5.
/*@60*/	char     serno[16]; /* device serial number */

/*@70*/	uint16_t unclean_prints;
	uint16_t cleanat[15]; // XX Guess?

/*@90*/	uint16_t supply_motor;
	uint16_t take_motor;
	uint8_t row9[12]; // all 00 except last, which is 0xa5

/*@a0*/	uint16_t errorcount[31];
	uint8_t unk_rowd[2]; // seems to be 00 cc ?

/*@e0*/	uint16_t tpc_4x6;
	uint16_t tpc_5x7;
	uint16_t tpc_6x8;
	uint16_t tpc_6x9;
	uint8_t unk_rowe[8]; // all 00

/*@f0*/	uint16_t apc_4x6;
	uint16_t apc_5x7;
	uint16_t apc_6x8;
	uint16_t apc_6x9;
	uint8_t unk_rowf[4]; // all 00
	uint8_t tphv_a;
	uint8_t tphv_d;
	uint8_t unk_rowf2[2]; // all 00
/* @100 */
} __attribute__((packed));

struct hiti_jobhdr { /* based on p525l, others should be similar */
	uint8_t queuename[32];
	uint8_t octname[32];

	uint8_t zero_a[11];
	uint8_t unk_a[2]; // 6c 1b for 6x6, 6c 00 for others
	uint8_t zero_b[7];
	uint8_t unk_b[4]; // 06 00 05 00
	uint8_t zero_c[8];

	uint8_t unk_c[8]; // 64 00 00 00 00 00 64 00
	uint8_t matte;    // 01 for enabled, 00 for disabled.
	uint8_t unk_d[11]; // 00 06 00 05 00 06 00 05 00 01 00
	uint8_t zero_d[4];
	uint8_t unk_e[2]; // 05 00

	uint8_t zero_e[114];

	uint8_t unk_f[2]; // 32 00 for 6", 00 00 for 5"
	uint8_t unk_g[2]; // 64 00
	uint8_t zero_g[24];

	uint8_t unk_h[8]; // 02 00 03 00 01 01 00 05
	uint8_t unk_i[2]; // 00 00 on 6", 02 00 on 5"  (ribbon code?)
	uint8_t zero_i[136];

	uint8_t printsize; // 00 = 6x4, 09 = 6x9, 0b = 5x7, 0d = 6x4-cut2, 14 = 6x6
	uint8_t unk_k[5]; // 02 01 00 01 00
	uint8_t copies;
	uint8_t unk_l[5]; // 00 00 00 03 01
	uint16_t rows;  // LE!
	uint16_t cols;  // LE!
/* 0x1a2 */
	uint8_t unk_m[10]; // 2d 01 01 00 00 00 00 00 01 00
} __attribute__((packed)); // 436 bytes

/* Private data structure */
struct hiti_printjob {
	uint8_t *databuf;
	uint32_t datalen;

	struct hiti_jobhdr hdr;

	int blocks;

	int copies;
};

struct hiti_ctx {
	struct libusb_device_handle *dev;
	uint8_t endp_up;
	uint8_t endp_down;
	int type;
	int jobid;

	struct marker marker;
	char     version[256];
	char     id[256];
	uint8_t  matrix[256];  // XXX convert to struct matrix */
	uint8_t  supplies[5];  /* Ribbon */ // XXX convert to struct
	uint8_t  supplies2[4]; /* Paper */  // XXX convert to struct
	struct hiti_calibration calibration;
	uint8_t  led_calibration[10]; // XXX convert to struct
	struct hiti_erdc_rs erdc_rs;
	uint8_t  rtlv[2];      /* XXX figure out conversion/math? */
	struct hiti_rpidm rpidm;
	uint16_t ribbonvendor; // low byte = media subtype, high byte = type.
	uint32_t media_remain; // XXX could be array?
};

/* Prototypes */
static int hiti_query_status(struct hiti_ctx *ctx, uint8_t *sts, uint16_t *err);
static int hiti_query_version(struct hiti_ctx *ctx);
static int hiti_query_matrix(struct hiti_ctx *ctx);
static int hiti_query_supplies(struct hiti_ctx *ctx);
static int hiti_query_tphv(struct hiti_ctx *ctx);
static int hiti_query_statistics(struct hiti_ctx *ctx);
static int hiti_query_calibration(struct hiti_ctx *ctx);
static int hiti_query_led_calibration(struct hiti_ctx *ctx);
static int hiti_query_ribbonvendor(struct hiti_ctx *ctx);
static int hiti_query_summary(struct hiti_ctx *ctx, struct hiti_erdc_rs *rds);
static int hiti_query_rpidm(struct hiti_ctx *ctx);

static int hiti_query_markers(void *vctx, struct marker **markers, int *count);

static int hiti_docmd(struct hiti_ctx *ctx, uint16_t cmdid, uint8_t *buf, uint16_t buf_len, uint16_t *rsplen)
{
	uint8_t cmdbuf[sizeof(struct hiti_cmd)];
	struct hiti_cmd *cmd = (struct hiti_cmd *)cmdbuf;
	int ret, num = 0;

	buf_len += 3;

	cmd->hdr = 0xa5;
	cmd->len = cpu_to_be16(buf_len);
	cmd->unk = 0x50;
	cmd->cmd = cpu_to_be16(cmdid);
	if (buf && buf_len)
		memcpy(cmd->payload, buf, buf_len);

	/* Send over command */
	if ((ret = send_data(ctx->dev, ctx->endp_down, (uint8_t*) cmd, 3 + cmd->len))) {
		return ret;
	}

	/* Read back command */
	ret = read_data(ctx->dev, ctx->endp_up, cmdbuf, 6, &num);
	if (ret)
		return ret;

	if (num != 6) {
		ERROR("CMD Readback length mismatch (%d vs %d)!\n", num, 6);
		return CUPS_BACKEND_FAILED;
	}

	/* Compensate for hdr len */
	*rsplen = cmd->len - 3;

	return CUPS_BACKEND_OK;
}

static int hiti_docmd_resp(struct hiti_ctx *ctx, uint16_t cmdid,
			   uint8_t *buf, uint8_t buf_len,
			   uint8_t *respbuf, uint16_t *resplen)
{
	int ret, num = 0;
	uint16_t cmd_resp_len = 0;

	ret = hiti_docmd(ctx, cmdid, buf, buf_len, &cmd_resp_len);
	if (ret)
		return ret;


	if (cmd_resp_len > *resplen) {
		ERROR("Response too long! (%d vs %d)\n", cmd_resp_len, *resplen);
		*resplen = 0;
		return CUPS_BACKEND_FAILED;
	}

	/* Read back the data*/
	ret = read_data(ctx->dev, ctx->endp_up, respbuf, *resplen, &num);
	if (ret)
		return ret;

	/* Sanity check */
	if (num != *resplen) {
		ERROR("Length mismatch (%d vs %d)!\n", num, *resplen);
		*resplen = 0;
		return CUPS_BACKEND_FAILED;
	}

	*resplen = num;

	return CUPS_BACKEND_OK;
}

static int hiti_sepd(struct hiti_ctx *ctx, uint32_t buf_len,
		     uint16_t startLine, uint16_t numLines)
{
	uint8_t cmdbuf[sizeof(struct hiti_extprintdata)];
	struct hiti_extprintdata *cmd = (struct hiti_extprintdata *)cmdbuf;
	int ret, num = 0;

	buf_len += 8;

	cmd->hdr = 0xa5;
	cmd->len = cpu_to_be16(buf_len >> 8);
	cmd->unk = 0x50;
	cmd->cmd = cpu_to_be16(CMD_ESD_SEPD);
	cmd->lenb = buf_len & 0xff;
	cmd->startLine = cpu_to_be16(startLine);
	cmd->numLines = cpu_to_be16(numLines);

	/* Send over command */
	if ((ret = send_data(ctx->dev, ctx->endp_down, (uint8_t*) cmd, sizeof(*cmd)))) {
		return ret;
	}

	/* Read back command */
	ret = read_data(ctx->dev, ctx->endp_up, cmdbuf, 6, &num);
	if (ret)
		return ret;

	if (num != 6) {
		ERROR("CMD Readback length mismatch (%d vs %d)!\n", num, 6);
		return CUPS_BACKEND_FAILED;
	}

	return CUPS_BACKEND_OK;
}

#define STATUS_IDLE          0x00
#define STATUS0_RESEND_DATA  0x04
#define STATUS0_BUSY         0x08
#define STATUS1_SUPPLIES     0x01
#define STATUS1_PAPERJAM     0x02
#define STATUS1_INPUT        0x08
#define STATUS2_WARNING      0x02
#define STATUS2_DEVSERVICE   0x04
#define STATUS2_OPERATOR     0x08

static const char *hiti_status(uint8_t *sts)
{
	if (sts[2] & STATUS2_WARNING)
		return "Warning";
	else if (sts[2] & STATUS2_DEVSERVICE)
		return "Service Required";
	else if (sts[2] & STATUS2_OPERATOR)
		return "Operator Intervention Required";
	else if (sts[1] & STATUS1_PAPERJAM)
		return "Paper Jam";
	else if (sts[1] & STATUS1_INPUT)
		return "Input Alert";
	else if (sts[1] & STATUS1_SUPPLIES)
		return "Supply Alert";
	else if (sts[0] & STATUS0_RESEND_DATA)
		return "Resend Data";
	else if (sts[0] & STATUS0_BUSY)
		return "Busy";
	else if (sts[0] == STATUS_IDLE)
		return "Idle";
	else
		return "Unknown";
}

#define RIBBON_TYPE_4x6    0x01
#define RIBBON_TYPE_5x7    0x02
#define RIBBON_TYPE_6x9    0x03
#define RIBBON_TYPE_6x8    0x04

static const char* hiti_ribbontypes(uint8_t code)
{
	switch (code) {
	case RIBBON_TYPE_4x6: return "4x6";
	case RIBBON_TYPE_5x7: return "5x7";
	case RIBBON_TYPE_6x9: return "6x9";
	case RIBBON_TYPE_6x8: return "6x8";
	default: return "Unknown";
	}
}

static int hiti_ribboncounts(uint8_t code)
{
	switch(code) {
	case RIBBON_TYPE_4x6: return 500;
	case RIBBON_TYPE_5x7: return 290;
	case RIBBON_TYPE_6x8: return 250;
	case RIBBON_TYPE_6x9: return 220; // XXX guess
	default: return 999;
	}
}

#define PAPER_TYPE_5INCH   0x02
#define PAPER_TYPE_6INCH   0x01
#define PAPER_TYPE_NONE    0x00

static const char* hiti_papers(uint8_t code)
{
	switch (code) {
	case PAPER_TYPE_NONE : return "None";
	case PAPER_TYPE_5INCH: return "5 inch";
	case PAPER_TYPE_6INCH: return "6 inch";
	default: return "Unknown";
	}
}

static const char* hiti_regions(uint8_t code)
{
	switch (code) {
	case 0x11: return "GB";
	case 0x12:
	case 0x22: return "CN";
	case 0x13: return "NA";
	case 0x14: return "SA";
	case 0x15: return "EU";
	case 0x16: return "IN";
	case 0x17: return "DB";
	default:
		return "Unknown";
	}
}

/* Supposedly correct for P720, P728, and P520 */
static const char *hiti_errors(uint8_t code)
{
	switch(code) {
	case 0x10: return "Cover open";
	case 0x11: return "Cover open failure";
	case 0x20: return "Ribbon IC missing";
	case 0x21: return "Ribbon missing";
	case 0x22: return "Ribbon mismatch 01";
	case 0x23: return "Security Check Fail";
	case 0x24: return "Ribbon mismatch 02";
	case 0x25: return "Ribbon mismatch 03";
	case 0x30: return "Ribbon out 01";
	case 0x31: return "Ribbon out 02";
	case 0x40: return "Paper out 01";
	case 0x41: return "Paper out 02";
	case 0x42: return "Paper not ready";
	case 0x50: return "Paper jam 01";
	case 0x51: return "Paper jam 02";
	case 0x52: return "Paper jam 03";
	case 0x53: return "Paper jam 04";
	case 0x54: return "Paper jam 05";
	case 0x60: return "Paper mismatch";
	case 0x70: return "Cam error 01";
	case 0x80: return "Cam error 02";
	case 0x90: return "NVRAM error";
	case 0xA0: return "IC error";
	case 0xC0: return "ADC error";
	case 0xD0: return "FW Check Error";
	case 0xF0: return "Cutter error";
	default: return "Unknown";
	}
}

static int hiti_get_info(struct hiti_ctx *ctx)
{
	int ret;

	INFO("Printer ID: %s\n",
	     ctx->id);
	INFO("Printer Version: %s\n",
	     ctx->version);
	INFO("Calibration:  H: %d V: %d\n", ctx->calibration.horiz, ctx->calibration.vert);
	INFO("LED Calibration: %d %d %d / %d %d %d\n",
	     ctx->led_calibration[4], ctx->led_calibration[5],
	     ctx->led_calibration[6], ctx->led_calibration[7],
	     ctx->led_calibration[8], ctx->led_calibration[9]);
	INFO("TPH Voltage (T/L): %d %d\n", ctx->rtlv[0], ctx->rtlv[1]);
	hiti_query_markers(ctx, NULL, NULL);
	INFO("Region: %s (%02x)\n",
	     hiti_regions(ctx->rpidm.region),
		ctx->rpidm.region);

	ret = hiti_query_summary(ctx, &ctx->erdc_rs);
	if (ret)
		return CUPS_BACKEND_FAILED;

	INFO("Status Summary: %d %dx%d %dx%d\n",
	     ctx->erdc_rs.stride,
	     ctx->erdc_rs.cols,
	     ctx->erdc_rs.rows,
	     ctx->erdc_rs.dpi_cols,
	     ctx->erdc_rs.dpi_rows);

	ret = hiti_query_matrix(ctx);
	if (ret)
		return CUPS_BACKEND_FAILED;

	int i;

	DEBUG("MAT ");
	for (i = 0 ; i < 256 ; i++) {
		if (i != 0 && (i % 16 == 0)) {
			DEBUG2("\n");
			DEBUG("    ");
		}
		DEBUG2("%02x ", ctx->matrix[i]);
	}
	DEBUG2("\n");

	// XXX other shit..
	// Serial number?

	return CUPS_BACKEND_OK;
}

static int hiti_get_status(struct hiti_ctx *ctx)
{
	uint8_t sts[3];
	uint16_t err = 0;
	int ret;

	ret = hiti_query_status(ctx, sts, &err);
	if (ret)
		return ret;

	INFO("Printer Status: %s %s (%02x %02x %02x %04x)\n",
	     hiti_status(sts), hiti_errors(err), sts[0], sts[1], sts[2], err);

	INFO("Media: %s (%02x / %04x) : %03d/%03d\n",
	     hiti_ribbontypes(ctx->supplies[2]),
	     ctx->supplies[2],
	     ctx->ribbonvendor,
	     ctx->media_remain, hiti_ribboncounts(ctx->supplies[2]));
	INFO("Paper: %s (%02x)\n",
	     hiti_papers(ctx->supplies2[1]),
	     ctx->supplies2[1]);

	// XXX other shit..
	// Jobs Queued Active

	return CUPS_BACKEND_OK;
}

static void *hiti_init(void)
{
	struct hiti_ctx *ctx = malloc(sizeof(struct hiti_ctx));
	if (!ctx) {
		ERROR("Memory Allocation Failure!\n");
		return NULL;
	}
	memset(ctx, 0, sizeof(struct hiti_ctx));

	return ctx;
}

extern struct dyesub_backend hiti_backend;

static int hiti_attach(void *vctx, struct libusb_device_handle *dev, int type,
			    uint8_t endp_up, uint8_t endp_down, uint8_t jobid)
{
	struct hiti_ctx *ctx = vctx;
	int ret;

	ctx->dev = dev;
	ctx->endp_up = endp_up;
	ctx->endp_down = endp_down;
	ctx->type = type;
	ctx->jobid = jobid;

	/* Ensure jobid is sane */
	ctx->jobid = (jobid & 0x7fff);
	if (!ctx->jobid)
		ctx->jobid++;

	if (test_mode < TEST_MODE_NOATTACH) {
		ret = hiti_query_version(ctx);
		if (ret)
			return ret;
		ret = hiti_query_supplies(ctx);
		if (ret)
			return ret;
		ret = hiti_query_calibration(ctx);
		if (ret)
			return ret;
		ret = hiti_query_led_calibration(ctx);
		if (ret)
			return ret;
		ret = hiti_query_ribbonvendor(ctx);
		if (ret)
			return ret;
		ret = hiti_query_rpidm(ctx);
		if (ret)
			return ret;
		ret = hiti_query_tphv(ctx);
		if (ret)
			return ret;

		// Query Serial Number (?)
		// do real stuff
	} else {
		if (getenv("MEDIA_CODE")) {
			// set fake fw version?
			ctx->supplies[2] = RIBBON_TYPE_4x6;
		}
	}

	ctx->marker.color = "#00FFFF#FF00FF#FFFF00";
	ctx->marker.name = hiti_ribbontypes(ctx->supplies[2]);
	ctx->marker.levelmax = hiti_ribboncounts(ctx->supplies[2]);
	ctx->marker.levelnow = 0;

	return CUPS_BACKEND_OK;
}

static void hiti_cleanup_job(const void *vjob) {
	const struct hiti_printjob *job = vjob;

	if (job->databuf)
		free(job->databuf);

	free((void*)job);
}

#define CORRECTION_FILE_SIZE (33*33*33*3 + 2)

static const uint8_t *hiti_get_correction_data(struct hiti_ctx *ctx)
{
	char *fname;
	uint8_t *buf;
	int ret, len;

	int mode = 0; // XXX 0 = standard, 1 = fine
	int mediaver = ctx->ribbonvendor & 0x3f;
	int mediatype = ((ctx->ribbonvendor & 0xf000) == 0x1000);

	switch (ctx->type)
	{
	case P_HITI_52X:
		fname = "P52x_CCPPri.bin";
		break;
	case P_HITI_72X:
		if (!mediatype) {
			if (mode) {
				fname = "P72x_CMQPrd.bin";
				break;
			} else {
				fname = "P72x_CMPPrd.bin";
				break;
			}
		} else {
			if (mode) {
				switch(mediaver) {
				case 0:
					fname = "P72x_CCQPrd.bin";
					break;
				case 1:
					fname = "P72x_CCQP1rd.bin";
					break;
				case 2:
					fname = "P72x_CCQP2rd.bin";
					break;
				case 3:
					fname = "P72x_CCQP3rd.bin";
					break;
				case 4:
				default:
					fname = "P72x_CCQP4rd.bin";
				break;
				}
			} else {
				switch(mediaver) {
				case 0:
					fname = "P72x_CCPPrd.bin";
					break;
				case 1:
					fname = "P72x_CCPP1rd.bin";
					break;
				case 2:
					fname = "P72x_CCPP2rd.bin";
					break;
				case 3:
					fname = "P72x_CCPP3rd.bin";
					break;
				case 4:
				default:
					fname = "P72x_CCPP4rd.bin";
					break;
				}
			}
		}
		break;
	case P_HITI_75X:
		fname = "P75x_CCPPri.bin";
		break;
	default:
		fname = NULL;
		break;
	}
	if (!fname)
		return NULL;

	buf = malloc(CORRECTION_FILE_SIZE);
	if (!buf) {
		WARNING("Memory allocation failure!\n");
		return NULL;
	}

	ret = dyesub_read_file(fname, buf, CORRECTION_FILE_SIZE, &len);
	if (ret) {
		free(buf);
		return NULL;
	}
	if (len != CORRECTION_FILE_SIZE) {
		WARNING("Read len mismatch\n");
		free(buf);
		return NULL;
	}

	return buf;
}

#define MAX_JOB_LEN (1844*2730*3+10480)

static int hiti_read_parse(void *vctx, const void **vjob, int data_fd, int copies)
{
	struct hiti_ctx *ctx = vctx;
	struct hiti_printjob *job = NULL;

	if (!ctx)
		return CUPS_BACKEND_FAILED;

	job = malloc(sizeof(*job));
	if (!job) {
		ERROR("Memory allocation failure!\n");
		return CUPS_BACKEND_RETRY_CURRENT;
	}
	memset(job, 0, sizeof(*job));
	job->copies = copies;

	/* Allocate a buffer */
	job->datalen = 0;
	job->databuf = malloc(MAX_JOB_LEN);
	if (!job->databuf) {
		ERROR("Memory allocation failure!\n");
		hiti_cleanup_job(job);
		return CUPS_BACKEND_RETRY_CURRENT;
	}

	uint32_t job_size = 0;

	/* Read in data */
	while (1) {
		uint32_t remain;
		uint16_t blocktype;
		uint8_t blocksubtype;

		int i;

		/* Read in block header */
		i = read(data_fd, job->databuf + job->datalen, 5);
		/* If we're done, we're done! */
		if (i == 0)
			break;
		if (i < 0) {
			hiti_cleanup_job(job);
			return CUPS_BACKEND_CANCEL;
		}
		if (job->databuf[job->datalen] != 0xa6) {
			ERROR("Bad header @ %d\n", job->datalen);
			hiti_cleanup_job(job);
			return CUPS_BACKEND_CANCEL;
		}
		memcpy(&remain, job->databuf + job->datalen + 1, sizeof(remain));
		remain = be32_to_cpu(remain);
		if (remain < 2) {
			ERROR("Bad block length %d\n", remain);
			hiti_cleanup_job(job);
			return CUPS_BACKEND_CANCEL;
		}
		job->datalen += 5;

		/* Read in block sub-header */
		i = read(data_fd, job->databuf + job->datalen, 3);
		if (i < 0) {
			hiti_cleanup_job(job);
			return CUPS_BACKEND_CANCEL;
		}
		memcpy(&blocktype, job->databuf + job->datalen, sizeof(blocktype));
		blocksubtype = job->databuf[job->datalen + 2];
		blocktype = be16_to_cpu(blocktype);

		DEBUG("Block len %05d type %04x %02x @%d\n", remain - 3, blocktype, blocksubtype, job->datalen - 5);

		remain -= 3;
		job->datalen += 3;

		uint32_t blocklen = remain;

		/* Read in block */
		while (remain) {
			i = read(data_fd, job->databuf + job->datalen, remain);
			if (i < 0) {
				hiti_cleanup_job(job);
				return CUPS_BACKEND_CANCEL;
			}
			remain -= i;
			job->datalen += i;
		}

		switch (blocktype) {
		case 0x40a1: /* Job header */
			if (blocklen != sizeof(job->hdr)) {
				ERROR("Header mismatch (%d vs %ld)\n", blocklen, sizeof(job->hdr));
				hiti_cleanup_job(job);
				return CUPS_BACKEND_CANCEL;
			}
			memcpy(&job->hdr, job->databuf + job->datalen - blocklen, blocklen);
			job->datalen -= (blocklen + 3 + 5);  /* Rewind the entire block */
			break;
		case 0x40a2: /* Page header/footer?  (subtype 00/01 respectively) */
			break;
		case 0x40a3: /* Image data.  Subtype matters, see end! */
			job->blocks++;
			break;
		}
	}
	DEBUG("Job data blocks: %d (%d)\n", job->blocks, job_size);

	/* Byteswap header fields! */
	job->hdr.rows = le16_to_cpu(job->hdr.rows);
	job->hdr.cols = le16_to_cpu(job->hdr.cols);

	// By the time we get here, job->databuf/datalen is
	// just the BGR image data!
	// XXX We are nowhere near ready yet!

	exit(CUPS_BACKEND_OK);

	// XXX sanity check job against ribbon and paper

	/* Load up correction data */
	const uint8_t *corrdata = hiti_get_correction_data(ctx);

	/* Convert input packed BGR data into YMC planar */
	{
		int rowlen = ((job->hdr.rows * 4) + 3) / 4;
		uint8_t *ymcbuf = malloc(job->hdr.cols * rowlen * 3);
		if (!ymcbuf) {
			hiti_cleanup_job(job);
			ERROR("Memory Allocation Failure!\n");
			return CUPS_BACKEND_FAILED;
		}
		uint8_t *dstY = ymcbuf;
		uint8_t *dstM = ymcbuf + (rowlen * job->hdr.cols);
		uint8_t *dstC = ymcbuf + (rowlen * job->hdr.cols * 2);
		uint32_t i;

		for (i = 0 ; i < job->datalen ; i+= 3) {
			uint8_t B, G, R;

			B = job->databuf[i];
			G = job->databuf[i+1];
			R = job->databuf[i+2];

			if (corrdata) {
				// XXX if we have a mapping table,
				// run it through conversion here.
			}

			/* Finally convert to YMC */
			*dstY++ = 255 - B;
			*dstM++ = 255 - G;
			*dstC++ = 255 - R;
		}
		/* Nuke the old BGR buffer and replace it with YMC */
		free(job->databuf);
		job->databuf = ymcbuf;
		job->datalen = rowlen * 3 * job->hdr.cols;
	}

	*vjob = job;

	return CUPS_BACKEND_OK;
}

static int calc_offset(int val, int mid, int max, int step)
{
	if (val > max)
		val = max;
	else if (val < 0)
		val = 0;

	val -= mid;
	val *= step;

	return step;
}

static int hiti_main_loop(void *vctx, const void *vjob)
{
	struct hiti_ctx *ctx = vctx;

	int ret;
	int copies;

	const struct hiti_printjob *job = vjob;

	if (!ctx)
		return CUPS_BACKEND_FAILED;
	if (!job)
		return CUPS_BACKEND_FAILED;

	copies = job->copies;

top:
	INFO("Waiting for printer idle\n");

	do {
		uint8_t sts[3];
		uint16_t err = 0;

		ret = hiti_query_status(ctx, sts, &err);
		if (ret)
			return ret;

		/* If we're idle, proceed */
		if (!sts[2] && !sts[1]) {
			if (!sts[0])
				break;
			// query active jobs, make sure we're not colliding?
		}
		sleep(1);
	} while(1);

	dump_markers(&ctx->marker, 1, 0);

	uint16_t resplen = 0;
	uint16_t rows = job->hdr.rows;
	uint16_t cols = ((4*job->hdr.cols) + 3) / 4;

	// XXX these two only need to change if rows > 3000
	uint16_t startLine = 0;
	uint16_t numLines = rows;

	uint32_t sent = 0;

	/* Set up and send over Sublimation Format */
	struct hiti_efd_sf sf;
	sf.mediaType = 0; // XXX PRINT_TYPE_??
	sf.cols_res = sf.rows_res = cpu_to_be16(300);
	sf.cols = cpu_to_be16(job->hdr.cols);
	sf.rows = cpu_to_be16(rows);
	sf.rows_offset = calc_offset(ctx->calibration.vert, 5, 8, 4);
	sf.cols_offset = calc_offset(ctx->calibration.horiz, 6, 11, 4);
	sf.colorSeq = 0x87 + (job->hdr.matte ? 0xc0 : 0);
	sf.printMode = 0x08; // fine |= 0x02
	ret = hiti_docmd(ctx, CMD_EFD_SF, (uint8_t*) &sf, sizeof(sf), &resplen);
	if (ret)
		return CUPS_BACKEND_FAILED;

	// CMD_JC_SJ   // start job (w/ jobid)

	uint8_t chs[2] = { 0, 1 }; /* Fixed..? */
	ret = hiti_docmd(ctx, CMD_EFD_CHS, chs, sizeof(chs), &resplen);
	if (ret)
		return CUPS_BACKEND_CANCEL;
	ret = hiti_docmd(ctx, CMD_EPC_SP, NULL, 0, &resplen);
	if (ret)
		return CUPS_BACKEND_FAILED;

	// CMD_ESD_SHPTC // Heating Parameters & Tone Curve (~7Kb, seen on windows..)
	INFO("Sending yellow plane\n");
	ret = hiti_docmd(ctx, CMD_EPC_SYP, NULL, 0, &resplen);
	if (ret)
		return CUPS_BACKEND_FAILED;
	ret = hiti_sepd(ctx, rows * cols, startLine, numLines);
	if (ret)
		return CUPS_BACKEND_FAILED;
	ret = send_data(ctx->dev, ctx->endp_down, job->databuf + sent, rows * cols);
	if (ret)
		return CUPS_BACKEND_FAILED;
	sent += rows * cols;

	INFO("Sending magenta plane\n");
	ret = hiti_docmd(ctx, CMD_EPC_SMP, NULL, 0, &resplen);
	if (ret)
		return CUPS_BACKEND_FAILED;
	ret = hiti_sepd(ctx, rows * cols, startLine, numLines);
	if (ret)
		return CUPS_BACKEND_FAILED;
	ret = send_data(ctx->dev, ctx->endp_down, job->databuf + sent, rows * cols);
	if (ret)
		return CUPS_BACKEND_FAILED;
	sent += rows * cols;

	INFO("Sending cyan plane\n");
	ret = hiti_docmd(ctx, CMD_EPC_SCP, NULL, 0, &resplen);
	if (ret)
		return CUPS_BACKEND_FAILED;
	ret = hiti_sepd(ctx, rows * cols, startLine, numLines);
	if (ret)
		return CUPS_BACKEND_FAILED;
	ret = send_data(ctx->dev, ctx->endp_down, job->databuf + sent, rows * cols);
	if (ret)
		return CUPS_BACKEND_FAILED;
	sent += rows * cols;
	ret = hiti_docmd(ctx, CMD_EPC_EP, NULL, 0, &resplen);
	if (ret)
		return CUPS_BACKEND_FAILED;

	// CMD_JC_EJ // end job

	INFO("Waiting for printer acknowledgement\n");
	do {
		uint8_t sts[3];
		uint16_t err = 0;

		sleep(1);

		ret = hiti_query_status(ctx, sts, &err);
		if (ret)
			return ret;

		/* If we're idle, we're done. */
		if (!sts[2] && !sts[1] && !sts[0]) {
			break;
		}

		// XXX query job ID?
		// if job completed, break;

		if (fast_return) {
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

static int hiti_cmdline_arg(void *vctx, int argc, char **argv)
{
	struct hiti_ctx *ctx = vctx;
	int i, j = 0;

	if (!ctx)
		return -1;

	while ((i = getopt(argc, argv, GETOPT_LIST_GLOBAL "is")) >= 0) {
		switch(i) {
		GETOPT_PROCESS_GLOBAL
		case 'i':
			hiti_get_info(ctx);
			break;
		case 's':
			hiti_get_status(ctx);
			break;
		}

		if (j) return j;
	}

	return 0;
}

static void hiti_cmdline(void)
{
	DEBUG("\t\t[ -i ]           # Query printer information\n");
	DEBUG("\t\t[ -s ]           # Query printer status\n");
	// XXX add support for RESET.
}

static int hiti_query_version(struct hiti_ctx *ctx)
{
	int ret;
	uint16_t len = 79;
	uint8_t buf[256];

	ret = hiti_docmd_resp(ctx, CMD_RDC_RS, NULL, 0, buf, &len);
	if (ret)
		return ret;

	/* Copy strings */
	strncpy(ctx->id, (char*) &buf[34], buf[33]);
	strncpy(ctx->version, (char*) &buf[34 + buf[33] + 1], sizeof(ctx->version));

	return CUPS_BACKEND_OK;
}

static int hiti_query_status(struct hiti_ctx *ctx, uint8_t *sts, uint16_t *err)
{
	int ret;
	uint16_t len = 3;
	uint16_t cmd;

	ret = hiti_docmd_resp(ctx, CMD_RDS_RSS, NULL, 0, sts, &len);
	if (ret)
		return ret;

	if (sts[2] & STATUS2_WARNING)
		cmd = CMD_RDS_RW;
	else if (sts[2] & STATUS2_DEVSERVICE)
		cmd = CMD_RDS_DSRA;
	else if (sts[2] & STATUS2_OPERATOR)
		cmd = CMD_RDS_ROIRA;
	else if (sts[1] & STATUS1_PAPERJAM)
		cmd = CMD_RDS_RJA;
	else if (sts[1] & STATUS1_INPUT)
		cmd = CMD_RDS_RIA;
	else if (sts[1] & STATUS1_SUPPLIES)
		cmd = CMD_RDS_SA;
	else
		cmd = 0;

	/* Query extended status, if needed */
	if (cmd) {
		uint8_t respbuf[16];
		len = sizeof(respbuf);

		ret = hiti_docmd_resp(ctx, CMD_RDS_RSS, NULL, 0, respbuf, &len);
		if (ret)
			return ret;

		if (respbuf[0]) { // error count
			memcpy(err, &respbuf[1], sizeof(*err)); // error code, BE
			*err = be16_to_cpu(*err);
			if (len > 8) { // OPTIONAL
				// 5-8 is ERRSTATE in ASCII HEX!
			}
		}
	}

	return CUPS_BACKEND_OK;
}

static int hiti_query_summary(struct hiti_ctx *ctx, struct hiti_erdc_rs *rds)
{
	int ret;
	uint16_t len = sizeof(*rds);

	ret = hiti_docmd_resp(ctx, CMD_ERDC_RS, NULL, 0, (uint8_t*)rds, &len);
	if (ret)
		return ret;

	return CUPS_BACKEND_OK;
}

static int hiti_query_rpidm(struct hiti_ctx *ctx)
{
	int ret;
	uint16_t len = sizeof(ctx->rpidm);

	ret = hiti_docmd_resp(ctx, CMD_ERDC_RPIDM, NULL, 0, (uint8_t*)&ctx->rpidm, &len);
	if (ret)
		return ret;

	ctx->rpidm.usb_pid = be16_to_cpu(ctx->rpidm.usb_pid);

	return CUPS_BACKEND_OK;
}

static int hiti_query_calibration(struct hiti_ctx *ctx)
{
	int ret;
	uint16_t len = sizeof(ctx->calibration);

	ret = hiti_docmd_resp(ctx, CMD_ERDC_RCC, NULL, 0, (uint8_t*)&ctx->calibration, &len);
	if (ret)
		return ret;

	return CUPS_BACKEND_OK;
}

static int hiti_query_led_calibration(struct hiti_ctx *ctx)
{
	int ret;
	uint16_t len = sizeof(ctx->led_calibration);

	ret = hiti_docmd_resp(ctx, CMD_ERDC_RLC, NULL, 0, (uint8_t*)&ctx->led_calibration, &len);
	if (ret)
		return ret;

	return CUPS_BACKEND_OK;
}

static int hiti_query_ribbonvendor(struct hiti_ctx *ctx)
{
	int ret;
	uint16_t len = 2;

	ret = hiti_docmd_resp(ctx, CMD_ERDC_RRVC, NULL, 0, (uint8_t*) &ctx->ribbonvendor, &len);
	if (ret)
		return ret;

	ctx->ribbonvendor = cpu_to_le16(ctx->ribbonvendor);

	return CUPS_BACKEND_OK;
}

static int hiti_query_tphv(struct hiti_ctx *ctx)
{
	int ret;
	uint16_t len = 2;

	ret = hiti_docmd_resp(ctx, CMD_ERDC_RTLV, NULL, 0, (uint8_t*) &ctx->rtlv, &len);
	if (ret)
		return ret;

	ctx->ribbonvendor = cpu_to_le32(ctx->ribbonvendor);

	return CUPS_BACKEND_OK;
}

static int hiti_query_supplies(struct hiti_ctx *ctx)
{
	int ret;
	uint16_t len = 5;
	uint8_t arg = 0;

	ret = hiti_docmd_resp(ctx, CMD_RDS_RSUS, &arg, sizeof(arg), ctx->supplies, &len);
	if (ret)
		return ret;

	len = 4;
	ret = hiti_docmd_resp(ctx, CMD_RDS_RIS, &arg, sizeof(arg), ctx->supplies2, &len);
	if (ret)
		return ret;

	return CUPS_BACKEND_OK;
}

static int hiti_query_statistics(struct hiti_ctx *ctx)
{
	int ret;
	uint16_t len = 6;
	uint8_t buf[256];

	ret = hiti_docmd_resp(ctx, CMD_RDS_RPS, NULL, 0, buf, &len);
	if (ret)
		return ret;

	memcpy(&ctx->media_remain, &buf[2], sizeof(ctx->media_remain));
	ctx->media_remain = be32_to_cpu(ctx->media_remain);

	return CUPS_BACKEND_OK;
}

static int hiti_query_matrix(struct hiti_ctx *ctx)
{
	int ret;
	int i;
	uint16_t len = 1;

	for (i = 0 ; i < 256 ; i++) {
		uint16_t offset = cpu_to_be16(i);

		ret = hiti_docmd_resp(ctx, CMD_EFM_RD, (uint8_t*)&offset, sizeof(offset), &ctx->matrix[i], &len);
		if (ret)
			return ret;
	}


	return CUPS_BACKEND_OK;
}

static int hiti_query_serno(struct libusb_device_handle *dev, uint8_t endp_up, uint8_t endp_down, char *buf, int buf_len)
{
	int ret;
	uint16_t rsplen = 18;
	uint8_t rspbuf[18];

	struct hiti_ctx ctx = {
		.dev = dev,
		.endp_up = endp_up,
		.endp_down = endp_down,
	};

	uint8_t arg = sizeof(rspbuf);
	ret = hiti_docmd_resp(&ctx, CMD_ERDC_RSN, &arg, sizeof(arg), rspbuf, &rsplen);
	if (ret)
		return ret;

	/* Copy over serial number */
	strncpy(buf, (char*)rspbuf, buf_len);

	return CUPS_BACKEND_OK;
}

static int hiti_query_markers(void *vctx, struct marker **markers, int *count)
{
	struct hiti_ctx *ctx = vctx;
	int ret;

	if (markers)
		*markers = &ctx->marker;
	if (count)
		*count = 1;

	ret = hiti_query_statistics(ctx);
	if (ret)
		return ret;

	ctx->marker.levelnow = ctx->media_remain;

	return CUPS_BACKEND_OK;
}

static const char *hiti_prefixes[] = {
	"hiti", // Family name
	"hiti-p52x", "hiti-p520l", "hiti-p525l",
	NULL
};

/* Exported */
#define USB_VID_HITI         0x0d16
#define USB_PID_HITI_P52X    0x0502

struct dyesub_backend hiti_backend = {
	.name = "HiTi Photo Printers",
	.version = "0.02WIP",
	.uri_prefixes = hiti_prefixes,
	.cmdline_usage = hiti_cmdline,
	.cmdline_arg = hiti_cmdline_arg,
	.init = hiti_init,
	.attach = hiti_attach,
	.cleanup_job = hiti_cleanup_job,
	.read_parse = hiti_read_parse,
	.main_loop = hiti_main_loop,
	.query_serno = hiti_query_serno,
	.query_markers = hiti_query_markers,
	.devices = {
		{ USB_VID_HITI, USB_PID_HITI_P52X, P_HITI_52X, NULL, "hiti-p52x"},
		{ 0, 0, 0, NULL, NULL}
	}
};

/* HiTi spool file format

 File is organized into a series of blocks.  Each has this header:

  A6 LL LL LL LL         # LL is number of bytes that follow. (32-bit BE)

Blocks are organized as follows:

  40 II II <payload>

   Known block IDs:

	A1 xx   <Initial job header block>
	A2 xx   <Unknown, seen after header block and as final block>
	A3 xx  <Data chunk, presumably>

Block A1 xx:  <TBD/WIP>

   See struct hiti_jobhdr

Block A2 00:  START PAGE

 a6 00 00  00 13  40  a2 00

    01 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00

Block A2 01:  END PAGE

 a6 00 00  00 05  40  a2 01

    00 00  [jobid? pageid?]

Block A3 01:  ?? page setup? 1136 byte header, 48 byte payload.
Block A3 02:  ?? 12 byte header?
Block A3 04:  ?? 292 byte header
Block A3 05:  raw BGR bitmap, to be blitted over?  384 byte header.

*/
