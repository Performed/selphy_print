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
#define UPDATE_SIZE 0x600
char *sinfonia_update_targets (uint8_t v);

#define TONECURVE_INIT    0x00
#define TONECURVE_USER    0x01
#define TONECURVE_CURRENT 0x02

char *sinfonia_tonecurve_statuses (uint8_t v);
