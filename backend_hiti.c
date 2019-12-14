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

#define BACKEND hiti_backend

#include "backend_common.h"

/* For Integration into gutenprint */
#if defined(HAVE_CONFIG_H)
#include <config.h>
#endif

#ifndef CORRTABLE_PATH
#ifdef PACKAGE_DATA_DIR
#define CORRTABLE_PATH PACKAGE_DATA_DIR "/backend_data"
#else
#error "Must define CORRTABLE_PATH or PACKAGE_DATA_DIR!"
#endif
#endif

/* Private structures */
struct hiti_cmd {
	uint8_t hdr;    /* 0xa5 */
	uint16_t len;   /* (BE) everything after this field, minimum 3, max 6 */
	uint8_t unk;    /* 0x50 */
	uint16_t cmd;   /* CMD_*  (BE) */
	uint8_t payload[];  /* 0-3 items */
} __attribute__((packed));

/* Request Device Characteristics */
#define CMD_RDC_RS     0x0100 /* Request Summary */
#define CMD_RDC_ROC    0x0104 /* Request Option Characteristics XX (1 resp) */

/* Printer Configuratio Control */
#define CMD_PCC_RP     0x0301 /* Reset Printer (1 arg) */
#define CMD_PCC_STP    0x030F /* Set Target Printer (1 arg) XX -- master or slave perhaps? */

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
#define CMD_JC_SJ      0x0500 /* Start Job (3 arg) */
#define CMD_JC_EJ      0x0501 /* End Job (3 arg) */
#define CMD_JC_QJC     0x0502 /* Query Job Completed (5 arg) XX */
#define CMD_JC_QQA     0x0503 /* Query Jobs Queued or Active (3 arg) */
#define CMD_JC_RSJ     0x0510 /* Resume Suspended Job (3 arg) XX */

/* Extended Read Device Characteristics */
#define CMD_ERDC_RS    0x8000 /* Request Summary */
#define CMD_ERDC_RCC   0x8001 /* Read Calibration Charcteristics */
#define CMD_ERDC_RPC   0x8005 /* Request Print Count (1 arg, 4 resp) */
#define CMD_ERDC_RLC   0x8006 /* Request LED calibration */
#define CMD_ERDC_RSN   0x8007 /* Read Serial Number (1 arg) */
#define CMD_ERDC_C_RPCS 0x8008 /* CS Request Printer Correction Status */
#define CMD_ERDC_RPIDM 0x8009 /* Request PID and Model Code */
#define CMD_ERDC_RTLV  0x800E /* Request T/L Voltage */
#define CMD_ERDC_RRVC  0x800F /* Read Ribbon Vendor Code */
#define CMD_ERDC_UNK   0x8010 /* Unknown Query RE */
#define CMD_ERDC_RHA   0x801C /* Read Highlight Adjustment (6 resp) RE */

// 8008 seen in Windows Comm @ 3211  (0 len response)
// 8011 seen in Windows Comm @ 3369 (1 arg req (always 00), 4 len response)

/* Extended Format Data */
#define CMD_EFD_SF     0x8100 /* Sublimation Format */
#define CMD_EFD_CHS    0x8101 /* Color & Heating Setting (2 arg) */
#define CMD_EFD_C_CHS  0x8102 /* CS Color Heating Setting (3 arg) */
#define CMD_EFD_C_SIID 0x8103 /* CS Set Input ID (1 arg) */

/* Extended Page Control */
#define CMD_EPC_SP     0x8200 /* Start Page */
#define CMD_EPC_EP     0x8201 /* End Page */
#define CMD_EPC_SYP    0x8202 /* Start Yellow Plane */
#define CMD_EPC_SMP    0x8204 /* Start Magenta Plane */
#define CMD_EPC_SCP    0x8206 /* Start Cyan Plane */

#define CMD_EPC_C_SYP  0x8202 /* CS Start Yellow Page */
#define CMD_EPC_C_SMP  0x8203 /* CS Start Magenta Page */
#define CMD_EPC_C_SCP  0x8204 /* CS Start Cyan Page */
#define CMD_EPC_C_SBP  0x8205 /* CS Start Black Page */
#define CMD_EPC_C_SKP  0x8206 /* CS Start K Resin Page */
#define CMD_EPC_C_SLP  0x8207 /* CS Start Lamination Page */
#define CMD_EPC_C_SOP  0x8208 /* CS Start Overcoat Page */
#define CMD_EPC_C_SY2P 0x8209 /* CS Start Yellow2 Page */
#define CMD_EPC_C_SM2P 0x820A /* CS Start Magenta2 Page */
#define CMD_EPC_C_SC2P 0x820B /* CS Start Cyan2 Page */
#define CMD_EPC_C_SB2P 0x820C /* CS Start Black2 Page */
#define CMD_EPC_C_SK2P 0x820D /* CS Start K Resin2 Page */
#define CMD_EPC_C_SL2P 0x820E /* CS Start Lamination2 Page */
#define CMD_EPC_C_SO2P 0x820F /* CS Start Overcoat2 Page */

/* Extended Send Data */
#define CMD_ESD_SEHT2  0x8303 /* Send Ext Heating Table (2 arg) XX */
#define CMD_ESD_SEHT   0x8304 /* Send Ext Heating Table XX */
#define CMD_ESD_SEPD   0x8309 /* Send Ext Print Data (2 arg) + struct */
#define CMD_ESD_SHPTC  0x830B /* Send Heating Parameters & Tone Curve XX (n arg) */
#define CMD_ESD_C_SHPTC  0x830C /* CS Send Heating Parameters & Tone Curve XX (n arg) */

/* Extended Flash/NVram */
#define CMD_EFM_RNV    0x8405 /* Read NVRam (1 arg) XX */
#define CMD_EFM_RD     0x8408 /* Read single location (2 arg) -- XXX RE */
#define CMD_EFM_SHA    0x840E /* Set Highlight Adjustment (5 arg) -- XXX RE */

/* Extended Security Control */
#define CMD_ESC_SP     0x8900 /* Set Password */
#define CMD_ESC_SSM    0x8901 /* Set Security Mode */

/* Extended Debug Mode */
#define CMD_EDM_CVD    0xE002 /* Common Voltage Drop Values (n arg) XX */
#define CMD_EDM_CPP    0xE023 /* Clean Paper Path (1 arg) XX */
#define CMD_EDM_C_MC2CES 0xE02E /* CS Move card to Contact Encoder Station */
#define CMD_EDM_C_MC2MES 0xE02F /* CS Move card to Mag Encoder Station */
#define CMD_EDM_C_MC2CLES 0xE030 /* CS Move card to ContactLess Encoder Station */
#define CMD_EDM_C_MC2EB 0xE031 /* CS Move card to Eject Box */
#define CMD_EDM_C_MC2H 0xE037 /* CS Move card to Hopper */

/* CMD_PCC_RP */
#define RESET_PRINTER 0x01
#define RESET_SOFT    0x02

/* 801C --> 0 args
        <-- 6 bytes: 00 YY MM CC 00 00  (YMC is +- 31d)

   040E --> 5 args:  YY MM CC 00 00 (YMC is +- 31d)
        <-- 1 arg:   00 (success, presumably)

*/

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

/* CMD_JC_* */
struct hiti_job {
	uint8_t  lun;    /* Logical Unit Number.  Leave at 0 */
	uint16_t jobid;  /* BE */
} __attribute__((packed));

/* CMD_JC_QJC */
struct hiti_jc_qjc {
	uint8_t  lun;    /* Logical Unit Number.  Leave at 0 */
	uint16_t jobid;  /* BE */
	uint16_t jobid2; /* BE, set to 1? */
} __attribute__((packed));
// repsonse is 6 bytes.

//. 5x3.5 1547 1072
//. 6x4   1844 1240
//. 6x9   1844 2740
//. 6x8/2 1844 2492
//. 6x8   1844 2434
//. 5x7   1548 2140
//. 5x7/2 1548 2152
//. 6x4/2 1844 1248
// 6x6    1844 1844
// 5x5    1540 1540 ? (1548?)
// 6x5    1844 1544
// 6x2    1844 ????


#define PRINT_TYPE_6x4      0
#define PRINT_TYPE_5x7      2
#define PRINT_TYPE_6x8      3
#define PRINT_TYPE_6x9      6
#define PRINT_TYPE_6x9_2UP  7
#define PRINT_TYPE_5x3_5    8
#define PRINT_TYPE_6x4_2UP  9
#define PRINT_TYPE_6x2     10
#define PRINT_TYPE_5x7_2UP 11

/* All fields are LE */
struct hiti_gpjobhdr {
	uint32_t cookie;  /* "GPHT" */
	uint32_t hdr_len; /* Including the whole thing */
	uint32_t model;   /* in BCD..? */
	uint32_t cols;
	uint32_t rows;
	uint32_t col_dpi;
	uint32_t row_dpi;
	uint32_t copies;
	uint32_t quality;  /* 0 for std, 1 for fine */
	uint32_t code;     /* PRINT_TYPE_* */
	uint32_t overcoat; /* 1 for matte, 0 for glossy */
	uint32_t payload_type; // 0 for bgr packed. what about rgb, planar?
	uint32_t payload_len;
} __attribute__((packed));

#define PAYLOAD_TYPE_FLAG_NOCORRECT 0x02

#define HDR_COOKIE 0x54485047

/* CMD_EFD_SF for non-CS systems */
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

/* Private data structure */
struct hiti_printjob {
	uint8_t *databuf;
	uint32_t datalen;

	struct hiti_gpjobhdr hdr;

	int blocks;

	int copies;
};

struct hiti_ctx {
	struct libusb_device_handle *dev;
	uint8_t endp_up;
	uint8_t endp_down;
	int iface;
	int type;
	int jobid;

	char serno[32];

	struct marker marker;
	char     version[256];
	char     id[256];
	uint8_t  matrix[256];  // XXX convert to struct matrix */
	uint8_t  supplies[5];  /* Ribbon */ // XXX convert to struct
	uint8_t  supplies2[4]; /* Paper */  // XXX convert to struct
	struct hiti_calibration calibration;
	uint8_t  led_calibration[10]; // XXX convert to struct
	uint8_t  unk_8010[15]; // XXX
	struct hiti_erdc_rs erdc_rs;
	uint8_t  hilight_adj[6]; // XXX convert to struct
	uint8_t  rtlv[2];      /* XXX figure out conversion/math? */
	struct hiti_rpidm rpidm;
	uint16_t ribbonvendor; // low byte = media subtype, high byte = type.
	uint32_t media_remain; // XXX could be array?
};

/* Prototypes */
static int hiti_doreset(struct hiti_ctx *ctx, uint8_t type);
static int hiti_query_job_qa(struct hiti_ctx *ctx, struct hiti_job *jobid);
static int hiti_query_status(struct hiti_ctx *ctx, uint8_t *sts, uint32_t *err);
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
static int hiti_query_hilightadj(struct hiti_ctx *ctx);
static int hiti_query_unk8010(struct hiti_ctx *ctx);
static int hiti_query_counter(struct hiti_ctx *ctx, uint8_t arg, uint32_t *resp);
static int hiti_query_markers(void *vctx, struct marker **markers, int *count);

static int hiti_query_serno(struct libusb_device_handle *dev, uint8_t endp_up, uint8_t endp_down, int iface, char *buf, int buf_len);

static int hiti_docmd(struct hiti_ctx *ctx, uint16_t cmdid, uint8_t *buf, uint16_t buf_len, uint16_t *rsplen)
{
	uint8_t cmdbuf[2048];
	struct hiti_cmd *cmd = (struct hiti_cmd *)cmdbuf;
	int ret, num = 0;

	cmd->hdr = 0xa5;
	cmd->len = cpu_to_be16(buf_len + 3);
	cmd->unk = 0x50;
	cmd->cmd = cpu_to_be16(cmdid);
	if (buf && buf_len)
		memcpy(cmd->payload, buf, buf_len);

	/* Send over command */
	if ((ret = send_data(ctx->dev, ctx->endp_down, (uint8_t*) cmd, buf_len + 3 + 3))) {
		return ret;
	}

	usleep(10*1000);

	/* Read back command */
	ret = read_data(ctx->dev, ctx->endp_up, cmdbuf, 6, &num);
	if (ret)
		return ret;

	if (num != 6) {
		ERROR("CMD Readback length mismatch (%d vs %d)!\n", num, 6);
		return CUPS_BACKEND_FAILED;
	}

	/* Compensate for hdr len */
	num = be16_to_cpu(cmd->len) - 3;

	if (num > *rsplen) {
		ERROR("Response too long for buffer (%d vs %d)!\n", num, *rsplen);
		*rsplen = 0;
		return CUPS_BACKEND_FAILED;
	}

	*rsplen = num;

	return CUPS_BACKEND_OK;
}

static int hiti_docmd_resp(struct hiti_ctx *ctx, uint16_t cmdid,
			   uint8_t *buf, uint8_t buf_len,
			   uint8_t *respbuf, uint16_t *resplen)
{
	int ret, num = 0;
	uint16_t cmd_resp_len = *resplen;

	ret = hiti_docmd(ctx, cmdid, buf, buf_len, &cmd_resp_len);
	if (ret)
		return ret;

	if (cmd_resp_len > *resplen) {
		ERROR("Response too long! (%d vs %d)\n", cmd_resp_len, *resplen);
		*resplen = 0;
		return CUPS_BACKEND_FAILED;
	}

	usleep(10*1000);

	/* Read back the data*/
	ret = read_data(ctx->dev, ctx->endp_up, respbuf, *resplen, &num);
	if (ret)
		return ret;

	/* Sanity check */
	if (num > *resplen) {
		ERROR("Response too long for buffer (%d vs %d)!\n", num, *resplen);
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

	usleep(10*1000);

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
#define STATUS0_POWERON      0x01
#define STATUS0_RESEND_DATA  0x04
#define STATUS0_BUSY         0x80
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

static unsigned int hiti_ribboncounts(uint8_t code)
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
static const char *hiti_errors(uint32_t code)
{
	switch(code) {
	case 0x00000000: return "None";
		/* Warning Alerts */
	case 0x000100FE: return "Paper roll mismatch";
	case 0x000300FE: return "Buffer underrun when printing";
	case 0x000301FE: return "Command sequence error";
	case 0x000302FE: return "NAND flash unformatted";
	case 0x000303FE: return "NAND flash space insufficient";
	case 0x000304FE: return "Heating parameter table incompatible";
	case 0x000502FE: return "Dust box needs cleaning";
		/* Device Service Required Alerts */
	case 0x00030001: return "SRAM error";
	case 0x00030101: return "Cutter error";
	case 0x00030201: return "ADC error";
	case 0x00030301: return "NVRAM R/W error";
	case 0x00030302: return "SDRAM checksum error";
	case 0x00030402: return "DSP code checksum error";
	case 0x00030501: return "Cam TPH error";
	case 0x00030502: return "NVRAM checksom error";
	case 0x00030601: return "Cam pinch error";
	case 0x00030602: return "SRAM checksum error";
	case 0x00030701: return "Firmware write error";
	case 0x00030702: return "Flash checksum error";
	case 0x00030802: return "Wrong firmware checksum error";
	case 0x00030901: return "ADC error in slave printer";
	case 0x00030A01: return "Cam Platen error in slave printer";
	case 0x00030B01: return "NVRAM R/W error in slave printer";
	case 0x00030C02: return "NVRAM CRC error in slave printer";
	case 0x00030D02: return "SDRAM checksum error in slave printer";
	case 0x00030E02: return "SRAM checksum error in slave printer";
	case 0x00030F02: return "FLASH checksum error in slave printer";
	case 0x00031002: return "Wrong firmware checksum error in slave printer";
	case 0x00031101: return "Communication error with slave printer";
	case 0x00031201: return "NAND flash error";
	case 0x00031302: return "Cutter error";
		/* Operator Intervention Required Alerts */
	case 0x00050001: return "Cover open";
	case 0x00050101: return "Cover open";
		/* Supplies Alerts */
	case 0x00080004: return "Ribbon missing";
	case 0x00080007: return "Ribbon newly inserted";
	case 0x00080103: return "Ribbon exhausted";
	case 0x00080104: return "Ribbon exhausted";
	case 0x00080105: return "Ribbon malfunction";
	case 0x00080204: return "Ribbon missing in slave printer";
	case 0x00080207: return "Ribbon newly inserted in slave printer";
	case 0x000802FE: return "Ribbon IC error";
	case 0x00080303: return "Ribbon exhausted in slave printer";
	case 0x000803FE: return "Ribbon not authenticated";
	case 0x000804FE: return "Ribbon IC read/write error";
	case 0x000805FE: return "Ribbon IC read/write error in slave printer";
	case 0x000806FE: return "Unsupported ribbon";
	case 0x000807FE: return "Unsupported ribbon in slave printer";
	case 0x000808FE: return "Unknown ribbon";
	case 0x000809FE: return "Unknown ribbon in slave printer";
		/* Jam Alerts */
	case 0x00030000: return "Paper jam";
	case 0x0003000F: return "Paper jam";
	case 0x00030200: return "Paper jam in paper path 01";
	case 0x00030300: return "Paper jam in paper path 02";
	case 0x00030400: return "Paper jam in paper path 03";
	case 0x00030500: return "Paper jam in paper path 04";
	case 0x00030600: return "Paper jam in paper path 05";
	case 0x00030700: return "Paper jam in paper path 06";
	case 0x00030800: return "Paper jam in paper path 07";
	case 0x00030900: return "Paper jam in paper path 08";
	case 0x00030A00: return "Paper jam in paper path 09";
		/* Input Alerts */
	case 0x00000008: return "Paper box missing";
	case 0x00000100: return "Cover open";
	case 0x00000101: return "Cover open failure";
	case 0x00000200: return "Ribbon IC missing";
	case 0x00000201: return "Ribbon missing";
	case 0x00000202: return "Ribbon mismatch 01";
	case 0x00000203: return "Security check fail";
	case 0x00000204: return "Ribbon mismatch 02";
	case 0x00000205: return "Ribbon mismatch 03";
	case 0x00000300: return "Ribbon exhausted 01";
	case 0x00000301: return "Ribbon exhausted 02";
	case 0x00000302: return "Printing failure (jam?)";
	case 0x00000400: return "Paper exhausted 01";
	case 0x00000401: return "Paper exhausted 02";
	case 0x00000402: return "Paper not ready";
	case 0x00000500: return "Paper jam 01";
	case 0x00000501: return "Paper jam 02";
	case 0x00000502: return "Paper jam 03";
	case 0x00000503: return "Paper jam 04";
	case 0x00000504: return "Paper jam 05";
	case 0x00000600: return "Paper mismatch";
	case 0x00000700: return "Cam error 01";
	case 0x00000800: return "Cam error 02";
	case 0x00000900: return "NVRAM error";
	case 0x00001000: return "IC error";
	case 0x00001200: return "ADC error";
	case 0x00001300: return "FW Check Error";
	case 0x00001500: return "Cutter error";

#if 0  // XXX these seem inappropriate
	case 0x00007538: return "Device attached to printer";
	case 0x00007539: return "Printer is in mobile mode";
	case 0x00007540: return "Printer is in standalone mode";
	case 0x00007542: return "Firmware too old for Fine mode";
	case 0x00007543: return "Firmware too old for 2x6 mode";
	case 0x00007544: return "Firmware too old for Matte mode";
	case 0x00007545: return "Firmware too old";
	case 0x00007546: return "Firmware too old";
#endif
	case 0x00008000: return "Paper out or feeding error";
	case 0x00008008: return "Paper box missing";
	case 0x00008010: return "Paper roll mismatch";
	case 0x00080200: return "Ribbon type mismatch";
//	case 0x10008000: return "Paper out or paper low";  /* XXX this won't work, high byte is cleared */

	default: return "Unknown";
	}
}

static int hiti_get_info(struct hiti_ctx *ctx)
{
	int ret;

	ret = hiti_query_tphv(ctx);
	if (ret)
		return ret;
	ret = hiti_query_led_calibration(ctx);
	if (ret)
		return ret;

	INFO("Printer ID: %s\n", ctx->id);
	INFO("Printer Version: %s\n", ctx->version);
	INFO("Serial Number: %s\n", ctx->serno);

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
	INFO("Highlight Adjustment (Y M C): %d %d %d\n",
	     ctx->hilight_adj[1], ctx->hilight_adj[2], ctx->hilight_adj[3]);

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

	uint32_t buf = 0;
	ret = hiti_query_counter(ctx, 1, &buf);
	if (ret)
		return CUPS_BACKEND_FAILED;
	INFO("Total prints: %u\n", buf);

	ret = hiti_query_counter(ctx, 2, &buf);
	if (ret)
		return CUPS_BACKEND_FAILED;
	INFO("6x4 prints: %u\n", buf);

	ret = hiti_query_counter(ctx, 4, &buf);
	if (ret)
		return CUPS_BACKEND_FAILED;
	INFO("6x8 prints: %u\n", buf);

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

	return CUPS_BACKEND_OK;
}

/* Use jobid of 0 for "any" */
static int hiti_query_job_qa(struct hiti_ctx *ctx, struct hiti_job *jobid)
{
	int ret;
	uint16_t len = 16;
	uint8_t buf[17];  /* Enough for four jobs */
	int i;

	buf[0] = 0;
	ret = hiti_docmd_resp(ctx, CMD_JC_QQA,
			      (uint8_t*) jobid, sizeof(*jobid),
			      buf, &len);
	if (ret)
		return ret;

	/* Clear job ID.. */
	jobid->jobid = 0;

	for (i = 0 ; i < buf[0] ; i++) {
		// is jobid + status
		// status of 3 is suspended.
		// status of 0 is active.

		if (buf[4*i + 4] == 0)
			memcpy(jobid, &buf[4*i+1], sizeof(*jobid));

#if 0
		INFO("Job %02x %02x %02x => %02x\n",
		     buf[4*i + 1], buf[4*i + 2],
		     buf[4*i + 3], buf[4*i + 4]);
#endif
	}

	return CUPS_BACKEND_OK;
}

static int hiti_get_status(struct hiti_ctx *ctx)
{
	uint8_t sts[3];
	uint32_t err = 0;
	int ret;

	hiti_query_markers(ctx, NULL, NULL);
	ret = hiti_query_status(ctx, sts, &err);
	if (ret)
		return ret;

	INFO("Printer Status: %s (%02x %02x %02x)\n",
	     hiti_status(sts), sts[0], sts[1], sts[2]);
	INFO("Printer Error: %s (%08x)\n",
	     hiti_errors(err), err);

	INFO("Media: %s (%02x / %04x) : %03u/%03u\n",
	     hiti_ribbontypes(ctx->supplies[2]),
	     ctx->supplies[2],
	     ctx->ribbonvendor,
	     ctx->media_remain, hiti_ribboncounts(ctx->supplies[2]));
	INFO("Paper: %s (%02x)\n",
	     hiti_papers(ctx->supplies2[0]),
	     ctx->supplies2[0]);

	/* Find out if we have any jobs outstanding */
	struct hiti_job job = { 0 };
	hiti_query_job_qa(ctx, &job);

	// XXX other shit.../

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
		       uint8_t endp_up, uint8_t endp_down, int iface, uint8_t jobid)
{
	struct hiti_ctx *ctx = vctx;
	int ret;

	ctx->dev = dev;
	ctx->endp_up = endp_up;
	ctx->endp_down = endp_down;
	ctx->iface = iface;
	ctx->type = type;

	/* Ensure jobid is sane */
	ctx->jobid = (jobid & 0x7fff);
	if (!ctx->jobid)
		ctx->jobid++;

	if (test_mode < TEST_MODE_NOATTACH) {
		/* P52x firmware v1.19+ lose their minds when Linux
		   issues a routine CLEAR_ENDPOINT_HALT.  Printer can recover
		   if it is reset.  Unclear what the side effects are.. */
		if (ctx->type == P_HITI_52X)
			libusb_reset_device(dev);

		ret = hiti_query_unk8010(ctx);
		if (ret)
			return ret;
		ret = hiti_query_version(ctx);
		if (ret)
			return ret;
		ret = hiti_query_supplies(ctx);
		if (ret)
			return ret;
		ret = hiti_query_calibration(ctx);
		if (ret)
			return ret;
		ret = hiti_query_ribbonvendor(ctx);
		if (ret)
			return ret;
		ret = hiti_query_rpidm(ctx);
		if (ret)
			return ret;
		ret = hiti_query_hilightadj(ctx);
		if (ret)
			return ret;
		ret = hiti_query_serno(ctx->dev, ctx->endp_up, ctx->endp_down, ctx->iface, ctx->serno, sizeof(ctx->serno));
		if (ret)
			return ret;

		// do real stuff
	} else {
		ctx->supplies2[0] = PAPER_TYPE_6INCH;
		ctx->supplies[2] = RIBBON_TYPE_4x6;

		if (getenv("MEDIA_CODE")) {
			// set fake fw version?
			ctx->supplies[2] = atoi(getenv("MEDIA_CODE"));
			if (ctx->supplies[2] ==  RIBBON_TYPE_5x7)
				ctx->supplies2[0] = PAPER_TYPE_5INCH;
		}
	}

	ctx->marker.color = "#00FFFF#FF00FF#FFFF00";
	ctx->marker.name = hiti_ribbontypes(ctx->supplies[2]);
	ctx->marker.numtype = ctx->supplies[2];
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

static uint8_t *hiti_get_correction_data(struct hiti_ctx *ctx, uint8_t mode)
{
	char *fname;
	uint8_t *buf;
	int ret, len;

	int mediaver = ctx->ribbonvendor & 0x3f;
	int mediatype = ((ctx->ribbonvendor & 0xf000) == 0x1000);

	switch (ctx->type)
	{
	case P_HITI_51X:
		if (!mediatype) {
			if (mode) {
				fname = CORRTABLE_PATH "/P51x_CMQPra.bin";
				break;
			} else {
				fname = CORRTABLE_PATH "/P51x_CMPPra.bin";
				break;
			}
		} else {
			if (mode) {
				switch(mediaver) {
				case 0:
					fname = CORRTABLE_PATH "/P51x_CCQPra.bin";
					break;
				case 1:
					fname = CORRTABLE_PATH "/P51x_CCQP1ra.bin";
					break;
				case 2:
					fname = CORRTABLE_PATH "/P51x_CCQP2ra.bin";
					break;
				case 3:
				default:
					fname = CORRTABLE_PATH "/P51x_CCQP3ra.bin";
					break;
				}
			} else {
				switch(mediaver) {
				case 0:
					fname = CORRTABLE_PATH "/P51x_CCPPra.bin";
					break;
				case 1:
					fname = CORRTABLE_PATH "/P51x_CCPP1ra.bin";
					break;
				case 2:
					fname = CORRTABLE_PATH "/P51x_CCPP2ra.bin";
					break;
				case 3:
				default:
					fname = CORRTABLE_PATH "/P51x_CCPP3ra.bin";
					break;
				}
			}
		}
		break;
	case P_HITI_52X:
		fname = CORRTABLE_PATH "/P52x_CCPPri.bin";
		break;
	case P_HITI_720:
		if (!mediatype) {
			if (mode) {
				fname = CORRTABLE_PATH "/P72x_CMQPrd.bin";
				break;
			} else {
				fname = CORRTABLE_PATH "/P72x_CMPPrd.bin";
				break;
			}
		} else {
			if (mode) {
				switch(mediaver) {
				case 0:
					fname = CORRTABLE_PATH "/P72x_CCQPrd.bin";
					break;
				case 1:
					fname = CORRTABLE_PATH "/P72x_CCQP1rd.bin";
					break;
				case 2:
					fname = CORRTABLE_PATH "/P72x_CCQP2rd.bin";
					break;
				case 3:
					fname = CORRTABLE_PATH "/P72x_CCQP3rd.bin";
					break;
				case 4:
				default:
					fname = CORRTABLE_PATH "/P72x_CCQP4rd.bin";
				break;
				}
			} else {
				switch(mediaver) {
				case 0:
					fname = CORRTABLE_PATH "/P72x_CCPPrd.bin";
					break;
				case 1:
					fname = CORRTABLE_PATH "/P72x_CCPP1rd.bin";
					break;
				case 2:
					fname = CORRTABLE_PATH "/P72x_CCPP2rd.bin";
					break;
				case 3:
					fname = CORRTABLE_PATH "/P72x_CCPP3rd.bin";
					break;
				case 4:
				default:
					fname = CORRTABLE_PATH "/P72x_CCPP4rd.bin";
					break;
				}
			}
		}
		break;
	case P_HITI_750:
		fname = CORRTABLE_PATH "/P75x_CCPPri.bin";
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

/* HiTi's funky interpolation table processing */
struct rgb {
	uint8_t r;
	uint8_t g;
	uint8_t b;
};

static uint32_t interp1089[33];
static uint32_t interp33[33];
static uint16_t interp256[256*9];

static void hiti_interp_init(void)
{
	int i;
	uint16_t *pre, *cur;

	for (i = 0 ; i < 33 ; i++) {
		interp1089[i] = i * 1089;
		interp33[i] = i * 33;
	}
	memset(interp256, 0, sizeof(interp256));
	pre = &interp256[0];
	cur = &interp256[256];

	for (i = 1 ; i < 9 ; i++) {
		int j;
		for (j = 0 ; j < 256 ; j++) {
			cur[j] = pre[j] + j;
		};
		pre += 256;
		cur += 256;
	}
}

/* src and dst are RGB tuples */
static void hiti_interp33_256(uint8_t *dst, uint8_t *src, const uint8_t *pTable)
{
	struct rgb p1_pos, p2_pos, p3_pos, p4_pos;
	struct rgb p1_val, p2_val, p3_val, p4_val;
	uint8_t r_weight, g_weight, b_weight;
	uint16_t w1, w2, w3, w4;
	uint16_t *pw1, *pw2, *pw3, *pw4;
	uint32_t pos;

	/* Get Grid position */
	p1_pos.r = src[0] >> 3;
	p1_pos.g = src[1] >> 3;
	p1_pos.b = src[2] >> 3;

	p4_pos.r = p1_pos.r + 1;
	p4_pos.g = p1_pos.g + 1;
	p4_pos.b = p1_pos.b + 1;

	/* Weights */
	if (src[0] == 255)
		r_weight = 8;
	else
		r_weight = src[0] & 0x7;
	if (src[1] == 255)
		g_weight = 8;
	else
		g_weight = src[1] & 0x7;

	if (src[2] == 255)
		b_weight = 8;
	else
		b_weight = src[2] & 0x7;

	/* Work out relative weights and offsets */
	if (r_weight >= g_weight) {
		if (g_weight >= b_weight) { /* R > G > B */
			w1 = 8 - r_weight;
			w2 = r_weight - g_weight;
			w3 = g_weight - b_weight;
			w4 = b_weight;
			p2_pos.r = p1_pos.r + 1;
			p2_pos.g = p1_pos.g;
			p2_pos.b = p1_pos.b;
			p3_pos.r = p1_pos.r + 1;
			p3_pos.g = p1_pos.g + 1;
			p3_pos.b = p1_pos.b;
		} else {
			if (r_weight >= b_weight) { /* R > B > G */
				w1 = 8 - r_weight;
				w2 = r_weight - b_weight;
				w3 = b_weight - g_weight;
				w4 = g_weight;
				p2_pos.r = p1_pos.r + 1;
				p2_pos.g = p1_pos.g;
				p2_pos.b = p1_pos.b;
				p3_pos.r = p1_pos.r + 1;
				p3_pos.g = p1_pos.g;
				p3_pos.b = p1_pos.b + 1;
			} else { /* B > R > G */
				w1 = 8 - b_weight;
				w2 = b_weight - r_weight;
				w3 = r_weight - g_weight;
				w4 = g_weight;
				p2_pos.r = p1_pos.r;
				p2_pos.g = p1_pos.g;
				p2_pos.b = p1_pos.b + 1;
				p3_pos.r = p1_pos.r + 1;
				p3_pos.g = p1_pos.g;
				p3_pos.b = p1_pos.b + 1;
			}
		}
	} else {
		if (r_weight >= b_weight) { /* G > R > B */
			w1 = 8 - g_weight;
			w2 = g_weight - r_weight;
			w3 = r_weight - b_weight;
			w4 = b_weight;
			p2_pos.r = p1_pos.r;
			p2_pos.g = p1_pos.g + 1;
			p2_pos.b = p1_pos.b;
			p3_pos.r = p1_pos.r + 1;
			p3_pos.g = p1_pos.g + 1;
			p3_pos.b = p1_pos.b;
		} else {
			if (g_weight >= b_weight) { /* G > B > R */
				w1 = 8 - g_weight;
				w2 = g_weight - b_weight;
				w3 = b_weight - r_weight;
				w4 = r_weight;
				p2_pos.r = p1_pos.r;
				p2_pos.g = p1_pos.g + 1;
				p2_pos.b = p1_pos.b;
				p3_pos.r = p1_pos.r;
				p3_pos.g = p1_pos.g + 1;
				p3_pos.b = p1_pos.b + 1;
			} else { /* B > G > R */
				w1 = 8 - b_weight;
				w2 = b_weight - g_weight;
				w3 = g_weight - r_weight;
				w4 = r_weight;
				p2_pos.r = p1_pos.r;
				p2_pos.g = p1_pos.g;
				p2_pos.b = p1_pos.b + 1;
				p3_pos.r = p1_pos.r;
				p3_pos.g = p1_pos.g + 1;
				p3_pos.b = p1_pos.b + 1;
			}
		}
	}

	/* Work out values */
	pos = (interp1089[p1_pos.b] + interp33[p1_pos.g] + p1_pos.r) * 3;
	p1_val.r = pTable[pos];
	p1_val.g = pTable[pos + 1];
	p1_val.b = pTable[pos + 2];
	pos = (interp1089[p2_pos.b] + interp33[p2_pos.g] + p2_pos.r) * 3;
	p2_val.r = pTable[pos];
	p2_val.g = pTable[pos + 1];
	p2_val.b = pTable[pos + 2];
	pos = (interp1089[p3_pos.b] + interp33[p3_pos.g] + p3_pos.r) * 3;
	p3_val.r = pTable[pos];
	p3_val.g = pTable[pos + 1];
	p3_val.b = pTable[pos + 2];
	pos = (interp1089[p4_pos.b] + interp33[p4_pos.g] + p4_pos.r) * 3;
	p4_val.r = pTable[pos];
	p4_val.g = pTable[pos + 1];
	p4_val.b = pTable[pos + 2];

	/* Final offsets into interpolation table */
	pw1 = &interp256[w1 << 8];
	pw2 = &interp256[w2 << 8];
	pw3 = &interp256[w3 << 8];
	pw4 = &interp256[w4 << 8];

	/* And at long last.. final values */
	dst[0] = (pw1[p1_val.r] + pw2[p2_val.r] + pw3[p3_val.r] + pw4[p4_val.r]) >> 3;
	dst[1] = (pw1[p1_val.g] + pw2[p2_val.g] + pw3[p3_val.g] + pw4[p4_val.g]) >> 3;
	dst[2] = (pw1[p1_val.b] + pw2[p2_val.b] + pw3[p3_val.b] + pw4[p4_val.b]) >> 3;

}

static int hiti_read_parse(void *vctx, const void **vjob, int data_fd, int copies)
{
	struct hiti_ctx *ctx = vctx;
	struct hiti_printjob *job = NULL;
	int i;

	if (!ctx)
		return CUPS_BACKEND_FAILED;

	job = malloc(sizeof(*job));
	if (!job) {
		ERROR("Memory allocation failure!\n");
		return CUPS_BACKEND_RETRY_CURRENT;
	}
	memset(job, 0, sizeof(*job));

	job->copies = copies;

	/* Read in header */
	i = read(data_fd, &job->hdr, sizeof(job->hdr));
	if (i < 0 || i != sizeof(job->hdr)) {
		hiti_cleanup_job(job);
		if (i == 0)
			return CUPS_BACKEND_CANCEL;

		ERROR("Read failed (%d/%d)\n",
		      i, (int)sizeof(job->hdr));
		perror("ERROR: Read failed");
		return i;
	}

	/* Byteswap everything */
	{
		uint32_t *ptr = (uint32_t*) &job->hdr;
		for (i = 0 ; i < (int)(sizeof(job->hdr) / sizeof(uint32_t)) ; i++)
			ptr[i] = le32_to_cpu(ptr[i]);
	}

	/* Sanity check header */
	if (job->hdr.hdr_len != sizeof(job->hdr)) {
		ERROR("Header length mismatch (%u/%d)!\n", job->hdr.hdr_len, (int)sizeof(job->hdr));
		hiti_cleanup_job(job);
		return CUPS_BACKEND_CANCEL;
	}
	if (job->hdr.cookie != HDR_COOKIE) {
		ERROR("Unrecognized header!\n");
		hiti_cleanup_job(job);
		return CUPS_BACKEND_CANCEL;
	}

	/* Use whicever copy count is larger */
	if (job->copies < (int)job->hdr.copies)
		job->copies = job->hdr.copies;

	switch(ctx->type)
	{
	case P_HITI_52X:
		if (job->hdr.model != 520) {
			ERROR("Unrecognized header!\n");
			hiti_cleanup_job(job);
			return CUPS_BACKEND_CANCEL;
		}
		break;
	case P_HITI_720:
	case P_HITI_750:
		if (job->hdr.model != 720) {
			ERROR("Unrecognized header!\n");
			hiti_cleanup_job(job);
			return CUPS_BACKEND_CANCEL;
		}
		break;
	default:
		break;
	}
	// XXX check hdr.format

	/* Allocate a buffer */
	job->datalen = 0;
	job->databuf = malloc(job->hdr.payload_len);
	if (!job->databuf) {
		ERROR("Memory allocation failure!\n");
		hiti_cleanup_job(job);
		return CUPS_BACKEND_RETRY_CURRENT;
	}

	/* Read in data */
	uint32_t remain = job->hdr.payload_len;
	while (remain) {
		i = read(data_fd, job->databuf + job->datalen, remain);
		if (i < 0) {
			ERROR("Read failed (%d/%u/%u)\n",
			      i, remain, job->datalen);
			perror("ERROR: Read failed");
			hiti_cleanup_job(job);
			return CUPS_BACKEND_CANCEL;
		}
		job->datalen += i;
		remain -= i;
	}

	/* Sanity check against paper */
	switch (ctx->supplies2[0]) {
	case PAPER_TYPE_5INCH:
		if (job->hdr.cols != 1548) {
			ERROR("Illegal job on 5-inch paper!\n");
			hiti_cleanup_job(job);
			return CUPS_BACKEND_CANCEL;
		}
		break;
	case PAPER_TYPE_6INCH:
		if (job->hdr.cols != 1844) {
			ERROR("Illegal job on 6-inch paper!\n");
			hiti_cleanup_job(job);
			return CUPS_BACKEND_CANCEL;
		}
		break;
	default:
		ERROR("Unknown paper type (%d)!\n", ctx->supplies2[0]);
		hiti_cleanup_job(job);
		return CUPS_BACKEND_CANCEL;
	}

	/* Sanity check against ribbon type */
	switch (ctx->supplies[2]) {
	case RIBBON_TYPE_4x6:
		if (job->hdr.code != PRINT_TYPE_6x4 &&
		    job->hdr.code != PRINT_TYPE_6x4_2UP &&
		    job->hdr.code != PRINT_TYPE_6x2) {
			ERROR("Invalid ribbon type vs job (%02x/%02x)\n",
			      ctx->supplies[2], job->hdr.code);
			hiti_cleanup_job(job);
			return CUPS_BACKEND_CANCEL;
		}
		break;
	case RIBBON_TYPE_5x7:
		if (job->hdr.code != PRINT_TYPE_5x7 &&
		    job->hdr.code != PRINT_TYPE_5x3_5 &&
		    job->hdr.code != PRINT_TYPE_5x7_2UP) {
			ERROR("Invalid ribbon type vs job (%02x/%02x)\n",
			      ctx->supplies[2], job->hdr.code);
			hiti_cleanup_job(job);
			return CUPS_BACKEND_CANCEL;
		}
		break;
	case RIBBON_TYPE_6x8:
		if (job->hdr.code != PRINT_TYPE_6x4 &&
		    job->hdr.code != PRINT_TYPE_6x4_2UP &&
		    job->hdr.code != PRINT_TYPE_6x8 &&
		    job->hdr.code != PRINT_TYPE_6x2) {
			ERROR("Invalid ribbon type vs job (%02x/%02x)\n",
			      ctx->supplies[2], job->hdr.code);
			hiti_cleanup_job(job);
			return CUPS_BACKEND_CANCEL;
		}
		break;
	case RIBBON_TYPE_6x9:
		if (job->hdr.code != PRINT_TYPE_6x4 &&
		    job->hdr.code != PRINT_TYPE_6x4_2UP &&
		    job->hdr.code != PRINT_TYPE_6x8 &&
		    job->hdr.code != PRINT_TYPE_6x2 &&
		    job->hdr.code != PRINT_TYPE_6x9 &&
		    job->hdr.code != PRINT_TYPE_6x9_2UP) {
			ERROR("Invalid ribbon type vs job (%02x/%02x)\n",
			      ctx->supplies[2], job->hdr.code);
			hiti_cleanup_job(job);
			return CUPS_BACKEND_CANCEL;
		}
		break;
	default:
		ERROR("Unknown ribbon type!\n");
		hiti_cleanup_job(job);
		return CUPS_BACKEND_CANCEL;
	}

	/* Load up correction data, if requested */
	uint8_t *corrdata = NULL;
	if (!(job->hdr.payload_type & PAYLOAD_TYPE_FLAG_NOCORRECT))
		corrdata = hiti_get_correction_data(ctx, job->hdr.quality);
	if (corrdata) {
		INFO("Running input data through correction tables\n");
		hiti_interp_init();
	}

	/* Convert input packed BGR data into YMC planar */
	{
		int stride = ((job->hdr.cols * 4) + 3) / 4;
		uint8_t *ymcbuf = malloc(job->hdr.rows * stride * 3);
		uint32_t i, j;

		if (!ymcbuf) {
			hiti_cleanup_job(job);
			ERROR("Memory Allocation Failure!\n");
			return CUPS_BACKEND_FAILED;
		}

		for (i = 0 ; i < job->hdr.rows ; i++) {
			uint8_t *rowY = ymcbuf + stride * i;
			uint8_t *rowM = ymcbuf + stride * (job->hdr.rows + i);
			uint8_t *rowC = ymcbuf + stride * (job->hdr.rows * 2 + i);

			/* Simple optimization */
			uint8_t oldrgb[3] = { 255, 255, 255 };
			uint8_t destrgb[3];

			if (corrdata) {
				hiti_interp33_256(oldrgb, destrgb, corrdata);
			}

			for (j = 0 ; j < job->hdr.cols ; j++) {
				uint8_t rgb[3];
				uint32_t base = (job->hdr.cols * i + j) * 3;

				/* Input data is BGR */
				rgb[2] = job->databuf[base];
				rgb[1] = job->databuf[base + 1];
				rgb[0] = job->databuf[base + 2];

				if (corrdata) {
					if (rgb[0] == oldrgb[0] &&
					    rgb[1] == oldrgb[1] &&
					    rgb[2] == oldrgb[2]) {
						rgb[0] = destrgb[0];
						rgb[1] = destrgb[1];
						rgb[2] = destrgb[2];
					} else {
						oldrgb[0] = rgb[0];
						oldrgb[1] = rgb[1];
						oldrgb[2] = rgb[2];
						hiti_interp33_256(rgb, rgb, corrdata);
						destrgb[0] = rgb[0];
						destrgb[1] = rgb[1];
						destrgb[2] = rgb[2];
					}
				}

				/* Finally convert to YMC */
				rowY[j] = 255 - rgb[2];
				rowM[j] = 255 - rgb[1];
				rowC[j] = 255 - rgb[0];
			}
		}

		/* Nuke the old BGR buffer and replace it with YMC buffer */
		free(job->databuf);
		job->databuf = ymcbuf;
		job->datalen = stride * 3 * job->hdr.cols;

		if (corrdata)
			free(corrdata);
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

	return val;
}

static int hiti_main_loop(void *vctx, const void *vjob)
{
	struct hiti_ctx *ctx = vctx;

	int ret;
	int copies;
	uint32_t err = 0;
	uint8_t sts[3];
	struct hiti_job jobid;

	const struct hiti_printjob *job = vjob;

	if (!ctx)
		return CUPS_BACKEND_FAILED;
	if (!job)
		return CUPS_BACKEND_FAILED;

	copies = job->copies;

top:
	INFO("Waiting for printer idle\n");

	do {
		ret = hiti_query_status(ctx, sts, &err);
		if (ret)
			return ret;

		/* If we have an error state, bail! */
		if (err) {
			ERROR("Printer reported alert: %08x (%s)\n",
			      err, hiti_errors(err));
			return CUPS_BACKEND_FAILED;
		}

		/* If we're idle, proceed */
		if (!(sts[0] & (STATUS0_POWERON|STATUS0_BUSY)))
			break;

		jobid.lun = 0;
		jobid.jobid = 0;
		ret = hiti_query_job_qa(ctx, &jobid);
		if (ret)
			return ret;

		/* If we have no active job.. proceed! */
		if (jobid.jobid == 0)
		    break;

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
	sf.mediaType = job->hdr.code;
	sf.cols_res = cpu_to_be16(job->hdr.col_dpi);
	sf.rows_res = cpu_to_be16(job->hdr.row_dpi);
	sf.cols = cpu_to_be16(job->hdr.cols);
	sf.rows = cpu_to_be16(rows);
	sf.rows_offset = calc_offset(ctx->calibration.vert, 5, 8, 4);
	sf.cols_offset = calc_offset(ctx->calibration.horiz, 6, 11, 4);
	sf.colorSeq = 0x87 + (job->hdr.overcoat ? 0xc0 : 0);
	sf.copies = copies;
	sf.printMode = 0x08 + (job->hdr.quality ? 0x02 : 0);
	ret = hiti_docmd(ctx, CMD_EFD_SF, (uint8_t*) &sf, sizeof(sf), &resplen);
	if (ret)
		return CUPS_BACKEND_FAILED;

	// XXX msg 8011 sent here..

	/* XXX startjob returns actual jobid */
	jobid.lun = 0;
	jobid.jobid = cpu_to_be16(ctx->jobid);

	resplen = sizeof(jobid);
	ret = hiti_docmd_resp(ctx, CMD_JC_SJ, (uint8_t*) &jobid, sizeof(jobid),
			      (uint8_t*) &jobid, &resplen);
	if (ret)
		return CUPS_BACKEND_FAILED;

	INFO("Printer returned Job ID %04x\n", be16_to_cpu(jobid.jobid));

	uint8_t chs[2] = { 0, 1 }; /* Fixed..? */

	// XXX send ESD_SHTPC  w/ heat table.  Unknown.

	resplen = 0;
	ret = hiti_docmd(ctx, CMD_EFD_CHS, chs, sizeof(chs), &resplen);
	if (ret)
		return CUPS_BACKEND_FAILED;

	ret = hiti_docmd(ctx, CMD_EPC_SP, NULL, 0, &resplen);
	if (ret)
		return CUPS_BACKEND_FAILED;

	// CMD_ESD_SHPTC // Heating Parameters & Tone Curve (~7Kb, seen on windows..)

resend_y:
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
	usleep(200*1000);
	sent += rows * cols;
	ret = hiti_query_status(ctx, sts, &err);
	if (ret)
		return ret;
	if (err) {
		ERROR("Printer reported alert: %08x (%s)\n",
		      err, hiti_errors(err));
		return CUPS_BACKEND_FAILED;
	}
	if (sts[0] & STATUS0_RESEND_DATA) {
		WARNING("Printer requested resend\n");
		goto resend_y;
	}

resend_m:
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
	usleep(200*1000);
	ret = hiti_query_status(ctx, sts, &err);
	if (ret)
		return ret;
	if (err) {
		ERROR("Printer reported alert: %08x (%s)\n",
		      err, hiti_errors(err));
		return CUPS_BACKEND_FAILED;
	}
	if (sts[0] & STATUS0_RESEND_DATA) {
		WARNING("Printer requested resend\n");
		goto resend_m;
	}

resend_c:
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
	usleep(200*1000);
	sent += rows * cols;
	ret = hiti_query_status(ctx, sts, &err);
	if (ret)
		return ret;
	if (err) {
		ERROR("Printer reported alert: %08x (%s)\n",
		      err, hiti_errors(err));
		return CUPS_BACKEND_FAILED;
	}
	if (sts[0] & STATUS0_RESEND_DATA) {
		WARNING("Printer requested resend\n");
		goto resend_c;
	}

	INFO("Sending Print start\n");
	ret = hiti_docmd(ctx, CMD_EPC_EP, NULL, 0, &resplen);
	if (ret)
		return CUPS_BACKEND_FAILED;

	resplen = 3;
	ret = hiti_docmd_resp(ctx, CMD_JC_EJ, (uint8_t*) &jobid, sizeof(jobid), (uint8_t*) &jobid, &resplen);
	if (ret)
		return CUPS_BACKEND_FAILED;

	INFO("Waiting for printer acknowledgement\n");
	do {
		sleep(1);

		ret = hiti_query_status(ctx, sts, &err);
		if (ret)
			return ret;

		if (err) {
			ERROR("Printer reported alert: %08x (%s)\n",
			      err, hiti_errors(err));
			return CUPS_BACKEND_FAILED;
		}

		/* If we're idle, proceed! */
		if (!(sts[0] & (STATUS0_POWERON|STATUS0_BUSY)))
			break;

		ret = hiti_query_job_qa(ctx, &jobid);
		if (ret)
			return ret;

		/* If our job is complete.. */
		if (jobid.jobid == 0)
			break;

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

	while ((i = getopt(argc, argv, GETOPT_LIST_GLOBAL "irRs")) >= 0) {
		switch(i) {
		GETOPT_PROCESS_GLOBAL
		case 'i':
			hiti_get_info(ctx);
			break;
		case 'r':
			hiti_doreset(ctx, RESET_SOFT);
			break;
		case 'R':
			hiti_doreset(ctx, RESET_PRINTER);
			break;
		case 's':
			hiti_get_status(ctx);
			break;
		}

		if (j) return j;
	}

	return CUPS_BACKEND_OK;
}

static void hiti_cmdline(void)
{
	DEBUG("\t\t[ -i ]           # Query printer information\n");
	DEBUG("\t\t[ -r ]           # Soft Reset printer\n");
	DEBUG("\t\t[ -R ]           # Reset printer\n");
	DEBUG("\t\t[ -s ]           # Query printer status\n");
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

static int hiti_query_status(struct hiti_ctx *ctx, uint8_t *sts, uint32_t *err)
{
	int ret;
	uint16_t len = 3;
	uint16_t cmd;

	*err = 0;

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
		uint8_t respbuf[17];  /* Enough for four errors */
		len = sizeof(respbuf);

		ret = hiti_docmd_resp(ctx, cmd, NULL, 0, respbuf, &len);
		if (ret)
			return ret;

		if (!respbuf[0])
			return CUPS_BACKEND_OK;

		if (respbuf[0] > 1) {
			WARNING("Multiple Alerts detected, only returning the first!\n");
		} else if (len > 8) {
			// XXX means we have ASCIIHEX in positions [5:8], convert to number..
			// eg "30 31 30 30" == Code 0100
		}

		memcpy(err, &respbuf[1], sizeof(*err));
		*err = be32_to_cpu(*err);
		*err >>= 8;
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

	rds->stride = be16_to_cpu(rds->stride);
	rds->dpi_cols = be16_to_cpu(rds->dpi_cols);
	rds->dpi_rows = be16_to_cpu(rds->dpi_rows);
	rds->cols = be16_to_cpu(rds->cols);
	rds->rows = be16_to_cpu(rds->rows);

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

static int hiti_query_hilightadj(struct hiti_ctx *ctx)
{
	int ret;
	uint16_t len = sizeof(ctx->hilight_adj);

	ret = hiti_docmd_resp(ctx, CMD_ERDC_RHA, NULL, 0, ctx->hilight_adj, &len);
	if (ret)
		return ret;

	return CUPS_BACKEND_OK;
}

static int hiti_query_unk8010(struct hiti_ctx *ctx)
{
	int ret;
	uint16_t len = sizeof(ctx->unk_8010);

	ret = hiti_docmd_resp(ctx, CMD_ERDC_UNK, NULL, 0, ctx->unk_8010, &len);
	if (ret)
		return ret;

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

static int hiti_doreset(struct hiti_ctx *ctx, uint8_t type)
{
	int ret;
	uint8_t buf[6];
	uint16_t len = 6;

	ret = hiti_docmd_resp(ctx, CMD_RDS_RPS, &type, sizeof(type), buf, &len);
	if (ret)
		return ret;

	// response seems to be: 01 03 00 00 01 47
	sleep(5);

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

static int hiti_query_counter(struct hiti_ctx *ctx, uint8_t arg, uint32_t *resp)
{
	int ret;
	uint16_t len = sizeof(*resp);

	ret = hiti_docmd_resp(ctx, CMD_ERDC_RPC, &arg, sizeof(arg),
			      (uint8_t*) resp, &len);
	if (ret)
		return ret;

	*resp = be32_to_cpu(*resp);

	return CUPS_BACKEND_OK;
}

static int hiti_query_serno(struct libusb_device_handle *dev, uint8_t endp_up, uint8_t endp_down, int iface, char *buf, int buf_len)
{
	int ret;
	uint16_t rsplen = 18;
	uint8_t rspbuf[18];

	UNUSED(iface);

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

static int hiti_query_stats(void *vctx, struct printerstats *stats)
{
	struct hiti_ctx *ctx = vctx;
	uint8_t sts[3];
	uint32_t err = 0;
	uint32_t tmp = 0;

	/* Update marker info */
	if (hiti_query_markers(ctx, NULL, NULL))
		return CUPS_BACKEND_FAILED;
	if (hiti_query_status(ctx, sts, &err))
		return CUPS_BACKEND_FAILED;

	stats->mfg = "HiTi";
	stats->model = ctx->id;
	stats->serial = ctx->serno;
	stats->fwver = ctx->version;

	stats->decks = 1;
	stats->mediatype[0] = ctx->marker.name;
	stats->levelmax[0] = ctx->marker.levelmax;
	stats->levelnow[0] = ctx->marker.levelnow;
	stats->name[0] = "Roll";
	stats->cnt_life[0] = 0;

	if (hiti_query_counter(ctx, 1, &tmp))
		return CUPS_BACKEND_FAILED;

	stats->cnt_life[0] += tmp;

	if (err)
		stats->status[0] = strdup(hiti_errors(err));
	else
		stats->status[0] = strdup(hiti_status(sts));

	return CUPS_BACKEND_OK;
}


static const char *hiti_prefixes[] = {
	"hiti", // Family name
	"hiti-p52x",
	"hiti-p520l", "hiti-p525l", "hiti-p720l", "hiti-p750l",
	NULL
};

/* Exported */
#define USB_VID_HITI         0x0d16

#define USB_PID_HITI_P510K   0x0007
#define USB_PID_HITI_P720    0x0009
#define USB_PID_HITI_P728    0x000A
#define USB_PID_HITI_P510L   0x000B
#define USB_PID_HITI_P518A   0x000D
#define USB_PID_HITI_P530    0x000F
#define USB_PID_HITI_P510S   0x010E
#define USB_PID_HITI_P110S   0x0110
#define USB_PID_HITI_P510SI  0x0111
#define USB_PID_HITI_P518S   0x0112
#define USB_PID_HITI_CS200   0x0309
#define USB_PID_HITI_CS220   0x030A
#define USB_PID_HITI_P750    0x0501
#define USB_PID_HITI_P52X    0x0502
#define USB_PID_HITI_P310L   0x0503
#define USB_PID_HITI_P310W   0x050A
#define USB_PID_HITI_X610    0x0800

struct dyesub_backend hiti_backend = {
	.name = "HiTi Photo Printers",
	.version = "0.17",
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
	.query_stats = hiti_query_stats,
	.devices = {
		{ USB_VID_HITI, USB_PID_HITI_P52X, P_HITI_52X, NULL, "hiti-p520l"},
		{ USB_VID_HITI, USB_PID_HITI_P720, P_HITI_720, NULL, "hiti-p720l"},
		{ USB_VID_HITI, USB_PID_HITI_P750, P_HITI_750, NULL, "hiti-p750l"},
		{ 0, 0, 0, NULL, NULL}
	}
};

/* TODO:

   - Figure out 5x6, 6x5, and 6x6 prints (need 6x8 or 6x9 media!)
   - Confirm 6x2" print dimensions (windows?)
   - Confirm 5" media works properly
   - Figure out stats/counters for non-4x6 sizes
   - Job status & control (QJC, RSJ, QQA)
   - Figure out occasional data transfer hang (related to FW bug?)
   - Set highlight adjustment & H/V alignment from cmdline
   - Figure out if driver needs to consume highlight adjustment (ie feed into gamma correction?)
   - Figure out Windows spool format (probably never)
   - Spool parsing
      * Add additional 'reserved' fields for future use?
      * Support more hdr.format variants?
      * Have GP report proper modelid
   - Job combining (4x6 -> 8x6, etc)
   - Further performance optimizations in color conversion code
      * Pre-compute then cache entire map on disk?
   - Commands 8008, 8011, EST_SEHT, ESD_SHTPC, RDC_ROC, PCC_STP, CMD_EDM_*
   - Test with P525, P720, P750
   - Further investigation into P110S & P510 series
   - Start research into P530D, X610
   - Incorporate changes for CS-series card printers
   - More "Matrix table" decoding work

*/
