/*
 *   Canon SELPHY ES series print assister 
 *
 *   (c) 2007-2012 Solomon Peachy <pizza@shaftnet.org>
 *
 *   The SELPHY ES-series printers from Canon requires intelligent buffering
 *   of the raw spool data in order to keep the printer from locking up.  
 *
 *   Known supported printers:
 * 
 *     SELPHY ES1, SELPHY ES2, SELPHY ES30 
 *
 *   Supported but untested:
 *
 *     SELPHY ES20, SELPHY ES3 
 *     SELPHY CP-760 and all other CP-series printers EXCEPT for CP-790
 * 
 *   NOT currently supported:
 *
 *     SELPHY ES40, CP-790
 *       (They use different stream formats, and may have different readbacks;
 *        I will update this as needed and as I get hardware to test..)
 *
 *   The latest version of this program can be found at:
 *  
 *     http://www.shaftnet.org/users/pizza/es_print_assist.c
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

// Compile with gcc -o es_print es_print_libusb.c -lusb-1.0 -Wall

#define VERSION "0.20"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <libusb-1.0/libusb.h>

#include "es_print_common.h"

/* USB Identifiers */
#define USB_VID_CANON     0x0a49
#define USB_PID_CANON_ES1 0x3184 // XXX
#define USB_PID_CANON_ES2 0x3185 
#define USB_PID_CANON_ES20 1 //
#define USB_PID_CANON_ES3 2 
#define USB_PID_CANON_ES30 3
#define USB_PID_CANON_ES40 4
#define USB_PID_CANON_CP790 5
#define USB_PID_CANON_CP_XXX 6

#define ENDPOINT_UP   0x00
#define ENDPOINT_DOWN 0x01

#define BUF_LEN 4096

static int dump_data_libusb (int remaining, int present, int data_fd, 
			     struct libusb_device_handle *dev, 
			     uint8_t *buf, uint16_t buflen) {
	int cnt;
	int i;
	int wrote;
	int num;

	while (remaining) {
		cnt = read(data_fd, buf + present, (remaining < (buflen-present)) ? remaining : (buflen-present));
		if (present)
			present = 0;
		
		if (cnt < 0)
			return -1;

		i = libusb_bulk_transfer(dev, ENDPOINT_DOWN,
					 buf,
					 cnt,
					 &num,
					 2000);
		if (i < 0) {
			fprintf(stderr, "libusb error(%d)\n", i);
			return -1;
		}

		if (num != cnt) {
			/* Realign buffer.. */
			present = cnt - num;
			memmove(buf, buf + num, present);
		}
		wrote += num;
		remaining -= num;
	}
	
	fprintf(stderr, "Wrote %d bytes (%d)\n", wrote, buflen);
	
	return wrote;
}

int main (int argc, char **argv)
{
	struct libusb_context *ctx;
	struct libusb_device **list;
	struct libusb_device_handle *dev;

	int printer_type = P_END;

	int num, i;
	int ret = 0;
	int claimed;

	uint8_t rdbuf[RDBUF_LEN], rdbuf2[RDBUF_LEN];
	int last_state, state = S_IDLE;

	int plane_len = 0;

	int bw_mode = 0;
	int16_t paper_code_offset = -1;
	int16_t paper_code = -1;

	uint8_t buffer[BUF_LEN];

	int data_fd = fileno(stdin);

	/* Static initialization */
	setup_paper_codes();
	
	/* Cmdline help */
	if (argc < 2) {
		fprintf(stderr, "SELPHY ES Print Assist version %s\n\nUsage:\n\t%s infile\n",
			VERSION,
			argv[0]);
		fprintf(stderr, "\n");
		exit(1);
	}

	/* Open Input File */
	if (!strcmp("-", argv[1])) {
		data_fd = open(argv[1], O_RDONLY);
		if (data_fd < 0) {
			perror("Can't open input file");
			exit(1);
		}
	}

	/* Figure out printer this file is intended for */
	read(data_fd, buffer, MAX_HEADER);
	// printer_type, bw_mode

	if (buffer[0] != 0x40 &&
	    buffer[1] != 0x00) {
		fprintf(stderr, "Unrecognized file format!\n");
		return(-1);
	}

	if (buffer[12] == 0x40 &&
	    buffer[13] == 0x01) {
		if (buffer[2] == 0x00) {
			printer_type = P_CP_XXX;  /* Unpadded */
		} else {
			printer_type = P_ES1;
			bw_mode = (buffer[2] == 0x20);
		}
		
		plane_len = *(uint32_t*)(&buffer[16]);
		plane_len = cpu_to_le32(plane_len);
		goto found;
	}

	plane_len = cpu_to_le32(plane_len);
	plane_len = *(uint32_t*)(&buffer[12]);

	if (buffer[16] == 0x40 &&
	    buffer[17] == 0x01) {
		if (buffer[4] == 0x02) {
			printer_type = P_ES2_20;
			bw_mode = (buffer[7] == 0x01);
			goto found;
		}
		if (es40_plane_lengths[buffer[2]] == plane_len) {
			printer_type = P_ES40; 
			bw_mode = (buffer[3] == 0x01);
			goto found;
		} else {
			printer_type = P_ES3_30; 
			bw_mode = (buffer[3] == 0x01);
			goto found;
		}
	}

	fprintf(stderr, "Unrecognized file format!\n");
	return -1;
	
found:

	plane_len += 12; /* Add in plane header */
	paper_code_offset = paper_code_offsets[printer_type];
	if (paper_code_offset != -1)
		paper_code = paper_codes[printer_type][paper_code_offset];

	/* Libusb setup */
	libusb_init(&ctx);

	/* Enumerate and find suitable device */
	num = libusb_get_device_list(ctx, &list);

	for (i = 0 ; i < num ; i++) {
		struct libusb_device_descriptor desc;

		libusb_get_device_descriptor(list[i], &desc);

		if (desc.bDeviceClass != LIBUSB_CLASS_PRINTER)
			continue;

		if (desc.idVendor != USB_VID_CANON)
			continue;

		switch(desc.idProduct) {
		case USB_PID_CANON_ES1:
			if (printer_type == P_ES1)
				goto found2;
			break;
		case USB_PID_CANON_ES2:
		case USB_PID_CANON_ES20:
			if (printer_type == P_ES2_20)
				goto found2;
			break;
		case USB_PID_CANON_ES3:
		case USB_PID_CANON_ES30:
			if (printer_type == P_ES3_30)
				goto found2;
			break;
		case USB_PID_CANON_ES40:
		case USB_PID_CANON_CP790:
			if (printer_type == P_ES40)
				goto found2;
			break;
		case USB_PID_CANON_CP_XXX:
			if (printer_type == P_CP_XXX)
				goto found2;
			break;
		default:
			fprintf(stderr, "Found Unrecognized Canon Printer: %04x\n", 
				desc.idProduct);
			break;
		}
	}

	if (i == num) {
		ret = 1;
		fprintf(stderr, "No suitable printers found (looking for %s)\n", models[printer_type]);
		goto done;
	}

found2:
	fprintf(stderr, "Found a %s printer\r\n", models[printer_type]);

	libusb_open(list[i], &dev);
	
	claimed = libusb_kernel_driver_active(dev, 0);
	if (claimed)
		libusb_detach_kernel_driver(dev, 0);

	libusb_claim_interface(dev, 0);

top:
	/* Read in the printer status */
	ret = libusb_bulk_transfer(dev, ENDPOINT_UP,
				   rdbuf,
				   RDBUF_LEN,
				   &num,
				   2000);
	if (ret < 0) {
		fprintf(stderr, "libusb error (%d)\n", ret);
		goto done;
	}

	if (memcmp(rdbuf, rdbuf2, RDBUF_LEN)) {
		fprintf(stderr, "readback:  %02x %02x %02x %02x  %02x %02x %02x %02x  %02x %02x %02x %02x\n",
			rdbuf[0], rdbuf[1], rdbuf[2], rdbuf[3],
			rdbuf[4], rdbuf[5], rdbuf[6], rdbuf[7],
			rdbuf[8], rdbuf[9], rdbuf[10], rdbuf[11]);
		memcpy(rdbuf2, rdbuf, RDBUF_LEN);
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
		if (!fancy_memcmp(rdbuf, init_readbacks[printer_type], RDBUF_LEN, paper_code_offset, paper_code)) {
			state = S_PRINTER_READY;
		}
		break;
	case S_PRINTER_READY:
		fprintf(stderr, "Sending init sequence (%d bytes)\n", init_lengths[printer_type]);

		/* Send printer init */
		ret = libusb_bulk_transfer(dev, ENDPOINT_DOWN,
					   buffer,
					   init_lengths[printer_type],
					   &num,
					   2000);
		if (ret < 0) {
			fprintf(stderr, "libusb error (%d)\n", ret);
			goto done;
		}

		/* Realign plane data to start of buffer.. */
		memmove(buffer, buffer+init_lengths[printer_type],
			MAX_HEADER-init_lengths[printer_type]);

		state = S_PRINTER_INIT_SENT;
		break;
	case S_PRINTER_INIT_SENT:
		if (!fancy_memcmp(rdbuf, ready_y_readbacks[printer_type], RDBUF_LEN, paper_code_offset, paper_code)) {
			state = S_PRINTER_READY_Y;
		}
		break;
	case S_PRINTER_READY_Y:
		if (bw_mode)
			fprintf(stderr, "Sending BLACK plane\n");
		else
			fprintf(stderr, "Sending YELLOW plane\n");
		dump_data_libusb(plane_len, MAX_HEADER-init_lengths[printer_type], data_fd, dev, buffer, BUF_LEN);
		state = S_PRINTER_Y_SENT;
		break;
	case S_PRINTER_Y_SENT:
		// handle bw_mode?  transition to S_PRINTER_DONE?
		if (!fancy_memcmp(rdbuf, ready_m_readbacks[printer_type], RDBUF_LEN, paper_code_offset, paper_code)) {
			state = S_PRINTER_READY_M;
		}
		break;
	case S_PRINTER_READY_M:
		fprintf(stderr, "Sending MAGENTA plane\n");
		dump_data_libusb(plane_len, 0, data_fd, dev, buffer, BUF_LEN);
		state = S_PRINTER_M_SENT;
		break;
	case S_PRINTER_M_SENT:
		if (!fancy_memcmp(rdbuf, ready_c_readbacks[printer_type], RDBUF_LEN, paper_code_offset, paper_code)) {
			state = S_PRINTER_READY_C;
		}
		break;
	case S_PRINTER_READY_C:
		fprintf(stderr, "Sending CYAN plane\n");
		dump_data_libusb(plane_len, 0, data_fd, dev, buffer, BUF_LEN);
		state = S_PRINTER_C_SENT;
		break;
	case S_PRINTER_C_SENT:
		if (!fancy_memcmp(rdbuf, done_c_readbacks[printer_type], RDBUF_LEN, paper_code_offset, paper_code)) {
			state = S_PRINTER_DONE;
		}
		break;
	case S_PRINTER_DONE:
		if (foot_lengths[printer_type]) {
			fprintf(stderr, "Sending cleanup sequence\n");
			dump_data_libusb(foot_lengths[printer_type], 0, data_fd, dev, buffer, BUF_LEN);
		}
		state = S_FINISHED;
		break;
	case S_FINISHED:
		fprintf(stderr, "All data sent to printer!\n");
		break;
	}
	if (state != S_FINISHED)
		goto top;

	/* Done printing */
	
	libusb_release_interface(dev, 0);

	if (claimed)
		libusb_attach_kernel_driver(dev, 0);

	libusb_close(dev);

done:
	libusb_free_device_list(list, 1);
	libusb_exit(ctx);

	close(data_fd);

	return ret;
}
