/*
 *   Canon SELPHY ES/CP series print assister -- Native Linux version
 *
 *   (c) 2007-2012 Solomon Peachy <pizza@shaftnet.org>
 *
 *   The latest version of this program can be found at
 *  
 *   http://git.shaftnet.org/git/gitweb.cgi?p=selphy_print.git
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

#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

#include "es_print_common.h"

#define dump_data dump_data_linux

int dump_data_linux(int remaining, int present, int data_fd, 
		    int dev_fd, uint8_t *buf, uint16_t buflen)
{
	int cnt;
	int i;
	int wrote;

	while (remaining > 0) {
		cnt = read(data_fd, buf, (remaining < buflen) ? remaining : buflen);

		if (cnt < 0)
			return -1;

		if (present) {
			cnt += present;
			present = 0;
		}

		i = write(dev_fd, buf, cnt);
		if (i < 0)
			return wrote;
		if (i != cnt) {
			/* Realign buffer.. */
			present = cnt - i;
			memmove(buf, buf + i, present);
		}
		wrote += i;
		remaining -= cnt;
	}

	fprintf(stderr, "Wrote %d bytes (%d)\n", wrote, buflen);

	return wrote;
}

int main(int argc, char **argv) 
{
	int dev_fd, data_fd;

	uint8_t buffer[BUF_LEN];
	uint8_t rdbuf[READBACK_LEN];
	uint8_t rdbuf2[READBACK_LEN];

	int last_state, state = S_IDLE;
	int printer_type = P_END;
	int printer_type2 = P_END;
	int plane_len = 0;
	int bw_mode = 0;
	int16_t paper_code_offset = -1;
	int16_t paper_code = -1;

	/* Static initialization */
	setup_paper_codes();

	/* Cmdline help */
	if (argc < 2) {
		fprintf(stderr, "SELPHY ES/CP Print Assist version %s\n\nUsage:\n\t%s [ infile | - ] [ outdev ]\n",
			VERSION,
			argv[0]);
		fprintf(stderr, "\n");
		exit(1);
	}

	/* Open input file */
	data_fd = fileno(stdin);
	if (strcmp("-", argv[1])) {
		data_fd = open(argv[1], O_RDONLY);
		if (data_fd < 0) {
			perror("Can't open input file");
			exit(1);
		}
	}

	/* Open output device */
	dev_fd = open(argv[2], O_RDWR);
	if (dev_fd < 0) {
		perror("Can't open output device");
		exit(1);
	}

	/* Figure out the printer type based on the readback */
	read(dev_fd, rdbuf, READBACK_LEN);
	for (printer_type2 = 0; printer_type2 < P_END ; printer_type2++) {
		if (!fancy_memcmp(rdbuf, printers[printer_type2].init_readback, READBACK_LEN, -1, -1))
			break;
	}
	if (printer_type2 == P_END) {
		fprintf(stderr, "Unrecognized printer!\n");
		fprintf(stderr, "readback:  %02x %02x %02x %02x  %02x %02x %02x %02x  %02x %02x %02x %02x\n",
			rdbuf[0], rdbuf[1], rdbuf[2], rdbuf[3],
			rdbuf[4], rdbuf[5], rdbuf[6], rdbuf[7],
			rdbuf[8], rdbuf[9], rdbuf[10], rdbuf[11]);
	} 
  
	/* Figure out printer this file is intended for */
	read(data_fd, buffer, MAX_HEADER);

	printer_type = parse_printjob(buffer, &bw_mode, &plane_len);
	if (printer_type < 0) {
		return(-1);
	}

	if (printer_type != printer_type2) {
		fprintf(stderr, "File intended for a %s printer, aborting!\n", printers[printer_type].model);
		return (-1);
	} else {
		fprintf(stderr, "Printing a %s file\n", printers[printer_type].model);
	}

	plane_len += 12; /* Add in plane header */
	paper_code_offset = printers[printer_type].paper_code_offset;
	if (paper_code_offset != -1)
		paper_code = printers[printer_type].paper_codes[paper_code_offset];

top:

	read(dev_fd, rdbuf, READBACK_LEN);  /* Read the status from printer */
	if (memcmp(rdbuf, rdbuf2, READBACK_LEN)) {
		fprintf(stderr, "readback:  %02x %02x %02x %02x  %02x %02x %02x %02x  %02x %02x %02x %02x\n",
			rdbuf[0], rdbuf[1], rdbuf[2], rdbuf[3],
			rdbuf[4], rdbuf[5], rdbuf[6], rdbuf[7],
			rdbuf[8], rdbuf[9], rdbuf[10], rdbuf[11]);
		memcpy(rdbuf2, rdbuf, READBACK_LEN);
	} else {
		sleep(1);
	}
	if (state != last_state) {
		fprintf(stderr, "last_state %d new %d\n", last_state, state);
		last_state = state;
	}
	fflush(stderr);

	switch(state) {
	case S_IDLE:
		if (!fancy_memcmp(rdbuf, printers[printer_type].init_readback, READBACK_LEN, paper_code_offset, paper_code)) {
			state = S_PRINTER_READY;
			break;
		}
		break;
	case S_PRINTER_READY:
		fprintf(stderr, "Sending init sequence (%d bytes)\n", printers[printer_type].init_length);
		write(dev_fd, buffer, printers[printer_type].init_length); /* Send printer_init */
		/* Realign plane data to start of buffer.. */
		memmove(buffer, buffer+printers[printer_type].init_length,
			MAX_HEADER-printers[printer_type].init_length);

		state = S_PRINTER_INIT_SENT;
		break;
	case S_PRINTER_INIT_SENT:
		if (!fancy_memcmp(rdbuf, printers[printer_type].ready_y_readback, READBACK_LEN, paper_code_offset, paper_code)) {
			state = S_PRINTER_READY_Y;
		}
		break;
	case S_PRINTER_READY_Y:
		if (bw_mode)
			fprintf(stderr, "Sending BLACK plane\n");
		else
			fprintf(stderr, "Sending YELLOW plane\n");
		dump_data(plane_len, MAX_HEADER-printers[printer_type].init_length, data_fd, dev_fd, buffer, BUF_LEN);
		state = S_PRINTER_Y_SENT;
		break;
	case S_PRINTER_Y_SENT:
		if (!fancy_memcmp(rdbuf, printers[printer_type].ready_m_readback, READBACK_LEN, paper_code_offset, paper_code)) {
			if (bw_mode)
				state = S_PRINTER_DONE;
			else
				state = S_PRINTER_READY_M;
		}
		break;
	case S_PRINTER_READY_M:
		fprintf(stderr, "Sending MAGENTA plane\n");
		dump_data(plane_len, 0, data_fd, dev_fd, buffer, BUF_LEN);
		state = S_PRINTER_M_SENT;
		break;
	case S_PRINTER_M_SENT:
		if (!fancy_memcmp(rdbuf, printers[printer_type].ready_c_readback, READBACK_LEN, paper_code_offset, paper_code)) {
			state = S_PRINTER_READY_C;
		}
		break;
	case S_PRINTER_READY_C:
		fprintf(stderr, "Sending CYAN plane\n");
		dump_data(plane_len, 0, data_fd, dev_fd, buffer, BUF_LEN);
		state = S_PRINTER_C_SENT;
		break;
	case S_PRINTER_C_SENT:
		if (!fancy_memcmp(rdbuf, printers[printer_type].done_c_readback, READBACK_LEN, paper_code_offset, paper_code)) {
			state = S_PRINTER_DONE;
		}
		break;
	case S_PRINTER_DONE:
		if (printers[printer_type].foot_length) {
			fprintf(stderr, "Sending cleanup sequence\n");
			dump_data(printers[printer_type].foot_length, 0, data_fd, dev_fd, buffer, BUF_LEN);
		}
		state = S_FINISHED;
		break;
	case S_FINISHED:
		fprintf(stderr, "All data sent to printer!\n");
		break;
	}
	if (state != S_FINISHED)
		goto top;

	return 0;
}
