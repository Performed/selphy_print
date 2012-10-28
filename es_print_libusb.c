/*
 *   Canon SELPHY series print assister -- libusb-1.0 version
 *
 *   (c) 2007-2012 Solomon Peachy <pizza@shaftnet.org>
 *
 *   The latest version of this program can be found at:
 *  
 *     http://git.shaftnet.org/git/gitweb.cgi?p=selphy_print.git
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <libusb-1.0/libusb.h>

#include "es_print_common.h"

#define dump_data dump_data_libusb

/* USB Identifiers */
#define USB_VID_CANON       0x04a9
#define USB_PID_CANON_ES1   0x3141
#define USB_PID_CANON_ES2   0x3185 
#define USB_PID_CANON_ES20  20  // XXX
#define USB_PID_CANON_ES3   3   // XXX
#define USB_PID_CANON_ES30  0x31B0
#define USB_PID_CANON_ES40  0x31EE
#define USB_PID_CANON_CP10  0x304A
#define USB_PID_CANON_CP100 0x3063
#define USB_PID_CANON_CP200 200 // XXX
#define USB_PID_CANON_CP220 0x30BD
#define USB_PID_CANON_CP300 0x307D
#define USB_PID_CANON_CP330 0x30BE
#define USB_PID_CANON_CP400 0x30F6
#define USB_PID_CANON_CP500 500 // XXX
#define USB_PID_CANON_CP510 510 // XXX
#define USB_PID_CANON_CP520 520 // XXX
#define USB_PID_CANON_CP530 530 // XXX
#define USB_PID_CANON_CP600 0x310B
#define USB_PID_CANON_CP710 710 // XXX
#define USB_PID_CANON_CP720 720 // XXX
#define USB_PID_CANON_CP730 730 // XXX
#define USB_PID_CANON_CP740 0x3171
#define USB_PID_CANON_CP750 750 // XXX
#define USB_PID_CANON_CP760 760 // XXX
#define USB_PID_CANON_CP770 770 // XXX
#define USB_PID_CANON_CP780 780 // XXX
#define USB_PID_CANON_CP790 790 // XXX
#define USB_PID_CANON_CP800 0x3214
#define USB_PID_CANON_CP810 810 // XXX -- completely unknown type
#define USB_PID_CANON_CP900 900 // XXX -- completely unknown type

#define ENDPOINT_UP   0x81
#define ENDPOINT_DOWN 0x02

static int dump_data_libusb(int remaining, int present, int data_fd, 
			    struct libusb_device_handle *dev, 
			    uint8_t *buf, uint16_t buflen) {
	int cnt;
	int i;
	int wrote;
	int num;

	while (remaining > 0) {
		cnt = read(data_fd, buf + present, (remaining < (buflen-present)) ? remaining : (buflen-present));
		
		if (cnt < 0)
			return -1;

		if (present) {
			cnt += present;
			present = 0;
		}

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
	
	fprintf(stderr, "Wrote %d bytes\n", wrote);
	
	return wrote;
}

int main (int argc, char **argv)
{
	struct libusb_context *ctx;
	struct libusb_device **list;
	struct libusb_device_handle *dev;

	int printer_type = P_END;

	int iface = 0;

	int num, i;
	int ret = 0;
	int claimed;

	uint8_t rdbuf[READBACK_LEN], rdbuf2[READBACK_LEN];
	int last_state = -1, state = S_IDLE;

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
		fprintf(stderr, "SELPHY Print Assist version %s\n\nUsage:\n\t%s [ infile | - ]\n",
			VERSION,
			argv[0]);
		fprintf(stderr, "\n");
		exit(1);
	}

	/* Open Input File */
	if (strcmp("-", argv[1])) {
		data_fd = open(argv[1], O_RDONLY);
		if (data_fd < 0) {
			perror("Can't open input file");
			exit(1);
		}
	}

	/* Figure out printer this file is intended for */
	read(data_fd, buffer, MAX_HEADER);

	printer_type = parse_printjob(buffer, &bw_mode, &plane_len);
	if (printer_type < 0) {
		return(-1);
	}

	fprintf(stderr, "File intended for a '%s' printer %s\r\n",  printers[printer_type].model, bw_mode? "B/W" : "");

	plane_len += 12; /* Add in plane header */
	paper_code_offset = printers[printer_type].paper_code_offset;
	if (paper_code_offset != -1)
		paper_code = printers[printer_type].paper_codes[paper_code_offset];

	/* Libusb setup */
	libusb_init(&ctx);

	/* Enumerate and find suitable device */
	num = libusb_get_device_list(ctx, &list);

	for (i = 0 ; i < num ; i++) {
		struct libusb_device_descriptor desc;

		libusb_get_device_descriptor(list[i], &desc);

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
		case USB_PID_CANON_CP10:
		case USB_PID_CANON_CP100:
		case USB_PID_CANON_CP200:
		case USB_PID_CANON_CP220:
		case USB_PID_CANON_CP300:
		case USB_PID_CANON_CP330:
		case USB_PID_CANON_CP400:
		case USB_PID_CANON_CP500:
		case USB_PID_CANON_CP510:
		case USB_PID_CANON_CP520:
		case USB_PID_CANON_CP530:
		case USB_PID_CANON_CP600:
		case USB_PID_CANON_CP710:
		case USB_PID_CANON_CP720:
		case USB_PID_CANON_CP730:
		case USB_PID_CANON_CP740:
		case USB_PID_CANON_CP750:
		case USB_PID_CANON_CP760:
		case USB_PID_CANON_CP770:
		case USB_PID_CANON_CP780:
		case USB_PID_CANON_CP800:
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
		fprintf(stderr, "No suitable printers found (looking for %s)\n", printers[printer_type].model);
		goto done;
	}

found2:

#if 0
	// XXX pull interface list, and make sure we have the right one.
		if (interface.bInterfaceClass != LIBUSB_CLASS_PRINTER)
			continue;
#endif


	fprintf(stderr, "Found a %s printer\r\n", printers[printer_type].model);

	ret = libusb_open(list[i], &dev);
	if (ret) {
		fprintf(stderr, "Could not open device (Need to be root?) (%d)\r\n", ret);
		goto done;
	}
	
	claimed = libusb_kernel_driver_active(dev, iface);
	if (claimed) {
		ret = libusb_detach_kernel_driver(dev, iface);
		if (ret) {
			fprintf(stderr, "Could not detach printer from kernel (%d)\r\n", ret);
			goto done;
		}
	}

	ret = libusb_claim_interface(dev, iface);
	if (ret) {
		fprintf(stderr, "Could not claim printer interface (%d)\r\n", ret);
		goto done;
	}

top:
	/* Read in the printer status */
	ret = libusb_bulk_transfer(dev, ENDPOINT_UP,
				   rdbuf,
				   READBACK_LEN,
				   &num,
				   2000);
	if (ret < 0) {
		fprintf(stderr, "libusb error (%d)\n", ret);
		goto done;
	}

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
		}
		break;
	case S_PRINTER_READY:
		fprintf(stderr, "Sending init sequence (%d bytes)\n", printers[printer_type].init_length);

		/* Send printer init */
		ret = libusb_bulk_transfer(dev, ENDPOINT_DOWN,
					   buffer,
					   printers[printer_type].init_length,
					   &num,
					   2000);
		if (ret < 0) {
			fprintf(stderr, "libusb error (%d) (%d)\n", ret, num);
			goto done;
		}

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
		dump_data(plane_len, MAX_HEADER-printers[printer_type].init_length, data_fd, dev, buffer, BUF_LEN);
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
		dump_data(plane_len, 0, data_fd, dev, buffer, BUF_LEN);
		state = S_PRINTER_M_SENT;
		break;
	case S_PRINTER_M_SENT:
		if (!fancy_memcmp(rdbuf, printers[printer_type].ready_c_readback, READBACK_LEN, paper_code_offset, paper_code)) {
			state = S_PRINTER_READY_C;
		}
		break;
	case S_PRINTER_READY_C:
		fprintf(stderr, "Sending CYAN plane\n");
		dump_data(plane_len, 0, data_fd, dev, buffer, BUF_LEN);
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
			dump_data(printers[printer_type].foot_length, 0, data_fd, dev, buffer, BUF_LEN);
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
	
	libusb_release_interface(dev, iface);

	if (claimed)
		libusb_attach_kernel_driver(dev, iface);

	libusb_close(dev);

done:
	libusb_free_device_list(list, 1);
	libusb_exit(ctx);

	close(data_fd);

	return ret;
}
