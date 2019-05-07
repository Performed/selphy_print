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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

#include "backend_common.h"
#include "backend_shinko.h"

int sinfonia_read_parse(int data_fd, uint32_t model,
			struct sinfonia_job_param *jp,
			uint8_t **data, int *datalen)
{
	uint32_t hdr[29];
	int ret, i;
	uint8_t tmpbuf[4];

	/* Read in header */
	ret = read(data_fd, hdr, SINFONIA_HDR_LEN);
	if (ret < 0 || ret != SINFONIA_HDR_LEN) {
		if (ret == 0)
			return CUPS_BACKEND_CANCEL;
		ERROR("Read failed (%d/%d)\n",
		      ret, SINFONIA_HDR_LEN);
		perror("ERROR: Read failed");
		return ret;
	}

	/* Byteswap everything */
	for (i = 0 ; i < (SINFONIA_HDR_LEN / 4) ; i++) {
		hdr[i] = le32_to_cpu(hdr[i]);
	}

	/* Sanity-check headers */
	if (hdr[0] != SINFONIA_HDR1_LEN ||
	    hdr[4] != SINFONIA_HDR2_LEN ||
	    hdr[22] != SINFONIA_DPI) {
		ERROR("Unrecognized header data format!\n");
		return CUPS_BACKEND_CANCEL;
	}
	if (hdr[1] != model) {
		ERROR("job/printer mismatch (%u/%u)!\n", hdr[1], model);
		return CUPS_BACKEND_CANCEL;
	}

	if (!hdr[13] || !hdr[14]) {
		ERROR("Bad job cols/rows!\n");
		return CUPS_BACKEND_CANCEL;
	}

	/* Work out data length */
	*datalen = hdr[13] * hdr[14] * 3;
	*data = malloc(*datalen);
	if (!*data) {
		ERROR("Memory allocation failure!\n");
		return CUPS_BACKEND_RETRY_CURRENT;
	}

	/* Read in payload data */
	{
		uint32_t remain = *datalen;
		uint8_t *ptr = *data;
		do {
			ret = read(data_fd, ptr, remain);
			if (ret < 0) {
				ERROR("Read failed (%d/%d/%d)\n",
				      ret, remain, *datalen);
				perror("ERROR: Read failed");
				free(*data);
				*data = NULL;
				return ret;
			}
			ptr += ret;
			remain -= ret;
		} while (remain);
	}

	/* Make sure footer is sane too */
	ret = read(data_fd, tmpbuf, 4);
	if (ret != 4) {
		ERROR("Read failed (%d/%d)\n", ret, 4);
		perror("ERROR: Read failed");
		free(*data);
		*data = NULL;
		return ret;
	}
	if (tmpbuf[0] != 0x04 ||
	    tmpbuf[1] != 0x03 ||
	    tmpbuf[2] != 0x02 ||
	    tmpbuf[3] != 0x01) {
		ERROR("Unrecognized footer data format!\n");
		free (*data);
		*data = NULL;
		return CUPS_BACKEND_CANCEL;
	}

	/* Fill out job params */
	if (jp) {
		jp->media = hdr[6];
		if (hdr[1] != 6245)
			jp->method = hdr[8];
		if (hdr[1] == 1245 || hdr[1] == 2145)
			jp->oc_mode = hdr[9];
		else
			jp->oc_mode = hdr[10];
		if (hdr[1] == 1245)
			jp->mattedepth = hdr[11];
		if (hdr[1] == 1245)
			jp->dust = hdr[12];
		jp->rows = hdr[13];
		jp->columns = hdr[14];
		jp->copies = hdr[15];

		if (hdr[1] == 6145)
			jp->ext_flags = hdr[28];
	}

	return CUPS_BACKEND_OK;
}

char *sinfonia_update_targets (uint8_t v) {
	switch (v) {
	case UPDATE_TARGET_USER:
		return "User";
	case UPDATE_TARGET_CURRENT:
		return "Current";
	default:
		return "Unknown";
	}
}

char *sinfonia_tonecurve_statuses (uint8_t v)
{
	switch(v) {
	case 0:
		return "Initial";
	case 1:
		return "UserSet";
	case 2:
		return "Current";
	default:
		return "Unknown";
	}
}

char *sinfonia_bank_statuses(uint8_t v)
{
	switch (v) {
	case BANK_STATUS_FREE:
		return "Free";
	case BANK_STATUS_XFER:
		return "Xfer";
	case BANK_STATUS_FULL:
		return "Full";
	case BANK_STATUS_PRINTING:
		return "Printing";
	default:
		return "Unknown";
	}
}
