 /*
 *   Shinko/Sinfonia Common Code
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

#define SINFONIA_HDR1_LEN 0x10
#define SINFONIA_HDR2_LEN 0x64
#define SINFONIA_HDR_LEN (SINFONIA_HDR1_LEN + SINFONIA_HDR2_LEN)
#define SINFONIA_DPI 300

struct sinfonia_job_param {
	uint32_t columns;
	uint32_t rows;
	uint32_t copies;

	uint32_t method;
	uint32_t media;
	uint32_t oc_mode;

	uint32_t quality;

	int       mattedepth;
	uint32_t dust;

	uint32_t ext_flags;
};

struct sinfonia_printjob {
	struct sinfonia_job_param jp;

	uint8_t *databuf;
	int datalen;
	int copies;
};

int sinfonia_read_parse(int data_fd, uint32_t model,
			struct sinfonia_job_param *jp,
			uint8_t **data, int *datalen);


#define BANK_STATUS_FREE  0x00
#define BANK_STATUS_XFER  0x01
#define BANK_STATUS_FULL  0x02
#define BANK_STATUS_PRINTING  0x12  /* Not on S2145 */

char *sinfonia_bank_statuses(uint8_t v);


#define UPDATE_TARGET_USER    0x03
#define UPDATE_TARGET_CURRENT 0x04

/* Update is three channels, Y, M, C;
   each is 256 entries of 11-bit data padded to 16-bits.
   Printer expects LE data.  We use BE data on disk.
*/
#define TONE_CURVE_SIZE 0x600
char *sinfonia_update_targets (uint8_t v);

#define TONECURVE_INIT    0x00
#define TONECURVE_USER    0x01
#define TONECURVE_CURRENT 0x02

char *sinfonia_tonecurve_statuses (uint8_t v);

struct sinfonia_error_item {
	uint8_t  major;
	uint8_t  minor;
	uint32_t print_counter;
} __attribute__((packed));

#define ERROR_NONE              0x00
#define ERROR_INVALID_PARAM     0x01
#define ERROR_MAIN_APP_INACTIVE 0x02
#define ERROR_COMMS_TIMEOUT     0x03
#define ERROR_MAINT_NEEDED      0x04
#define ERROR_BAD_COMMAND       0x05
#define ERROR_PRINTER           0x11
#define ERROR_BUFFER_FULL       0x21

char *sinfonia_error_str(uint8_t v);

enum {
	MEDIA_TYPE_UNKNOWN = 0x00,
	MEDIA_TYPE_PAPER = 0x01,
};

char *media_types(uint8_t v);


/* ********** Below are for the old S1145 (EK68xx) and S1245 only! */

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

char *sinfonia_1x45_status_str(uint8_t status1, uint32_t status2, uint8_t error);
