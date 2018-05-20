/*
 *   Mitsubishi CP-D70/D707 Photo Printer CUPS backend -- libusb-1.0 version
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

/* For Integration into gutenprint */
#if defined(HAVE_CONFIG_H)
#include <config.h>
#endif

#if defined(USE_DLOPEN)
#define WITH_DYNAMIC
#include <dlfcn.h>
#define DL_INIT() do {} while(0)
#define DL_OPEN(__x) dlopen(__x, RTLD_NOW)
#define DL_SYM(__x, __y) dlsym(__x, __y)
#define DL_CLOSE(__x) dlclose(__x)
#define DL_EXIT() do {} while(0)
#elif defined(USE_LTDL)
#define WITH_DYNAMIC
#include <ltdl.h>
#define DL_INIT() lt_dlinit()
#define DL_OPEN(__x) lt_dlopen(__x)
#define DL_SYM(__x, __y) lt_dlsym(__x, __y)
#define DL_CLOSE(__x) do {} while(0)
#define DL_EXIT() lt_dlexit()
#else
#define DL_INIT()     do {} while(0)
#define DL_CLOSE(__x) do {} while(0)
#define DL_EXIT()     do {} while(0)
#warning "No dynamic loading support!"
#endif

#define BACKEND mitsu70x_backend

#include "backend_common.h"

// #include "lib70x/libMitsuD70ImageReProcess.h"

#ifndef LUT_LEN
#define COLORCONV_RGB 0
#define COLORCONV_BGR 1

#define LUT_LEN 14739
struct BandImage {
	   void  *imgbuf;
	 int32_t bytes_per_row;
	uint16_t origin_cols;
	uint16_t origin_rows;
	uint16_t cols;
	uint16_t rows;
};
#endif

#define REQUIRED_LIB_APIVERSION 4

/* Image processing library function prototypes */
#define LIB_NAME_RE "libMitsuD70ImageReProcess.so" // Reimplemented library

typedef int (*lib70x_getapiversionFN)(void);
typedef int (*Get3DColorTableFN)(uint8_t *buf, const char *filename);
typedef struct CColorConv3D *(*Load3DColorTableFN)(const uint8_t *ptr);
typedef void (*Destroy3DColorTableFN)(struct CColorConv3D *this);
typedef void (*DoColorConvFN)(struct CColorConv3D *this, uint8_t *data, uint16_t cols, uint16_t rows, uint32_t bytes_per_row, int rgb_bgr);
typedef struct CPCData *(*get_CPCDataFN)(const char *filename);
typedef void (*destroy_CPCDataFN)(struct CPCData *data);
typedef int (*do_image_effectFN)(struct CPCData *cpc, struct CPCData *ecpc, struct BandImage *input, struct BandImage *output, int sharpen, int reverse, uint8_t rew[2]);
typedef int (*send_image_dataFN)(struct BandImage *out, void *context,
			       int (*callback_fn)(void *context, void *buffer, uint32_t len));

#ifndef CORRTABLE_PATH
#ifdef PACKAGE_DATA_DIR
#define CORRTABLE_PATH PACKAGE_DATA_DIR "/backend_data"
#else
#error "Must define CORRTABLE_PATH or PACKAGE_DATA_DIR!"
#endif
#endif

#define USB_VID_MITSU       0x06D3
#define USB_PID_MITSU_D70X  0x3B30
#define USB_PID_MITSU_K60   0x3B31
#define USB_PID_MITSU_D80   0x3B36
#define USB_VID_KODAK       0x040a
#define USB_PID_KODAK305    0x404f
#define USB_VID_FUJIFILM    0x04cb
#define USB_PID_FUJI_ASK300 0x5006

/* Width of the laminate data file */
#define LAMINATE_STRIDE 1864

/* Max size of data chunk sent over */
#define CHUNK_LEN (256*1024)

/* Private data structure */
struct mitsu70x_ctx {
	struct libusb_device_handle *dev;
	uint8_t endp_up;
	uint8_t endp_down;
	int type;

	uint8_t *databuf;
	int datalen;

	struct marker marker[2];

	uint32_t matte;

	uint16_t jobid;
	uint16_t rows;
	uint16_t cols;

	uint16_t last_l;
	uint16_t last_u;
	int num_decks;

	char *laminatefname;
	char *lutfname;
	char *cpcfname;
	char *ecpcfname;

	void *dl_handle;
	lib70x_getapiversionFN GetAPIVersion;
	Get3DColorTableFN Get3DColorTable;
	Load3DColorTableFN Load3DColorTable;
	Destroy3DColorTableFN Destroy3DColorTable;
	DoColorConvFN DoColorConv;
	get_CPCDataFN GetCPCData;
	destroy_CPCDataFN DestroyCPCData;
	do_image_effectFN DoImageEffect60;
	do_image_effectFN DoImageEffect70;
	do_image_effectFN DoImageEffect80;
	do_image_effectFN DoImageEffect;
	send_image_dataFN SendImageData;

	struct CColorConv3D *lut;
	struct CPCData *cpcdata;
	struct CPCData *ecpcdata;

	char *last_cpcfname;
	char *last_ecpcfname;

	int raw_format;
	int reverse;
	int sharpen; /* ie mhdr.sharpen - 1 */

	uint8_t rew[2]; /* 1 for rewind ok (default!) */

	struct BandImage output;
};

/* Printer data structures */
struct mitsu70x_jobstatus {
	uint8_t  hdr[4]; /* E4 56 31 30 */
	uint16_t jobid;  /* BE */
	uint16_t mecha_no; /* BE */
	uint8_t  job_status[4];
	uint8_t  memory;
	uint8_t  power;
	uint8_t  mecha_status[2];
	uint8_t  temperature;
	uint8_t  error_status[3];
	uint8_t  reserved[6];
} __attribute__((packed));

struct mitsu70x_job {
	uint16_t id; /* BE */
	uint8_t status[4];
} __attribute__((packed));

#define NUM_JOBS 170

struct mitsu70x_jobs {
	uint8_t  hdr[4]; /* E4 56 31 31 */
	struct mitsu70x_job jobs[NUM_JOBS];
} __attribute__((packed));

#define TEMPERATURE_NORMAL  0x00
#define TEMPERATURE_PREHEAT 0x40
#define TEMPERATURE_COOLING 0x80

#define MECHA_STATUS_INIT   0x80
#define MECHA_STATUS_FEED   0x50
#define MECHA_STATUS_LOAD   0x40
#define MECHA_STATUS_LOAD2  0x30
#define MECHA_STATUS_PRINT  0x20
#define MECHA_STATUS_IDLE   0x00

#define JOB_STATUS0_NONE    0x00
#define JOB_STATUS0_DATA    0x10
#define JOB_STATUS0_QUEUE   0x20
#define JOB_STATUS0_PRINT   0x50
#define JOB_STATUS0_ASSIGN  0x70 // XXX undefined.
#define JOB_STATUS0_END     0x80

#define JOB_STATUS1_PRINT_MEDIALOAD  0x10
#define JOB_STATUS1_PRINT_PRE_Y      0x20
#define JOB_STATUS1_PRINT_Y          0x30
#define JOB_STATUS1_PRINT_PRE_M      0x40
#define JOB_STATUS1_PRINT_M          0x50
#define JOB_STATUS1_PRINT_PRE_C      0x60
#define JOB_STATUS1_PRINT_C          0x70
#define JOB_STATUS1_PRINT_PRE_OC     0x80
#define JOB_STATUS1_PRINT_OC         0x90
#define JOB_STATUS1_PRINT_EJECT      0xA0

#define JOB_STATUS1_END_OK           0x00
#define JOB_STATUS1_END_MECHA        0x10 // 0x10...0x7f
#define JOB_STATUS1_END_HEADER       0x80
#define JOB_STATUS1_END_PRINT        0x90
#define JOB_STATUS1_END_INTERRUPT    0xA0

#define JOB_STATUS2_END_HEADER_ERROR 0x00
#define JOB_STATUS2_END_HEADER_MEMORY 0x10
#define JOB_STATUS2_END_PRINT_MEDIA   0x00
#define JOB_STATUS2_END_PRINT_PREVERR 0x10
#define JOB_STATUS2_END_INT_TIMEOUT  0x00
#define JOB_STATUS2_END_INT_CANCEL   0x10
#define JOB_STATUS2_END_INT_DISCON   0x20

/* Error codes */
#define ERROR_STATUS0_NOSTRIPBIN     0x01
#define ERROR_STATUS0_NORIBBON       0x02
#define ERROR_STATUS0_NOPAPER        0x03
#define ERROR_STATUS0_MEDIAMISMATCH  0x04
#define ERROR_STATUS0_RIBBONCNTEND   0x05
#define ERROR_STATUS0_BADRIBBON      0x06
#define ERROR_STATUS0_BADJOBPARAM    0x07
#define ERROR_STATUS0_PAPEREND       0x08
#define ERROR_STATUS0_RIBBONEND      0x09
#define ERROR_STATUS0_DOOROPEN_IDLE  0x0A
#define ERROR_STATUS0_DOOROPEN_PRNT  0x0B
#define ERROR_STATUS0_POWEROFF       0x0C // Powered off during printing..?
#define ERROR_STATUS0_NOMCOP         0x0D
#define ERROR_STATUS0_RIBBONSKIP1    0x0E
#define ERROR_STATUS0_RIBBONSKIP2    0x0F
#define ERROR_STATUS0_RIBBONJAM      0x10
#define ERROR_STATUS0_RIBBON_OTHER   0x11 // 0x11->0x1F
#define ERROR_STATUS0_PAPER_JAM      0x20 // 0x20->0x2F
#define ERROR_STATUS0_MECHANICAL     0x30 // 0x30->0x39
#define ERROR_STATUS0_RFID           0x3A
#define ERROR_STATUS0_FLASH          0x3B
#define ERROR_STATUS0_EEPROM         0x3C
#define ERROR_STATUS0_PREHEAT        0x3D
#define ERROR_STATUS0_MDASTATE       0x3E
#define ERROR_STATUS0_PSUFANLOCKED   0x3F
#define ERROR_STATUS0_OTHERS         0x40 // 0x40..?

/* Error classifications */
#define ERROR_STATUS1_PAPER          0x01
#define ERROR_STATUS1_RIBBON         0x02
#define ERROR_STATUS1_SETTING        0x03
#define ERROR_STATUS1_OPEN           0x05
#define ERROR_STATUS1_NOSTRIPBIN     0x06
#define ERROR_STATUS1_PAPERJAM       0x07
#define ERROR_STATUS1_RIBBONSYS      0x08
#define ERROR_STATUS1_MECHANICAL     0x09
#define ERROR_STATUS1_ELECTRICAL     0x0A
#define ERROR_STATUS1_FIRMWARE       0x0E
#define ERROR_STATUS1_OTHER          0x0F

/* Error recovery conditions */
#define ERROR_STATUS2_AUTO           0x00
#define ERROR_STATUS2_RELOAD_PAPER   0x01
#define ERROR_STATUS2_RELOAD_RIBBON  0x02
#define ERROR_STATUS2_CHANGE_BOTH    0x03
#define ERROR_STATUS2_CHANGE_ONE     0x04
#define ERROR_STATUS2_CLOSEUNIT      0x05
#define ERROR_STATUS2_ATTACHSTRIPBIN 0x06
#define ERROR_STATUS2_CLEARJAM       0x07
#define ERROR_STATUS2_CHECKRIBBON    0x08
#define ERROR_STATUS2_OPENCLOSEUNIT  0x0A
#define ERROR_STATUS2_POWEROFF       0x0F

struct mitsu70x_status_deck {
	uint8_t  mecha_status[2];
	uint8_t  temperature;   /* D70/D80 family only, K60 no? */
	uint8_t  error_status[3];
	uint8_t  rsvd_a[10];    /* K60 family [1] == temperature? [3:6] == lifetime prints in BCD */

	uint8_t  media_brand;
	uint8_t  media_type;
	uint8_t  rsvd_b[2];
	uint16_t capacity; /* media capacity */
	uint16_t remain;   /* media remaining */
	uint8_t  rsvd_c[2];
	uint8_t  lifetime_prints[4]; /* lifetime prints on deck + 10, in BCD! */
	uint16_t rsvd_e[17];
} __attribute__((packed));

struct mitsu70x_status_ver {
	char     ver[6];
	uint16_t checksum; /* Presumably BE */
} __attribute__((packed));

struct mitsu70x_printerstatus_resp {
	uint8_t  hdr[4];  /* E4 56 32 31 */
	uint8_t  memory;
	uint8_t  power;
	uint8_t  unk[20];
	uint8_t  sleeptime; /* In minutes, 0-60 */
	uint8_t  iserial; /* 0x00 for Enabled, 0x80 for Disabled */
	uint8_t  unk_b[12];
	int16_t  model[6]; /* LE, UTF-16 */
	int16_t  serno[6]; /* LE, UTF-16 */
	struct mitsu70x_status_ver vers[7]; // components are 'MLRTF'
	uint8_t  null[2];
	uint8_t  user_serno[6];  /* XXX Supposedly, don't know how to set it */
	struct mitsu70x_status_deck lower;
	struct mitsu70x_status_deck upper;
} __attribute__((packed));

struct mitsu70x_memorystatus_resp {
	uint8_t  hdr[3]; /* E4 56 33 */
	uint8_t  memory;
	uint8_t  size;
	uint8_t  rsvd;
} __attribute__((packed));

// XXX also seen commands 0x67, 0x72, 0x54, 0x6e

struct mitsu70x_hdr {
	uint8_t  hdr[4]; /* 1b 5a 54 XX */  // XXX also, seen 1b 5a 43!
	uint16_t jobid;
	uint8_t  rewind[2];  /* K60/EK305/D80 only */
	uint8_t  zero0[8];

	uint16_t cols;
	uint16_t rows;
	uint16_t lamcols;
	uint16_t lamrows;
	uint8_t  speed;
	uint8_t  zero1[7];

	uint8_t  deck; /* 0 = default, 1 = lower, 2 = upper -- Non-D70/D707 is always '1' */
	uint8_t  zero2[7];
	uint8_t  laminate; /* 00 == on, 01 == off */
	uint8_t  laminate_mode; /* 00 == glossy, 02 == matte */
	uint8_t  zero3[6];

	uint8_t  multicut;
	uint8_t  zero4[12]; /* NOTE:  everything past this point is an extension */
	uint8_t  sharpen;  /* 0-9.  5 is "normal", 0 is "off" */
	uint8_t  mode;     /* 0 for cooked YMC planar, 1 for packed BGR */
	uint8_t  use_lut;  /* in BGR mode, 0 disables, 1 enables */
	uint8_t  reversed; /* 1 tells the backend the row data is correct */
	uint8_t  pad[447];
} __attribute__((packed));

static int mitsu70x_get_printerstatus(struct mitsu70x_ctx *ctx, struct mitsu70x_printerstatus_resp *resp);

/* Error dumps, etc */

const char *mitsu70x_temperatures(uint8_t temp)
{
	switch(temp) {
	case TEMPERATURE_NORMAL:
		return "Normal";
	case TEMPERATURE_PREHEAT:
		return "Warming Up";
	case TEMPERATURE_COOLING:
		return "Cooling Down";
	default:
		break;
	}
	return "Unknown Temperature Status";
}

static const char *mitsu70x_mechastatus(uint8_t *sts)
{
	switch(sts[0]) {
	case MECHA_STATUS_INIT:
		return "Initializing";
	case MECHA_STATUS_FEED:
		return "Paper Feeding/Cutting";
	case MECHA_STATUS_LOAD:
	case MECHA_STATUS_LOAD2:
		return "Media Loading";
	case MECHA_STATUS_PRINT:
		return "Printing";
	case MECHA_STATUS_IDLE:
		return "Idle";
	default:
		break;
	}
	return "Unknown Mechanical Status";
}

static const char *mitsu70x_jobstatuses(uint8_t *sts)
{
	switch(sts[0]) {
	case JOB_STATUS0_NONE:
		return "No Job";
	case JOB_STATUS0_DATA:
		return "Data transfer";
	case JOB_STATUS0_QUEUE:
		return "Queued for printing";
	case JOB_STATUS0_PRINT:
		switch(sts[1]) {
		case JOB_STATUS1_PRINT_MEDIALOAD:
			return "Media loading";
		case JOB_STATUS1_PRINT_PRE_Y:
			return "Waiting to print yellow plane";
		case JOB_STATUS1_PRINT_Y:
			return "Printing yellow plane";
		case JOB_STATUS1_PRINT_PRE_M:
			return "Waiting to print magenta plane";
		case JOB_STATUS1_PRINT_M:
			return "Printing magenta plane";
		case JOB_STATUS1_PRINT_PRE_C:
			return "Waiting to print cyan plane";
		case JOB_STATUS1_PRINT_C:
			return "Printing cyan plane";
		case JOB_STATUS1_PRINT_PRE_OC:
			return "Waiting to laminate page";
		case JOB_STATUS1_PRINT_OC:
			return "Laminating page";
		case JOB_STATUS1_PRINT_EJECT:
			return "Ejecting page";
		default:
			return "Unknown 'Print' status1";
		}
		break;
	case JOB_STATUS0_ASSIGN:
		return "Unknown 'Assignment' status1";
	case JOB_STATUS0_END:
		switch(sts[1]) {
		case JOB_STATUS1_END_OK:
			return "Normal End";
		case JOB_STATUS1_END_HEADER:
			switch(sts[2]) {
			case JOB_STATUS2_END_HEADER_ERROR:
				return "Incorrect Header data (bad print size?)";
			case JOB_STATUS2_END_HEADER_MEMORY:
				return "Insufficient printer memory";
			default:
				return "Unknown 'End Header' status2";
			}
			break;
		case JOB_STATUS1_END_PRINT:
			switch(sts[2]) {
			case JOB_STATUS2_END_PRINT_MEDIA:
				return "Incorrect mediasize";
			case JOB_STATUS2_END_PRINT_PREVERR:
				return "Previous job terminated abnormally";
			default:
				return "Unknown 'End Print' status2";
			}
			break;
		case JOB_STATUS1_END_INTERRUPT:
			switch(sts[2]) {
			case JOB_STATUS2_END_INT_TIMEOUT:
				return "Timeout";
			case JOB_STATUS2_END_INT_CANCEL:
				return "Job cancelled";
			case JOB_STATUS2_END_INT_DISCON:
				return "Printer disconnected";
			default:
				return "Unknown 'End Print' status2";
			}
			break;
		default:
			if (sts[1] >= 0x10 && sts[1] <= 0x7f)
				return "Mechanical Error";
			else
				return "Unknown 'End' status1";
		}
		break;
	default:
		break;
	}

	return "Unknown status0";
}

static const char *mitsu70x_errorclass(uint8_t *err)
{
	switch(err[1]) {
	case ERROR_STATUS1_PAPER:
		return "Paper";
	case ERROR_STATUS1_RIBBON:
		return "Ribbon";
	case ERROR_STATUS1_SETTING:
		return "Job settings";
	case ERROR_STATUS1_OPEN:
		return "Cover open";
	case ERROR_STATUS1_NOSTRIPBIN:
		return "No cut bin";
	case ERROR_STATUS1_PAPERJAM:
		return "Paper jam";
	case ERROR_STATUS1_RIBBONSYS:
		return "Ribbon system";
	case ERROR_STATUS1_MECHANICAL:
		return "Mechanical";
	case ERROR_STATUS1_ELECTRICAL:
		return "Electrical";
	case ERROR_STATUS1_FIRMWARE:
		return "Firmware";
	case ERROR_STATUS1_OTHER:
		return "Other";
	default:
		break;
	}
	return "Unknown error class";
}

static const char *mitsu70x_errorrecovery(uint8_t *err)
{
	switch(err[1]) {
	case ERROR_STATUS2_AUTO:
		return "Automatic recovery";
	case ERROR_STATUS2_RELOAD_PAPER:
		return "Reload or change paper";
	case ERROR_STATUS2_RELOAD_RIBBON:
		return "Reload or change ribbon";
	case ERROR_STATUS2_CHANGE_BOTH:
		return "Change paper and ribbon";
	case ERROR_STATUS2_CHANGE_ONE:
		return "Change paper or ribbon";
	case ERROR_STATUS2_CLOSEUNIT:
		return "Close printer";
	case ERROR_STATUS2_ATTACHSTRIPBIN:
		return "Attach Strip Bin";
	case ERROR_STATUS2_CLEARJAM:
		return "Remove and reload paper";
	case ERROR_STATUS2_CHECKRIBBON:
		return "Check ribbon and reload paper";
	case ERROR_STATUS2_OPENCLOSEUNIT:
		return "Open then close printer";
	case ERROR_STATUS2_POWEROFF:
		return "Power-cycle printer";
	default:
		break;
	}
	return "Unknown recovery";
}

static const char *mitsu70x_errors(uint8_t *err)
{
	switch(err[0]) {
	case ERROR_STATUS0_NOSTRIPBIN:
		return "Strip bin not attached";
	case ERROR_STATUS0_NORIBBON:
		return "No ribbon detected";
	case ERROR_STATUS0_NOPAPER:
		return "No paper loaded";
	case ERROR_STATUS0_MEDIAMISMATCH:
		return "Ribbon/Paper mismatch";
	case ERROR_STATUS0_RIBBONCNTEND:
		return "Ribbon count end";
	case ERROR_STATUS0_BADRIBBON:
		return "Illegal Ribbon";
	case ERROR_STATUS0_BADJOBPARAM:
		return "Job does not match loaded media";
	case ERROR_STATUS0_PAPEREND:
		return "End of paper detected";
	case ERROR_STATUS0_RIBBONEND:
		return "End of ribbon detected";
	case ERROR_STATUS0_DOOROPEN_IDLE:
	case ERROR_STATUS0_DOOROPEN_PRNT:
		return "Printer door open";
	case ERROR_STATUS0_POWEROFF:
		return "Printer powered off"; // nonsense..
	case ERROR_STATUS0_RIBBONSKIP1:
	case ERROR_STATUS0_RIBBONSKIP2:
		return "Ribbon skipped";
	case ERROR_STATUS0_RIBBONJAM:
		return "Ribbon stuck to paper";
	case ERROR_STATUS0_RFID:
		return "RFID read error";
	case ERROR_STATUS0_FLASH:
		return "FLASH read error";
	case ERROR_STATUS0_EEPROM:
		return "EEPROM read error";
	case ERROR_STATUS0_PREHEAT:
		return "Preheating unit time out";
	case ERROR_STATUS0_MDASTATE:
		return "Unknown MDA state";
	case ERROR_STATUS0_PSUFANLOCKED:
		return "Power supply fan locked up";
	default:
		break;
	}

	if (err[0] >= ERROR_STATUS0_RIBBON_OTHER &&
	    err[0] < ERROR_STATUS0_PAPER_JAM) {
		return "Unknown ribbon error";
		// XXX use err[1]/err[2] codes?
	}
	if (err[0] >= ERROR_STATUS0_PAPER_JAM &&
	    err[0] < ERROR_STATUS0_MECHANICAL) {
		return "Paper jam";
		// XXX use err[1]/err[2] codes?
	}
	if (err[0] >= ERROR_STATUS0_MECHANICAL &&
	    err[0] < ERROR_STATUS0_RFID) {
		return "Unknown mechanical error";
		// XXX use err[1]/err[2] codes?
	}

	return "Unknown error";
}

const char *mitsu70x_media_types(uint8_t brand, uint8_t type)
{
	if (brand == 0xff && type == 0x01)
		return "CK-D735 (3.5x5)";
	else if (brand == 0xff && type == 0x02)
		return "CK-D746 (4x6)";
	else if (brand == 0xff && type == 0x04)
		return "CK-D757 (5x7)";
	else if (brand == 0xff && type == 0x05)
		return "CK-D769 (6x9)";
	else if (brand == 0xff && type == 0x0f)
		return "CK-D768/CK-D868 (6x8)";
	else if (brand == 0x6c && type == 0x84)
		return "Kodak 5R (5x7)";
	else if (brand == 0x6c && type == 0x8f)
		return "Kodak 6R (6x8)";
	else if (brand == 0x61 && type == 0x84)
		return "CK-K57R (5x7)";
	else if (brand == 0x61 && type == 0x8f)
		return "CK-K76R (6x8)";
	else if (brand == 0x7a && type == 0x01)
		return "RL-CF900 (3.5x5)";
	else if (brand == 0x7a && type == 0x02)
		return "RK-CF800/4R (4x6)";
	else if (brand == 0x7a && type == 0x04)
		return "R2L-CF460/5R (5x7)";
	else if (brand == 0x7a && type == 0x0f)
		return "R68-CF400/6R (6x8)";
	else
		return "Unknown";

// Also CK-D715, CK-D718, CK-D720, CK-D723 (4x6,5x8,6x8,6x9) for D70-S model
//      CK-D746-U for D70-U model
//      CK-D820 (6x8) for D80-S model
// D90 can use _all_ of these types except for the -U!

}

#define CMDBUF_LEN 512
#define READBACK_LEN 256

static void *mitsu70x_init(void)
{
	struct mitsu70x_ctx *ctx = malloc(sizeof(struct mitsu70x_ctx));
	if (!ctx) {
		ERROR("Memory Allocation Failure!\n");
		return NULL;
	}
	memset(ctx, 0, sizeof(struct mitsu70x_ctx));

	DL_INIT();

	return ctx;
}

static int mitsu70x_attach(void *vctx, struct libusb_device_handle *dev, int type,
			   uint8_t endp_up, uint8_t endp_down, uint8_t jobid)
{
	struct mitsu70x_ctx *ctx = vctx;

	ctx->jobid = jobid;
	if (!ctx->jobid)
		jobid++;

	ctx->dev = dev;
	ctx->endp_up = endp_up;
	ctx->endp_down = endp_down;
	ctx->type = type;

	ctx->last_l = ctx->last_u = 65535;

	/* Attempt to open the library */
#if defined(WITH_DYNAMIC)
	DEBUG("Attempting to load image processing library\n");
	ctx->dl_handle = DL_OPEN(LIB_NAME_RE);
	if (!ctx->dl_handle)
		WARNING("Image processing library not found, using internal fallback code\n");
	if (ctx->dl_handle) {
		ctx->GetAPIVersion = DL_SYM(ctx->dl_handle, "lib70x_getapiversion");
		if (!ctx->GetAPIVersion) {
			ERROR("Problem resolving API Version symbol in imaging processing library, too old or not installed?\n");
			DL_CLOSE(ctx->dl_handle);
			ctx->dl_handle = NULL;
			return CUPS_BACKEND_FAILED;
		}
		if (ctx->GetAPIVersion() != REQUIRED_LIB_APIVERSION) {
			ERROR("Image processing library API version mismatch!\n");
			DL_CLOSE(ctx->dl_handle);
			ctx->dl_handle = NULL;
			return CUPS_BACKEND_FAILED;
		}

		ctx->Get3DColorTable = DL_SYM(ctx->dl_handle, "CColorConv3D_Get3DColorTable");
		ctx->Load3DColorTable = DL_SYM(ctx->dl_handle, "CColorConv3D_Load3DColorTable");
		ctx->Destroy3DColorTable = DL_SYM(ctx->dl_handle, "CColorConv3D_Destroy3DColorTable");
		ctx->DoColorConv = DL_SYM(ctx->dl_handle, "CColorConv3D_DoColorConv");
		ctx->GetCPCData = DL_SYM(ctx->dl_handle, "get_CPCData");
		ctx->DestroyCPCData = DL_SYM(ctx->dl_handle, "destroy_CPCData");
		ctx->DoImageEffect60 = DL_SYM(ctx->dl_handle, "do_image_effect60");
		ctx->DoImageEffect70 = DL_SYM(ctx->dl_handle, "do_image_effect70");
		ctx->DoImageEffect80 = DL_SYM(ctx->dl_handle, "do_image_effect80");
		ctx->SendImageData = DL_SYM(ctx->dl_handle, "send_image_data");
		if (!ctx->Get3DColorTable || !ctx->Load3DColorTable ||
		    !ctx->Destroy3DColorTable || !ctx->DoColorConv ||
		    !ctx->GetCPCData || !ctx->DestroyCPCData ||
		    !ctx->DoImageEffect60 || !ctx->DoImageEffect70 ||
		    !ctx->DoImageEffect80 || !ctx->SendImageData) {
			ERROR("Problem resolving symbols in imaging processing library\n");
			DL_CLOSE(ctx->dl_handle);
			ctx->dl_handle = NULL;
			return CUPS_BACKEND_FAILED;
		} else {
			DEBUG("Image processing library successfully loaded\n");
		}
	}

	switch (ctx->type) {
	case P_MITSU_D80:
		ctx->DoImageEffect = ctx->DoImageEffect80;
		break;
	case P_MITSU_K60:
	case P_KODAK_305:
		ctx->DoImageEffect = ctx->DoImageEffect60;
		break;
	default:
		ctx->DoImageEffect = ctx->DoImageEffect70;
		break;
	}
#else
	WARNING("Dynamic library support not enabled, using internal fallback code\n");
#endif

	struct mitsu70x_printerstatus_resp resp;
	int ret;

	if (test_mode < TEST_MODE_NOATTACH) {
		ret = mitsu70x_get_printerstatus(ctx, &resp);
		if (ret) {
			ERROR("Unable to get printer status! (%d)\n", ret);
			return CUPS_BACKEND_FAILED;
		}
	} else {
		resp.upper.mecha_status[0] = MECHA_STATUS_INIT;
		resp.lower.mecha_status[0] = MECHA_STATUS_INIT;
		resp.upper.capacity = cpu_to_be16(230);
		resp.lower.capacity = cpu_to_be16(230);
		resp.upper.remain = cpu_to_be16(200);
		resp.lower.remain = cpu_to_be16(200);
		resp.upper.media_brand = 0xff;
		resp.lower.media_brand = 0xff;
		resp.upper.media_type = 0x0f;
		resp.lower.media_type = 0x0f;
	}

	if (ctx->type == P_MITSU_D70X &&
	    resp.upper.mecha_status[0] != MECHA_STATUS_INIT &&
		resp.upper.capacity != 0xffff)
		ctx->num_decks = 2;
	else
		ctx->num_decks = 1;

	/* Set up markers */
	ctx->marker[0].color = "#00FFFF#FF00FF#FFFF00";
	ctx->marker[0].name = mitsu70x_media_types(resp.lower.media_brand, resp.lower.media_type);
	ctx->marker[0].levelmax = be16_to_cpu(resp.lower.capacity);
	ctx->marker[0].levelnow = be16_to_cpu(resp.lower.remain);

	if (ctx->num_decks == 2) {
		ctx->marker[1].color = "#00FFFF#FF00FF#FFFF00";
		ctx->marker[1].name = mitsu70x_media_types(resp.upper.media_brand, resp.upper.media_type);
		ctx->marker[1].levelmax = be16_to_cpu(resp.upper.capacity);
		ctx->marker[1].levelnow = be16_to_cpu(resp.upper.remain);
	}

	return CUPS_BACKEND_OK;
}

static void mitsu70x_teardown(void *vctx) {
	struct mitsu70x_ctx *ctx = vctx;

	if (!ctx)
		return;

	if (ctx->databuf)
		free(ctx->databuf);

	if (ctx->dl_handle) {
		if (ctx->cpcdata)
			ctx->DestroyCPCData(ctx->cpcdata);
		if (ctx->ecpcdata)
			ctx->DestroyCPCData(ctx->ecpcdata);
		if (ctx->lut)
			ctx->Destroy3DColorTable(ctx->lut);
		DL_CLOSE(ctx->dl_handle);
	}

	DL_EXIT();

	free(ctx);
}

static int mitsu70x_read_parse(void *vctx, int data_fd) {
	struct mitsu70x_ctx *ctx = vctx;
	int i, remain;
	struct mitsu70x_hdr mhdr;
	uint32_t planelen;

	if (!ctx)
		return CUPS_BACKEND_FAILED;

	if (ctx->databuf) {
		free(ctx->databuf);
		ctx->databuf = NULL;
	}

	/* Reset some state */
	ctx->matte = 0;
	ctx->rew[0] = 1;
	ctx->rew[1] = 1;

repeat:
	/* Read in initial header */
	remain = sizeof(mhdr);
	while (remain > 0) {
		i = read(data_fd, ((uint8_t*)&mhdr) + sizeof(mhdr) - remain, remain);
		if (i == 0)
			return CUPS_BACKEND_CANCEL;
		if (i < 0)
			return CUPS_BACKEND_CANCEL;
		remain -= i;
	}

	/* Skip over wakeup header if it's present. */
	if (mhdr.hdr[0] == 0x1b &&
	    mhdr.hdr[1] == 0x45 &&
	    mhdr.hdr[2] == 0x57 &&
	    mhdr.hdr[3] == 0x55) {
		goto repeat;
	}

	/* Sanity check header */
	if (mhdr.hdr[0] != 0x1b ||
	    mhdr.hdr[1] != 0x5a ||
	    mhdr.hdr[2] != 0x54) {
		ERROR("Unrecognized data format!\n");
		return CUPS_BACKEND_CANCEL;
	}

	ctx->raw_format = !mhdr.mode;

	/* Sanity check Matte mode */
	if (!mhdr.laminate && mhdr.laminate_mode) {
		if (ctx->type != P_MITSU_D70X) {
			if (mhdr.speed != 0x03 && mhdr.speed != 0x04) {
				WARNING("Forcing Ultrafine mode for matte printing!\n");
				mhdr.speed = 0x04; /* Force UltraFine */
			}
		} else {
			if (mhdr.speed != 0x03) {
				mhdr.speed = 0x03; /* Force SuperFine */
				WARNING("Forcing SuperFine mode for matte printing!\n");
			}
		}
	}

	/* Figure out the correction data table to use */
	if (ctx->type == P_MITSU_D70X) {
		ctx->laminatefname = CORRTABLE_PATH "/D70MAT01.raw";
		ctx->lutfname = CORRTABLE_PATH "/CPD70L01.lut";

		if (mhdr.speed == 3) {
			ctx->cpcfname = CORRTABLE_PATH "/CPD70S01.cpc";
		} else if (mhdr.speed == 4) {
			ctx->cpcfname = CORRTABLE_PATH "/CPD70U01.cpc";
		} else {
			ctx->cpcfname = CORRTABLE_PATH "/CPD70N01.cpc";
		}
		if (mhdr.hdr[3] != 0x01) {
			WARNING("Print job has wrong submodel specifier (%x)\n", mhdr.hdr[3]);
			mhdr.hdr[3] = 0x01;
		}
	} else if (ctx->type == P_MITSU_D80) {
		ctx->laminatefname = CORRTABLE_PATH "/D80MAT01.raw";
		ctx->lutfname = CORRTABLE_PATH "/CPD80L01.lut";

		if (mhdr.speed == 3) {
			ctx->cpcfname = CORRTABLE_PATH "/CPD80S01.cpc";
			ctx->ecpcfname = CORRTABLE_PATH "/CPD80E01.cpc";
		} else if (mhdr.speed == 4) {
			ctx->cpcfname = CORRTABLE_PATH "/CPD80U01.cpc";
			ctx->ecpcfname = NULL;
		} else {
			ctx->cpcfname = CORRTABLE_PATH "/CPD80N01.cpc";
			ctx->ecpcfname = NULL;
		}
		if (mhdr.hdr[3] != 0x01) {
			WARNING("Print job has wrong submodel specifier (%x)\n", mhdr.hdr[3]);
			mhdr.hdr[3] = 0x01;
		}
	} else if (ctx->type == P_MITSU_K60) {
		ctx->laminatefname = CORRTABLE_PATH "/S60MAT02.raw";
		ctx->lutfname = CORRTABLE_PATH "/CPS60L01.lut";

		if (mhdr.speed == 3 || mhdr.speed == 4) {
			mhdr.speed = 4; /* Ultra Fine */
			ctx->cpcfname = CORRTABLE_PATH "/CPS60T03.cpc";
		} else {
			ctx->cpcfname = CORRTABLE_PATH "/CPS60T01.cpc";
		}
		if (mhdr.hdr[3] != 0x00) {
			WARNING("Print job has wrong submodel specifier (%x)\n", mhdr.hdr[3]);
			mhdr.hdr[3] = 0x00;
		}
	} else if (ctx->type == P_KODAK_305) {
		ctx->laminatefname = CORRTABLE_PATH "/EK305MAT.raw"; // Same as K60
		ctx->lutfname = CORRTABLE_PATH "/EK305L01.lut";

		if (mhdr.speed == 3 || mhdr.speed == 4) {
			mhdr.speed = 4; /* Ultra Fine */
			ctx->cpcfname = CORRTABLE_PATH "/EK305T03.cpc";
		} else {
			ctx->cpcfname = CORRTABLE_PATH "/EK305T01.cpc";
		}
		// XXX what about using K60 media if we read back the proper code?
		if (mhdr.hdr[3] != 0x90) {
			WARNING("Print job has wrong submodel specifier (%x)\n", mhdr.hdr[3]);
			mhdr.hdr[3] = 0x90;
		}
	} else if (ctx->type == P_FUJI_ASK300) {
		ctx->laminatefname = CORRTABLE_PATH "/ASK300M2.raw"; // Same as D70
//		ctx->lutfname = CORRTABLE_PATH "/CPD70L01.lut";  // XXX guess, driver did not come with external LUT!
		if (mhdr.speed == 3 || mhdr.speed == 4) {
			mhdr.speed = 3; /* Super Fine */
			ctx->cpcfname = CORRTABLE_PATH "/ASK300T3.cpc";
		} else {
			ctx->cpcfname = CORRTABLE_PATH "/ASK300T1.cpc";
		}
		if (mhdr.hdr[3] != 0x80) {
			WARNING("Print job has wrong submodel specifier (%x)\n", mhdr.hdr[3]);
			mhdr.hdr[3] = 0x80;
		}
	}
	if (!mhdr.use_lut)
		ctx->lutfname = NULL;

	ctx->sharpen = mhdr.sharpen - 1;
	ctx->reverse = !mhdr.reversed;

	/* Clean up header back to pristine. */
	mhdr.use_lut = 0;
	mhdr.mode = 0;
	mhdr.sharpen = 0;
	mhdr.reversed = 0;

	/* Work out total printjob size */
	ctx->cols = be16_to_cpu(mhdr.cols);
	ctx->rows = be16_to_cpu(mhdr.rows);

	planelen = ctx->rows * ctx->cols * 2;
	planelen = (planelen + 511) / 512 * 512; /* Round to nearest 512 bytes. */

	if (!mhdr.laminate && mhdr.laminate_mode) {
		i = be16_to_cpu(mhdr.lamcols) * be16_to_cpu(mhdr.lamrows) * 2;
		i = (i + 511) / 512 * 512; /* Round to nearest 512 bytes. */
		ctx->matte = i;
	}

	remain = 3 * planelen + ctx->matte;

	ctx->datalen = 0;
	ctx->databuf = malloc(sizeof(mhdr) + remain + LAMINATE_STRIDE*2);  /* Give us a bit extra */

	if (!ctx->databuf) {
		ERROR("Memory allocation failure!\n");
		return CUPS_BACKEND_RETRY_CURRENT;
	}

	memcpy(ctx->databuf + ctx->datalen, &mhdr, sizeof(mhdr));
	ctx->datalen += sizeof(mhdr);

	if (ctx->raw_format) { /* RAW MODE */
		DEBUG("Reading in %d bytes of 16bpp YMCL data\n", remain);

		/* Read in the spool data */
		while(remain) {
			i = read(data_fd, ctx->databuf + ctx->datalen, remain);
			if (i == 0)
				return CUPS_BACKEND_CANCEL;
			if (i < 0)
				return CUPS_BACKEND_CANCEL;
			ctx->datalen += i;
			remain -= i;
		}
	} else {  /* RAW MODE OFF */
		int spoolbuflen = 0;
		uint8_t *spoolbuf;

		remain = ctx->rows * ctx->cols * 3;
		DEBUG("Reading in %d bytes of 8bpp BGR data\n", remain);

		spoolbuflen = 0;
		spoolbuf = malloc(remain);
		if (!spoolbuf) {
			ERROR("Memory allocation failure!\n");
			return CUPS_BACKEND_RETRY_CURRENT;
		}

		/* Read in the BGR data */
		while (remain) {
			i = read(data_fd, spoolbuf + spoolbuflen, remain);
			if (i == 0) {
				free(spoolbuf);
				return CUPS_BACKEND_CANCEL;
			}
			if (i < 0) {
				free(spoolbuf);
				return CUPS_BACKEND_CANCEL;
			}
			spoolbuflen += i;
			remain -= i;
		}

		/* Run through basic LUT, if present and enabled */
		if (ctx->dl_handle && ctx->lutfname && !ctx->lut) {  /* printer-specific, it is fixed per-job */
			DEBUG("Running print data through LUT\n");
			uint8_t *buf = malloc(LUT_LEN);
			if (!buf) {
				ERROR("Memory allocation failure!\n");
				return CUPS_BACKEND_RETRY_CURRENT;
			}
			if (ctx->Get3DColorTable(buf, ctx->lutfname)) {
				ERROR("Unable to open LUT file '%s'\n", ctx->lutfname);
				return CUPS_BACKEND_CANCEL;
			}
			ctx->lut = ctx->Load3DColorTable(buf);
			free(buf);
			if (!ctx->lut) {
				ERROR("Unable to parse LUT file '%s'!\n", ctx->lutfname);
				return CUPS_BACKEND_CANCEL;
			}
			ctx->DoColorConv(ctx->lut, spoolbuf, ctx->cols, ctx->rows, ctx->cols * 3, COLORCONV_BGR);
		}

		if (ctx->dl_handle) {
			struct BandImage input;


			/* Load in the CPC file, if needed */
			if (ctx->cpcfname && ctx->cpcfname != ctx->last_cpcfname) {
				ctx->last_cpcfname = ctx->cpcfname;
				if (ctx->cpcdata)
					ctx->DestroyCPCData(ctx->cpcdata);
				ctx->cpcdata = ctx->GetCPCData(ctx->cpcfname);
				if (!ctx->cpcdata) {
					ERROR("Unable to load CPC file '%s'\n", ctx->cpcfname);
					return CUPS_BACKEND_CANCEL;
				}
			}

			/* Load in the secondary CPC, if needed */
			if (ctx->ecpcfname != ctx->last_ecpcfname) {
				ctx->last_ecpcfname = ctx->ecpcfname;
				if (ctx->ecpcdata)
					ctx->DestroyCPCData(ctx->ecpcdata);
				if (ctx->ecpcfname) {
					ctx->ecpcdata = ctx->GetCPCData(ctx->ecpcfname);
					if (!ctx->ecpcdata) {
						ERROR("Unable to load CPC file '%s'\n", ctx->cpcfname);
						return CUPS_BACKEND_CANCEL;
					}
				} else {
					ctx->ecpcdata = NULL;
				}
			}

			/* Convert using image processing library */
			input.origin_rows = input.origin_cols = 0;
			input.rows = ctx->rows;
			input.cols = ctx->cols;
			input.imgbuf = spoolbuf;
			input.bytes_per_row = ctx->cols * 3;

			ctx->output.origin_rows = ctx->output.origin_cols = 0;
			ctx->output.rows = ctx->rows;
			ctx->output.cols = ctx->cols;
			ctx->output.imgbuf = ctx->databuf + ctx->datalen;
			ctx->output.bytes_per_row = ctx->cols * 3 * 2;

			DEBUG("Running print data through processing library\n");
			if (ctx->DoImageEffect(ctx->cpcdata, ctx->ecpcdata,
					       &input, &ctx->output, ctx->sharpen, ctx->reverse, ctx->rew)) {
				ERROR("Image Processing failed, aborting!\n");
				return CUPS_BACKEND_CANCEL;
			}
		} else {
			// XXXFALLBACK write fallback code?
			ERROR("!!! Image Processing Library not found, aborting!\n");
			return CUPS_BACKEND_CANCEL;
		}

		/* Move up the pointer to after the image data */
		ctx->datalen += 3*planelen;

		/* Clean up */
		free(spoolbuf);

		/* Now that we've filled everything in, read matte from file */
		if (ctx->matte) {
			int fd;
			uint32_t j;
			DEBUG("Reading %u bytes of matte data from disk (%d/%d)\n", ctx->matte, ctx->cols, LAMINATE_STRIDE);
			fd = open(ctx->laminatefname, O_RDONLY);
			if (fd < 0) {
				ERROR("Unable to open matte lamination data file '%s'\n", ctx->laminatefname);
				return CUPS_BACKEND_CANCEL;
			}

			for (j = 0 ; j < be16_to_cpu(mhdr.lamrows) ; j++) {
				remain = LAMINATE_STRIDE * 2;

				/* Read one row of lamination data at a time */
				while (remain) {
					i = read(fd, ctx->databuf + ctx->datalen, remain);
					if (i < 0)
						return CUPS_BACKEND_CANCEL;
					if (i == 0) {
						/* We hit EOF, restart from beginning */
						lseek(fd, 0, SEEK_SET);
						continue;
					}
					ctx->datalen += i;
					remain -= i;
				}
				/* Back off the buffer so we "wrap" on the print row. */
				ctx->datalen -= ((LAMINATE_STRIDE - ctx->cols) * 2);
			}
			/* We're done */
			close(fd);

			/* Zero out the tail end of the buffer. */
			j = be16_to_cpu(mhdr.lamcols) * be16_to_cpu(mhdr.lamrows) * 2;
			memset(ctx->databuf + ctx->datalen, 0, ctx->matte - j);
		}
	}
	return CUPS_BACKEND_OK;
}

static int mitsu70x_get_jobstatus(struct mitsu70x_ctx *ctx, struct mitsu70x_jobstatus *resp, uint16_t jobid)
{
	uint8_t cmdbuf[CMDBUF_LEN];
	int num, ret;

	/* Send Printer Query */
	memset(cmdbuf, 0, CMDBUF_LEN);
	cmdbuf[0] = 0x1b;
	cmdbuf[1] = 0x56;
	cmdbuf[2] = 0x31;
	cmdbuf[3] = 0x30;  // XXX 30 == specific, 31 = "all"

	cmdbuf[4] = (jobid >> 8) & 0xff;
	cmdbuf[5] = jobid & 0xff;

	if ((ret = send_data(ctx->dev, ctx->endp_down,
			     cmdbuf, 6)))
		return ret;

	memset(resp, 0, sizeof(*resp));

	ret = read_data(ctx->dev, ctx->endp_up,
			(uint8_t*) resp, sizeof(*resp), &num);

	if (ret < 0)
		return ret;
	if (num != sizeof(*resp)) {
		ERROR("Short Read! (%d/%d)\n", num, (int)sizeof(*resp));
		return 4;
	}

	return 0;
}

#if 0
static int mitsu70x_get_jobs(struct mitsu70x_ctx *ctx, struct mitsu70x_jobs *resp)
{
	uint8_t cmdbuf[CMDBUF_LEN];
	int num, ret;

	/* Send Printer Query */
	memset(cmdbuf, 0, CMDBUF_LEN);
	cmdbuf[0] = 0x1b;
	cmdbuf[1] = 0x56;
	cmdbuf[2] = 0x31;
	cmdbuf[3] = 0x31;
	cmdbuf[4] = 0x00;
	cmdbuf[5] = 0x00;

	if ((ret = send_data(ctx->dev, ctx->endp_down,
			     cmdbuf, 6)))
		return ret;

	memset(resp, 0, sizeof(*resp));

	ret = read_data(ctx->dev, ctx->endp_up,
			(uint8_t*) resp, sizeof(*resp), &num);

	if (ret < 0)
		return ret;
	if (num != sizeof(*resp)) {
		ERROR("Short Read! (%d/%d)\n", num, (int)sizeof(*resp));
		return 4;
	}

	return 0;
}
#endif

static int mitsu70x_get_memorystatus(struct mitsu70x_ctx *ctx, uint8_t mcut, struct mitsu70x_memorystatus_resp *resp)
{
	uint8_t cmdbuf[CMDBUF_LEN];

	uint16_t tmp;

	int num;
	int ret;

	memset(cmdbuf, 0, CMDBUF_LEN);
	cmdbuf[0] = 0x1b;
	cmdbuf[1] = 0x56;
	cmdbuf[2] = 0x33;
	cmdbuf[3] = 0x00;
	tmp = cpu_to_be16(ctx->cols);
	memcpy(cmdbuf + 4, &tmp, 2);

	/* We have to lie about print sizes in 4x6*2 multicut modes */
	tmp = ctx->rows;
	if (tmp == 2730 && mcut == 1) {
		if (ctx->type == P_MITSU_D70X ||
		    ctx->type == P_FUJI_ASK300) {
			tmp = 2422;
		}
	}

	tmp = cpu_to_be16(tmp);
	memcpy(cmdbuf + 6, &tmp, 2);
	cmdbuf[8] = ctx->matte ? 0x80 : 0x00;
	cmdbuf[9] = 0x00;

	if ((ret = send_data(ctx->dev, ctx->endp_down,
			     cmdbuf, 10)))
		return CUPS_BACKEND_FAILED;

	/* Read in the printer status */
	ret = read_data(ctx->dev, ctx->endp_up,
			(uint8_t*) resp, sizeof(*resp), &num);
	if (ret < 0)
		return CUPS_BACKEND_FAILED;

	if (num != sizeof(*resp)) {
		ERROR("Short Read! (%d/%d)\n", num, (int)sizeof(*resp));
		return CUPS_BACKEND_FAILED;
	}

	/* Make sure response is sane */
	if (resp->hdr[0] != 0xe4 ||
	    resp->hdr[1] != 0x56 ||
	    resp->hdr[2] != 0x33) {
		ERROR("Unknown response from printer\n");
		return CUPS_BACKEND_FAILED;
	}

	return 0;
}


static int mitsu70x_get_printerstatus(struct mitsu70x_ctx *ctx, struct mitsu70x_printerstatus_resp *resp)
{
	uint8_t cmdbuf[CMDBUF_LEN];
	int num, ret;

	/* Send Printer Query */
	memset(cmdbuf, 0, CMDBUF_LEN);
	cmdbuf[0] = 0x1b;
	cmdbuf[1] = 0x56;
	cmdbuf[2] = 0x32;
	cmdbuf[3] = 0x30; /* or x31 or x32, for SINGLE DECK query!
			     Results will only have one deck. */
	if ((ret = send_data(ctx->dev, ctx->endp_down,
			     cmdbuf, 4)))
		return ret;
	memset(resp, 0, sizeof(*resp));
	ret = read_data(ctx->dev, ctx->endp_up,
			(uint8_t*) resp, sizeof(*resp), &num);

	if (ret < 0)
		return ret;
	if (num != sizeof(*resp)) {
		ERROR("Short Read! (%d/%d)\n", num, (int)sizeof(*resp));
		return 4;
	}

	return 0;
}

static int mitsu70x_cancel_job(struct mitsu70x_ctx *ctx, uint16_t jobid)
{
	uint8_t cmdbuf[4];
	int ret;

	/* Send Job cancel.  No response. */
	memset(cmdbuf, 0, 4);
	cmdbuf[0] = 0x1b;
	cmdbuf[1] = 0x44;
	cmdbuf[2] = (jobid >> 8) & 0xff;
	cmdbuf[3] = jobid & 0xff;
	if ((ret = send_data(ctx->dev, ctx->endp_down,
			     cmdbuf, 4)))
		return ret;

	return 0;
}

static int mitsu70x_set_sleeptime(struct mitsu70x_ctx *ctx, uint8_t time)
{
	uint8_t cmdbuf[4];
	int ret;

	/* 60 minutes max, according to all docs. */
	if (time > 60)
		time = 60;

	/* Send Parameter.. */
	memset(cmdbuf, 0, 4);
	cmdbuf[0] = 0x1b;
	cmdbuf[1] = 0x53;
	cmdbuf[2] = 0x53;
	cmdbuf[3] = time;

	if ((ret = send_data(ctx->dev, ctx->endp_down,
			     cmdbuf, 4)))
		return ret;

	return 0;
}

static int mitsu70x_set_iserial(struct mitsu70x_ctx *ctx, uint8_t enabled)
{
	uint8_t cmdbuf[4];
	int ret;

	if (enabled)
		enabled = 0;
	else
		enabled = 0x80;

	/* Send Parameter.. */
	memset(cmdbuf, 0, 4);
	cmdbuf[0] = 0x1b;
	cmdbuf[1] = 0x53;
	cmdbuf[2] = 0x4e;
	cmdbuf[3] = enabled;

	if ((ret = send_data(ctx->dev, ctx->endp_down,
			     cmdbuf, 4)))
		return ret;

	return 0;
}

#if 0
/* Switches between "Driver" and "SDK" modes.
   Single-endpoint vs Multi-Endpoint, essentially.
   Not sure about the polarity.
 */
static int mitsu70x_set_printermode(struct mitsu70x_ctx *ctx, uint8_t enabled)
{
	uint8_t cmdbuf[4];
	int ret;

	if (enabled)
		enabled = 0;
	else
		enabled = 0x80;

	/* Send Parameter.. */
	memset(cmdbuf, 0, 4);
	cmdbuf[0] = 0x1b;
	cmdbuf[1] = 0x53;
	cmdbuf[2] = 0x50;
	cmdbuf[3] = enabled;

	if ((ret = send_data(ctx->dev, ctx->endp_down,
			     cmdbuf, 4)))
		return ret;

	return 0;
}
#endif

static int mitsu70x_wakeup(struct mitsu70x_ctx *ctx, int wait)
{
	int ret;
	uint8_t buf[512];
	struct mitsu70x_jobstatus jobstatus;

top:
	/* Query job status for jobid 0 (global) */
	ret = mitsu70x_get_jobstatus(ctx, &jobstatus, 0x0000);
	if (ret)
		return CUPS_BACKEND_FAILED;

	/* Trigger a wakeup if necessary */
	if (jobstatus.power) {
		INFO("Waking up printer...\n");

		memset(buf, 0, sizeof(buf));
		buf[0] = 0x1b;
		buf[1] = 0x45;
		buf[2] = 0x57; // XXX also, 0x53, 0x54 seen.
		buf[3] = 0x55;

		if ((ret = send_data(ctx->dev, ctx->endp_down,
				     buf, sizeof(buf))))
			return CUPS_BACKEND_FAILED;

		if (wait) {
			sleep(1);
			goto top;
		}
	}


	return CUPS_BACKEND_OK;
}

static int d70_library_callback(void *context, void *buffer, uint32_t len)
{
	uint32_t chunk = len;
	uint32_t offset = 0;
	int ret = 0;

	struct mitsu70x_ctx *ctx = context;

	while (chunk > 0) {
		if (chunk > CHUNK_LEN)
			chunk = CHUNK_LEN;

		ret = send_data(ctx->dev, ctx->endp_down, (uint8_t*)buffer + offset, chunk);
		if (ret < 0)
			break;

		offset += chunk;
		chunk = len - offset;
	}

	return ret;
}

static int mitsu70x_main_loop(void *vctx, int copies)
{
	struct mitsu70x_ctx *ctx = vctx;
	struct mitsu70x_jobstatus jobstatus;
	struct mitsu70x_printerstatus_resp resp;
	struct mitsu70x_hdr *hdr;
	uint8_t last_status[4] = {0xff, 0xff, 0xff, 0xff};
	int statusdump = 0;

	int ret;

	if (!ctx)
		return CUPS_BACKEND_FAILED;

	hdr = (struct mitsu70x_hdr*) ctx->databuf;

	INFO("Waiting for printer idle...\n");

	/* Ensure printer is awake */
	ret = mitsu70x_wakeup(ctx, 1);
	if (ret)
		return CUPS_BACKEND_FAILED;

top:

	/* Query job status for jobid 0 (global) */
	ret = mitsu70x_get_jobstatus(ctx, &jobstatus, 0x0000);
	if (ret)
		return CUPS_BACKEND_FAILED;

	/* Make sure temperature is sane */
	if (jobstatus.temperature == TEMPERATURE_COOLING) {
		INFO("Printer cooling down...\n");
		sleep(1);
		goto top;
	}

	/* See if we hit a printer error. */
	if (jobstatus.error_status[0]) {
		ERROR("%s/%s -> %s:  %02x/%02x/%02x\n",
		      mitsu70x_errorclass(jobstatus.error_status),
		      mitsu70x_errors(jobstatus.error_status),
		      mitsu70x_errorrecovery(jobstatus.error_status),
		      jobstatus.error_status[0],
		      jobstatus.error_status[1],
		      jobstatus.error_status[2]);
		return CUPS_BACKEND_STOP;
	}

	if (statusdump)
		goto skip_status;
	statusdump = 1;

	/* Tell CUPS about the consumables we report */
	ret = mitsu70x_get_printerstatus(ctx, &resp);
	if (ret)
		return CUPS_BACKEND_FAILED;

	/* FW sanity checking */
	if (ctx->type == P_KODAK_305) {
		/* Known versions:
		   v1.02: M 316E81 1433   (Add Ultrafine and matte support)
		   v1.04: M 316F83 2878   (Add 2x6 strip and support "Triton" media)
		*/
		if (strncmp(resp.vers[0].ver, "316F83", 6) < 0)
			WARNING("Printer FW out of date. Highly recommend upgrading EK305 to v1.04 or newer!\n");
	} else if (ctx->type == P_MITSU_K60) {
		/* Known versions:
		   v1.05: M 316M31 148C   (Add HG media support)
		*/
		if (strncmp(resp.vers[0].ver, "316M31", 6) < 0)
			WARNING("Printer FW out of date. Highly recommend upgrading K60 to v1.05 or newer!\n");
	} else if (ctx->type == P_MITSU_D70X) {
		/* Known versions:
		   v1.10: M 316V11 064D   (Add ultrafine mode, 6x6 support, 2x6 strip, and more?)
		   v1.12: M 316W11 9FC3   (??)
		   v1.13:                 (??)
		*/
		if (strncmp(resp.vers[0].ver, "316W11", 6) < 0)
			WARNING("Printer FW out of date. Highly recommend upgrading D70/D707 to v1.12 or newer!\n");
	} else if (ctx->type == P_FUJI_ASK300) {
		/* Known versions:
		   v?.??: M 316A21 7998   (ancient. no matte or ultrafine)
		   v?.??: M 316H21 F8EB
		   v4.20a: M 316J21 4431  (Add 2x6 strip support)
		*/
		if (strncmp(resp.vers[0].ver, "316J21", 6) < 0)
			WARNING("Printer FW out of date. Highly recommend upgrading ASK300 to v4.20a or newer!\n");
	}

skip_status:
	/* Perform memory status query */
	{
		struct mitsu70x_memorystatus_resp memory;
		INFO("Checking Memory availability\n");

		ret = mitsu70x_get_memorystatus(ctx, hdr->multicut, &memory);
		if (ret)
			return CUPS_BACKEND_FAILED;

		/* Check size is sane */
		if (memory.size || memory.memory == 0xff) {
			ERROR("Unsupported print size!\n");
			return CUPS_BACKEND_CANCEL;
		}
		if (memory.memory) {
			INFO("Printer buffers full, retrying!\n");
			sleep(1);
			goto top;
		}
	}

#if 0
	/* Make sure we don't have any jobid collisions */
	{
		int i;
		struct mitsu70x_jobs jobs;

		ret = mitsu70x_get_jobs(ctx, &jobs);
		if (ret)
			return CUPS_BACKEND_FAILED;
		for (i = 0 ; i < NUM_JOBS ; i++) {
			if (jobs.jobs[0].id == 0)
				break;
			if (ctx->jobid == be16_to_cpu(jobs.jobs[0].id)) {
				ctx->jobid++;
				if (!ctx->jobid)
					ctx->jobid++;
				i = -1;
			}
		}
	}
#endif
	while(!ctx->jobid || ctx->jobid == be16_to_cpu(jobstatus.jobid))
		ctx->jobid++;

	/* Set jobid */
	hdr->jobid = cpu_to_be16(ctx->jobid);

	/* Set deck */
	if (ctx->type == P_MITSU_D70X) {
		hdr->deck = 0;  /* D70 use automatic deck selection */
		/* XXX alternatively route it based on state and media? */
	} else {
		hdr->deck = 1;  /* All others only have a "lower" deck. */
	}


	/* Twiddle rewind stuff if needed */
	if (ctx->type != P_MITSU_D70X) {
		hdr->rewind[0] = !ctx->rew[0];
		hdr->rewind[1] = !ctx->rew[1];
		DEBUG("Rewind Inhibit? %02x %02x\n", hdr->rewind[0], hdr->rewind[1]);
	}

	/* K60 and EK305 need the mcut type 1 specified for 4x6 prints! */
	if ((ctx->type == P_MITSU_K60 || ctx->type == P_KODAK_305) &&
	    ctx->cols == 0x0748 &&
	    ctx->rows == 0x04c2 && !hdr->multicut) {
		hdr->multicut = 1;
	}

	/* We're clear to send data over! */
	INFO("Sending Print Job (internal id %u)\n", ctx->jobid);

	if ((ret = send_data(ctx->dev, ctx->endp_down,
			     ctx->databuf,
			     sizeof(struct mitsu70x_hdr))))
		return CUPS_BACKEND_FAILED;

	if (ctx->dl_handle && !ctx->raw_format) {
		if (ctx->SendImageData(&ctx->output, ctx, d70_library_callback))
			return CUPS_BACKEND_FAILED;

		if (ctx->matte)
			if (d70_library_callback(ctx, ctx->databuf + ctx->datalen - ctx->matte, ctx->matte))
			    return CUPS_BACKEND_FAILED;
	} else { // Fallback code..
               /* K60 and 305 need data sent in 256K chunks, but the first
                  chunk needs to subtract the length of the 512-byte header */

		int chunk = CHUNK_LEN - sizeof(struct mitsu70x_hdr);
		int sent = 512;
		while (chunk > 0) {
			if ((ret = send_data(ctx->dev, ctx->endp_down,
					     ctx->databuf + sent, chunk)))
				return CUPS_BACKEND_FAILED;
			sent += chunk;
			chunk = ctx->datalen - sent;
			if (chunk > CHUNK_LEN)
				chunk = CHUNK_LEN;
		}
       }

	/* Then wait for completion, if so desired.. */
	INFO("Waiting for printer to acknowledge completion\n");

	do {
		sleep(1);

		ret = mitsu70x_get_printerstatus(ctx, &resp);
		if (ret)
			return CUPS_BACKEND_FAILED;

		ctx->marker[0].levelmax = be16_to_cpu(resp.lower.capacity);
		ctx->marker[0].levelnow = be16_to_cpu(resp.lower.remain);
		if (ctx->num_decks == 2) {
			ctx->marker[1].levelmax = be16_to_cpu(resp.upper.capacity);
			ctx->marker[1].levelnow = be16_to_cpu(resp.upper.remain);
		}
		if (ctx->marker[0].levelnow != ctx->last_l ||
		    ctx->marker[1].levelnow != ctx->last_u) {
			dump_markers(ctx->marker, ctx->num_decks, 0);
			ctx->last_l = ctx->marker[0].levelnow;
			ctx->last_u = ctx->marker[1].levelnow;
		}

		/* Query job status for our used jobid */
		ret = mitsu70x_get_jobstatus(ctx, &jobstatus, ctx->jobid);
		if (ret)
			return CUPS_BACKEND_FAILED;

		/* See if we hit a printer error. */
		if (jobstatus.error_status[0]) {
			ERROR("%s/%s -> %s:  %02x/%02x/%02x\n",
			      mitsu70x_errorclass(jobstatus.error_status),
			      mitsu70x_errors(jobstatus.error_status),
			      mitsu70x_errorrecovery(jobstatus.error_status),
			      jobstatus.error_status[0],
			      jobstatus.error_status[1],
			      jobstatus.error_status[2]);
			return CUPS_BACKEND_STOP;
		}

		/* Only print if it's changed */
		if (jobstatus.job_status[0] != last_status[0] ||
		    jobstatus.job_status[1] != last_status[1] ||
		    jobstatus.job_status[2] != last_status[2] ||
		    jobstatus.job_status[3] != last_status[3])
			INFO("%s: %02x/%02x/%02x/%02x\n",
			     mitsu70x_jobstatuses(jobstatus.job_status),
			     jobstatus.job_status[0],
			     jobstatus.job_status[1],
			     jobstatus.job_status[2],
			     jobstatus.job_status[3]);

		if (jobstatus.job_status[0] == JOB_STATUS0_END) {
			if (jobstatus.job_status[1] ||
			    jobstatus.job_status[2] ||
			    jobstatus.job_status[3]) {
				ERROR("Abnormal exit: %02x/%02x/%02x\n",
				      jobstatus.job_status[1],
				      jobstatus.job_status[2],
				      jobstatus.job_status[3]);
				return CUPS_BACKEND_STOP;
			}
			/* Job complete */
			break;
		}

		if (fast_return && copies <= 1) { /* Copies generated by backend! */
			INFO("Fast return mode enabled.\n");
			break;
		}

		/* Update cache for the next round */
		memcpy(last_status, jobstatus.job_status, 4);
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

static void mitsu70x_dump_printerstatus(struct mitsu70x_ctx *ctx,
					struct mitsu70x_printerstatus_resp *resp)
{
	uint32_t i;

	INFO("Model         : ");
	for (i = 0 ; i < 6 ; i++) {
		DEBUG2("%c", le16_to_cpu(resp->model[i]) & 0x7f);
	}
	DEBUG2("\n");
	INFO("Serial Number : ");
	for (i = 0 ; i < 6 ; i++) {
		DEBUG2("%c", le16_to_cpu(resp->serno[i]) & 0x7f);
	}
	DEBUG2("\n");
	for (i = 0 ; i < 7 ; i++) {
		char buf[7];
		char type;
		if (resp->vers[i].ver[5] == '@')  /* "DUMMY@" */
			continue;
		memcpy(buf, resp->vers[i].ver, 6);
		buf[6] = 0;
		if (i == 0) type = 'M';
		else if (i == 1) type = 'L';
		else if (i == 2) type = 'R';
		else if (i == 3) type = 'T';
		else if (i == 4) type = 'F';
		else type = i + 0x30;

		INFO("FW Component: %c %s (%04x)\n",
		     type, buf, be16_to_cpu(resp->vers[i].checksum));
	}
	INFO("Standby Timeout: %d minutes\n", resp->sleeptime);
	INFO("iSerial Reporting: %s\n", resp->iserial ? "No" : "Yes" );
	INFO("Power Status: %s\n", resp->power ? "Sleeping" : "Awake");

	if (resp->lower.error_status[0]) {
		INFO("Lower Error Status: %s/%s -> %s\n",
		     mitsu70x_errorclass(resp->lower.error_status),
		     mitsu70x_errors(resp->lower.error_status),
		     mitsu70x_errorrecovery(resp->lower.error_status));
	}
	INFO("Lower Temperature: %s\n", mitsu70x_temperatures(resp->lower.temperature));
	INFO("Lower Mechanical Status: %s\n",
	     mitsu70x_mechastatus(resp->lower.mecha_status));
	INFO("Lower Media Type:  %s (%02x/%02x)\n",
	     mitsu70x_media_types(resp->lower.media_brand, resp->lower.media_type),
	     resp->lower.media_brand,
	     resp->lower.media_type);
	INFO("Lower Prints Remaining:  %03d/%03d\n",
	     be16_to_cpu(resp->lower.remain),
	     be16_to_cpu(resp->lower.capacity));
	i = packed_bcd_to_uint32((char*)resp->lower.lifetime_prints, 4);
	if (i)
		i-= 10;
	INFO("Lower Lifetime Prints:  %u\n", i);

	if (ctx->num_decks == 2) {
		if (resp->upper.error_status[0]) {
			INFO("Upper Error Status: %s/%s -> %s\n",
			     mitsu70x_errorclass(resp->upper.error_status),
			     mitsu70x_errors(resp->upper.error_status),
			     mitsu70x_errorrecovery(resp->upper.error_status));
		}
		INFO("Upper Temperature: %s\n", mitsu70x_temperatures(resp->upper.temperature));
		INFO("Upper Mechanical Status: %s\n",
		     mitsu70x_mechastatus(resp->upper.mecha_status));
		INFO("Upper Media Type:  %s (%02x/%02x)\n",
		     mitsu70x_media_types(resp->upper.media_brand, resp->upper.media_type),
		     resp->upper.media_brand,
		     resp->upper.media_type);
		INFO("Upper Prints Remaining:  %03d/%03d\n",
		     be16_to_cpu(resp->upper.remain),
		     be16_to_cpu(resp->upper.capacity));

		i = packed_bcd_to_uint32((char*)resp->upper.lifetime_prints, 4);
		if (i)
			i-= 10;
		INFO("Upper Lifetime Prints:  %u\n", i);
	}
}

static int mitsu70x_query_jobs(struct mitsu70x_ctx *ctx)
{
#if 0
	struct mitsu70x_jobs jobs;
#endif
	struct mitsu70x_jobstatus jobstatus;
	int ret;

	ret = mitsu70x_get_jobstatus(ctx, &jobstatus, 0x0000);
	if (ret)
		return CUPS_BACKEND_FAILED;

	INFO("JOB00 ID     : %06u\n", jobstatus.jobid);
	INFO("JOB00 status : %s\n", mitsu70x_jobstatuses(jobstatus.job_status));
	INFO("Power Status: %s\n", jobstatus.power ? "Sleeping" : "Awake");
	INFO("Mechanical Status: %s\n",
	     mitsu70x_mechastatus(jobstatus.mecha_status));
	if (jobstatus.error_status[0]) {
		INFO("%s/%s -> %s\n",
		     mitsu70x_errorclass(jobstatus.error_status),
		     mitsu70x_errors(jobstatus.error_status),
		     mitsu70x_errorrecovery(jobstatus.error_status));
	}
	INFO("Temperature: %s\n", mitsu70x_temperatures(jobstatus.temperature));
	// memory status?

#if 0
	ret = mitsu70x_get_jobs(ctx, &jobs);
	if (!ret) {
		int i;
		for (i = 0 ; i < NUM_JOBS ; i++) {
			if (jobs.jobs[i].id == 0)
				break;
			INFO("JOB%02d ID     : %06u\n", i, jobs.jobs[i].id);
			INFO("JOB%02d status : %s\n", i, mitsu70x_jobstatuses(jobs.jobs[i].status));
		}
	}

done:
#endif
	return CUPS_BACKEND_OK;
}

static int mitsu70x_query_status(struct mitsu70x_ctx *ctx)
{
	struct mitsu70x_printerstatus_resp resp;
	int ret;

	ret = mitsu70x_get_printerstatus(ctx, &resp);
	if (!ret)
		mitsu70x_dump_printerstatus(ctx, &resp);

	return ret;
}

static int mitsu70x_query_serno(struct libusb_device_handle *dev, uint8_t endp_up, uint8_t endp_down, char *buf, int buf_len)
{
	int ret, i;
	struct mitsu70x_printerstatus_resp resp = { .hdr = { 0 } };

	struct mitsu70x_ctx ctx = {
		.dev = dev,
		.endp_up = endp_up,
		.endp_down = endp_down,
	};

	ret = mitsu70x_get_printerstatus(&ctx, &resp);

	if (buf_len > 6)  /* Will we ever have a buffer under 6 bytes? */
		buf_len = 6;

	for (i = 0 ; i < buf_len ; i++) {
		*buf++ = le16_to_cpu(resp.serno[i]) & 0x7f;
	}
	*buf = 0; /* Null-terminate the returned string */

	return ret;
}


static void mitsu70x_cmdline(void)
{
	DEBUG("\t\t[ -s ]           # Query printer status\n");
	DEBUG("\t\t[ -j ]           # Query job status\n");
	DEBUG("\t\t[ -w ]           # Wake up printer\n");
	DEBUG("\t\t[ -W ]           # Wake up printer and wait\n");
	DEBUG("\t\t[ -f ]           # Use fast return mode\n");
	DEBUG("\t\t[ -k num ]       # Set standby time (1-60 minutes, 0 disables)\n");
	DEBUG("\t\t[ -x num ]       # Set USB iSerialNumber Reporting (1 on, 0 off)\n");
	DEBUG("\t\t[ -X jobid ]     # Abort a printjob\n");}

static int mitsu70x_cmdline_arg(void *vctx, int argc, char **argv)
{
	struct mitsu70x_ctx *ctx = vctx;
	int i, j = 0;

	if (!ctx)
		return -1;

	while ((i = getopt(argc, argv, GETOPT_LIST_GLOBAL "jk:swWX:x:")) >= 0) {
		switch(i) {
		GETOPT_PROCESS_GLOBAL
		case 'j':
			j = mitsu70x_query_jobs(ctx);
			break;
		case 'k':
			j = mitsu70x_set_sleeptime(ctx, atoi(optarg));
			break;
		case 's':
			j = mitsu70x_query_status(ctx);
			break;
		case 'w':
			j = mitsu70x_wakeup(ctx, 0);
			break;
		case 'W':
			j = mitsu70x_wakeup(ctx, 1);
			break;
		case 'x':
			j = mitsu70x_set_iserial(ctx, atoi(optarg));
			break;
		case 'X':
			j = mitsu70x_cancel_job(ctx, atoi(optarg));
			break;
		default:
			break;  /* Ignore completely */
		}

		if (j) return j;
	}

	return 0;
}

static int mitsu70x_query_markers(void *vctx, struct marker **markers, int *count)
{
	struct mitsu70x_ctx *ctx = vctx;
	struct mitsu70x_printerstatus_resp resp;
	int ret;

	*markers = ctx->marker;
	*count = ctx->num_decks;

	/* Tell CUPS about the consumables we report */
	ret = mitsu70x_get_printerstatus(ctx, &resp);
	if (ret)
		return CUPS_BACKEND_FAILED;

	if (resp.power) {
		ret = mitsu70x_wakeup(ctx, 1);
		if (ret)
			return CUPS_BACKEND_FAILED;

		ret = mitsu70x_get_printerstatus(ctx, &resp);
		if (ret)
			return CUPS_BACKEND_FAILED;
	}

	ctx->marker[0].levelmax = be16_to_cpu(resp.lower.capacity);
	ctx->marker[0].levelnow = be16_to_cpu(resp.lower.remain);
	if (ctx->num_decks == 2) {
		ctx->marker[1].levelmax = be16_to_cpu(resp.upper.capacity);
		ctx->marker[1].levelnow = be16_to_cpu(resp.upper.remain);
	}

	return CUPS_BACKEND_OK;
}

static const char *mitsu70x_prefixes[] = {
	"mitsu70x",
	"mitsud80", "mitsuk60", "kodak305", "fujiask300",
	NULL,
};

/* Exported */
struct dyesub_backend mitsu70x_backend = {
	.name = "Mitsubishi CP-D70 family",
	.version = "0.79",
	.uri_prefixes = mitsu70x_prefixes,
	.cmdline_usage = mitsu70x_cmdline,
	.cmdline_arg = mitsu70x_cmdline_arg,
	.init = mitsu70x_init,
	.attach = mitsu70x_attach,
	.teardown = mitsu70x_teardown,
	.read_parse = mitsu70x_read_parse,
	.main_loop = mitsu70x_main_loop,
	.query_serno = mitsu70x_query_serno,
	.query_markers = mitsu70x_query_markers,
	.devices = {
		{ USB_VID_MITSU, USB_PID_MITSU_D70X, P_MITSU_D70X, NULL, "mitsu70x"},
		{ USB_VID_MITSU, USB_PID_MITSU_K60, P_MITSU_K60, NULL, "mitsuk60"},
		{ USB_VID_MITSU, USB_PID_MITSU_D80, P_MITSU_D80, NULL, "mitsud80"},
		{ USB_VID_KODAK, USB_PID_KODAK305, P_KODAK_305, NULL, "kodak305"},
		{ USB_VID_FUJIFILM, USB_PID_FUJI_ASK300, P_FUJI_ASK300, NULL, "fujiask300"},
		{ 0, 0, 0, NULL, NULL}
	}
};

/* Mitsubish CP-D70DW/D707DW/K60DW-S/D80DW, Kodak 305, Fuji ASK-300
   data format:

   Spool file consists of two headers followed by three image planes
   and an optional lamination data plane.  All blocks are rounded up to
   a 512-byte boundary.

   All multi-byte numbers are big endian, ie MSB first.

   Header 1:  (AKA Wake Up)

   1b 45 57 55 00 00 00 00  00 00 00 00 00 00 00 00
   (padded by NULLs to a 512-byte boundary)

   Header 2:  (Print Header)

   1b 5a 54 PP JJ JJ RR RR  00 00 00 00 00 00 00 00
   XX XX YY YY QQ QQ ZZ ZZ  SS 00 00 00 00 00 00 00
   UU 00 00 00 00 00 00 00  LL TT 00 00 00 00 00 00
   MM 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00

   (padded by NULLs to a 512-byte boundary)

   PP    == 0x01 on D70x/D80, 0x00 on K60, 0x90 on K305, 0x80 on ASK300
   JJ JJ == Job ID, can leave at 00 00
   XX XX == columns
   YY YY == rows
   QQ QQ == lamination columns (equal to XX XX)
   ZZ ZZ == lamination rows (YY YY + 12 on D70x/D80/ASK300, YY YY on others)
   RR RR == "rewind inhibit", 01 01 enabled, normally 00 00 (All but D70x/A300)
   SS    == Print mode: 00 = Fine, 03 = SuperFine (D70x/D80 only), 04 = UltraFine
            (Matte requires Superfine or Ultrafine)
   UU    == 00 = Auto, 01 = Lower Deck (required for !D70x), 02 = Upper Deck
   LL    == lamination enable, 00 == on, 01 == off
   TT    == lamination mode: 00 glossy, 02 matte
   MM    == 00 (normal), 01 = (Double-cut 4x6), 05 = (double-cut 2x6)

   Data planes:
   16-bit data, rounded up to 512-byte block (XX * YY * 2 bytes)

   Lamination plane: (only present if QQ and ZZ are nonzero)
   16-byte data, rounded up to 512-byte block (QQ * ZZ * 2 bytes)

 */
