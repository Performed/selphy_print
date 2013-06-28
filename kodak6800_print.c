/*
 *   Kodak 6800 Photo Printer print assister
 *
 *   (c) 2013 Solomon Peachy <pizza@shaftnet.org>
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

#include <arpa/inet.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

#define VERSION "0.02"
#define URI_PREFIX "kodak6800://"
#define STR_LEN_MAX 64

#include "backend_common.c"

/* USB Identifiers */
#define USB_VID_KODAK      0x040A
#define USB_PID_KODAK_6800 0x4021

/* Program states */
enum {
	S_IDLE = 0,
	S_PRINTER_READY_HDR,
	S_PRINTER_SENT_HDR,
	S_PRINTER_SENT_HDR2,
	S_PRINTER_SENT_DATA,
	S_FINISHED,
};

struct kodak6800_hdr {
	uint8_t  hdr[9];
	uint8_t  copies;
	uint16_t columns; /* BE */
	uint16_t rows; /* BE */
	uint8_t  unk1;  /* 0x06 for 6x8, 0x00 for 6x4 */ 
	uint8_t  laminate; /* 0x01 to laminate, 0x00 for not */
	uint8_t  null;
} __attribute__((packed));

#define CMDBUF_LEN 17
#define READBACK_LEN 58

static int find_and_enumerate(struct libusb_context *ctx,
			      struct libusb_device ***list,
			      char *match_serno,
			      int scan_only)
{
	int num;
	int i;
	int found = -1;

	struct libusb_device_handle *dev;

	/* Enumerate and find suitable device */
	num = libusb_get_device_list(ctx, list);

	for (i = 0 ; i < num ; i++) {
		struct libusb_device_descriptor desc;
		unsigned char product[STR_LEN_MAX] = "";
		unsigned char serial[STR_LEN_MAX] = "";
		unsigned char manuf[STR_LEN_MAX] = "";

		libusb_get_device_descriptor((*list)[i], &desc);

		if (desc.idVendor != USB_VID_KODAK)
			continue;

		switch(desc.idProduct) {
		case USB_PID_KODAK_6800:
			found = i;
			break;
		default:
			continue;
		}

		if (libusb_open(((*list)[i]), &dev)) {
			ERROR("Could not open device %04x:%04x\n", desc.idVendor, desc.idProduct);
			found = -1;
			continue;
		}

		/* Query detailed info */
		if (desc.iManufacturer) {
			libusb_get_string_descriptor_ascii(dev, desc.iManufacturer, manuf, STR_LEN_MAX);
		}
		if (desc.iProduct) {
			libusb_get_string_descriptor_ascii(dev, desc.iProduct, product, STR_LEN_MAX);
		}
		if (desc.iSerialNumber) {
			libusb_get_string_descriptor_ascii(dev, desc.iSerialNumber, serial, STR_LEN_MAX);
		}

		DEBUG("PID: %04X Product: '%s' Serial: '%s'\n",
		      desc.idProduct, product, serial);

		if (scan_only) {
			/* URL-ify model. */
			char buf[128]; // XXX ugly..
			int j = 0, k = 0;
			char *ieee_id;
			while (*(product + j + strlen("Kodak"))) {
				buf[k] = *(product + j + strlen("Kodak "));
				if(buf[k] == ' ') {
					buf[k++] = '%';
					buf[k++] = '2';
					buf[k] = '0';
				}
				k++;
				j++;
			}
			ieee_id = get_device_id(dev);

			fprintf(stdout, "direct %sKodak/%s?serial=%s \"%s\" \"%s\" \"%s\" \"\"\n", URI_PREFIX,
			        buf, serial, product, product,
				ieee_id);

			if (ieee_id)
				free(ieee_id);
		}

		/* If a serial number was passed down, use it. */
		if (found && match_serno &&
		    strcmp(match_serno, (char*)serial)) {
			found = -1;
		}

		libusb_close(dev);
	}

	return found;
}

int main (int argc, char **argv) 
{
	struct libusb_context *ctx;
	struct libusb_device **list;
	struct libusb_device_handle *dev;
	struct libusb_config_descriptor *config;

	uint8_t endp_up = 0;
	uint8_t endp_down = 0;

	int data_fd = fileno(stdin);

	int i, num;
	int claimed;

	int ret = 0;
	int iface = 0;
	int found = -1;
	int copies = 1;
	char *uri = getenv("DEVICE_URI");;
	char *use_serno = NULL;

	struct kodak6800_hdr hdr;
	uint8_t *planedata, *cmdbuf;
	uint32_t datasize;

	uint8_t rdbuf[READBACK_LEN];
	uint8_t rdbuf2[READBACK_LEN];
	int last_state = -1, state = S_IDLE;

	/* Cmdline help */
	if (argc < 2) {
		DEBUG("Kodak 6800 Print Assist version %s\nUsage:\n\t%s [ infile | - ]\n\t%s job user title num-copies options [ filename ] \n\n",
		      VERSION,
		      argv[0], argv[0]);
		libusb_init(&ctx);
		find_and_enumerate(ctx, &list, NULL, 1);
		libusb_free_device_list(list, 1);
		libusb_exit(ctx);
		exit(1);
	}

	/* Are we running as a CUPS backend? */
	if (uri) {
		if (argv[4])
			copies = atoi(argv[4]);
		if (argv[6]) {  /* IOW, is it specified? */
			data_fd = open(argv[6], O_RDONLY);
			if (data_fd < 0) {
				perror("ERROR:Can't open input file");
				exit(1);
			}
		}

		/* Ensure we're using BLOCKING I/O */
		i = fcntl(data_fd, F_GETFL, 0);
		if (i < 0) {
			perror("ERROR:Can't open input");
			exit(1);
		}
		i &= ~O_NONBLOCK;
		i = fcntl(data_fd, F_SETFL, 0);
		if (i < 0) {
			perror("ERROR:Can't open input");
			exit(1);
		}
		/* Start parsing URI 'selphy://PID/SERIAL' */
		if (strncmp(URI_PREFIX, uri, strlen(URI_PREFIX))) {
			ERROR("Invalid URI prefix (%s)\n", uri);
			exit(1);
		}
		use_serno = strchr(uri, '=');
		if (!use_serno || !*(use_serno+1)) {
			ERROR("Invalid URI (%s)\n", uri);
			exit(1);
		}
		use_serno++;
	} else {
		/* Open Input File */
		if (strcmp("-", argv[1])) {
			data_fd = open(argv[1], O_RDONLY);
			if (data_fd < 0) {
				perror("ERROR:Can't open input file");
				exit(1);
			}
		}
	}

	/* Ignore SIGPIPE */
	signal(SIGPIPE, SIG_IGN);
	signal(SIGTERM, sigterm_handler);

	/* Read in then validate header */
	read(data_fd, &hdr, sizeof(hdr));
	if (hdr.hdr[0] != 0x03 ||
	    hdr.hdr[1] != 0x1b ||
	    hdr.hdr[2] != 0x43 ||
	    hdr.hdr[3] != 0x48 ||
	    hdr.hdr[4] != 0x43) {
		ERROR("Unrecognized data format!\n");
		exit(1);
	}

	/* Read in image data */
	cmdbuf = malloc(CMDBUF_LEN);
	datasize = be16_to_cpu(hdr.rows) * be16_to_cpu(hdr.columns) * 3;
	planedata = malloc(datasize + CMDBUF_LEN);
	if (!cmdbuf || !planedata) {
		ERROR("Memory allocation failure!\n");
		exit(1);
	}

	{
		int remain;
		uint8_t *ptr = planedata;
		remain = datasize;
		do {
			ret = read(data_fd, ptr, remain);
			if (ret < 0) {
				ERROR("Read failed (%d/%d/%d)\n", 
				      ret, remain, datasize);
				perror("ERROR: Read failed");
				exit(1);
			}
			ptr += ret;
			remain -= ret;
		} while (remain);
	}
	close(data_fd); /* We're done reading! */
	
	/* We need to pad the data with 17 * 0xff */
	memset (planedata + datasize, 0xff, CMDBUF_LEN);
	datasize += CMDBUF_LEN;

	/* Libusb setup */
	libusb_init(&ctx);
	found = find_and_enumerate(ctx, &list, use_serno, 0);

	if (found == -1) {
		ERROR("Printer open failure (No suitable printers found!)\n");
		ret = 3;
		goto done;
	}

	ret = libusb_open(list[found], &dev);
	if (ret) {
		ERROR("Printer open failure (Need to be root?) (%d)\n", ret);
		ret = 4;
		goto done;
	}
	
	claimed = libusb_kernel_driver_active(dev, iface);
	if (claimed) {
		ret = libusb_detach_kernel_driver(dev, iface);
		if (ret) {
			ERROR("Printer open failure (Could not detach printer from kernel)\n");
			ret = 4;
			goto done_close;
		}
	}

	ret = libusb_claim_interface(dev, iface);
	if (ret) {
		ERROR("Printer open failure (Could not claim printer interface)\n");
		ret = 4;
		goto done_close;
	}

	ret = libusb_get_active_config_descriptor(list[found], &config);
	if (ret) {
		ERROR("Printer open failure (Could not fetch config descriptor)\n");
		ret = 4;
		goto done_close;
	}

	for (i = 0 ; i < config->interface[0].altsetting[0].bNumEndpoints ; i++) {
		if ((config->interface[0].altsetting[0].endpoint[i].bmAttributes & 3) == LIBUSB_TRANSFER_TYPE_BULK) {
			if (config->interface[0].altsetting[0].endpoint[i].bEndpointAddress & LIBUSB_ENDPOINT_IN)
				endp_up = config->interface[0].altsetting[0].endpoint[i].bEndpointAddress;
			else
				endp_down = config->interface[0].altsetting[0].endpoint[i].bEndpointAddress;				
		}
	}

	/* Time for the main processing loop */

top:
	/* Send State Query */
	memset(cmdbuf, 0, CMDBUF_LEN);
	cmdbuf[0] = 0x03;
	cmdbuf[1] = 0x1b;
	cmdbuf[2] = 0x43;
	cmdbuf[3] = 0x48;
	cmdbuf[4] = 0x43;
	cmdbuf[5] = 0x03;

	if ((ret = send_data(dev, endp_down,
			    cmdbuf, CMDBUF_LEN - 1)))
		goto done_claimed;

	/* Read in the printer status */
	memset(rdbuf, 0, READBACK_LEN);
	ret = libusb_bulk_transfer(dev, endp_up,
				   rdbuf,
				   READBACK_LEN,
				   &num,
				   2000);

	if (ret < 0 || ((num != 51) && (num != 58))) {
		ERROR("Failure to receive data from printer (libusb error %d: (%d/%d from 0x%02x))\n", ret, num, READBACK_LEN, endp_up);
		ret = 4;
		goto done_claimed;
	}

	if (memcmp(rdbuf, rdbuf2, READBACK_LEN)) {
		DEBUG("readback:  %02x %02x %02x %02x  %02x %02x %02x %02x ...\n",
		      rdbuf[0], rdbuf[1], rdbuf[2], rdbuf[3],
		      rdbuf[4], rdbuf[5], rdbuf[6], rdbuf[7]);
		memcpy(rdbuf2, rdbuf, READBACK_LEN);
	} else {
		sleep(1);
	}
	if (state != last_state) {
		DEBUG("last_state %d new %d\n", last_state, state);
		last_state = state;
	}
	fflush(stderr);       

	switch (state) {
	case S_IDLE:
		INFO("Printing started\n");
		state = S_PRINTER_READY_HDR;
		break;
	case S_PRINTER_READY_HDR:
		INFO("Waiting for printer to become ready\n");
		if (rdbuf[0] != 0x01 ||
		    rdbuf[1] != 0x02 ||
		    rdbuf[2] != 0x01) {
			break;
		}

		/* Send reset/attention */
		memset(cmdbuf, 0, CMDBUF_LEN);
		cmdbuf[0] = 0x03;
		cmdbuf[1] = 0x1b;
		cmdbuf[2] = 0x43;
		cmdbuf[3] = 0x48;
		cmdbuf[4] = 0x43;
		cmdbuf[5] = 0x1a;
		if ((ret = send_data(dev, endp_down,
				     cmdbuf, CMDBUF_LEN -1)))
			goto done_claimed;
		state = S_PRINTER_SENT_HDR;
		break;
	case S_PRINTER_SENT_HDR:
		INFO("Waiting for printer to acknowledge start\n");		
		if (rdbuf[0] != 0x01 ||
		    rdbuf[1] != 0x03 ||
		    rdbuf[2] != 0x00) {
			break;
		}

		INFO("Sending image header\n");
		/* Send actual image header, altered slightly */
		memcpy(cmdbuf, &hdr, CMDBUF_LEN);
		cmdbuf[14] = 0x06;
		cmdbuf[16] = 0x01;
		if ((ret = send_data(dev, endp_down,
				     cmdbuf, CMDBUF_LEN)))
			goto done_claimed;

		state = S_PRINTER_SENT_HDR2;
	case S_PRINTER_SENT_HDR2:
		INFO("Waiting for printer to accept data\n");
		if (rdbuf[0] != 0x01 ||
		    rdbuf[1] != 0x02 ||
		    rdbuf[2] != 0x01) {
			break;
		}

		INFO("Sending image data\n");
		if ((ret = send_data(dev, endp_down, planedata, datasize)))
			goto done_claimed;
		state = S_PRINTER_SENT_DATA;
		break;
	case S_PRINTER_SENT_DATA:
		INFO("Waiting for printer to acknowledge completion\n");
		if (rdbuf[0] != 0x01 ||
		    rdbuf[1] != 0x02 ||
		    rdbuf[2] != 0x01) {
			break;
		}

		state = S_FINISHED;
		break;
	default:
		break;
	};

	if (state != S_FINISHED)
		goto top;

	/* Clean up */
	if (terminate)
		copies = 1;

	INFO("Print complete (%d remaining)\n", copies - 1);

	if (copies && --copies) {
		state = S_IDLE;
		goto top;
	}

	/* Done printing */
	INFO("All printing done\n");
	ret = 0;

done_claimed:
	libusb_release_interface(dev, iface);

done_close:
	if (claimed)
		libusb_attach_kernel_driver(dev, iface);

	libusb_close(dev);
done:
	if (planedata)
		free(planedata);
	if (cmdbuf)
		free(cmdbuf);

	libusb_free_device_list(list, 1);
	libusb_exit(ctx);

	return ret;
}

/* Kodak 6800 data format (6850 is similar, but not documented here yet)

  Spool file consists of 17-byte header followed by plane-interleaved BGR data.
  Native printer resolution is 1844 pixels per row, and 1240 or 2434 rows.

  Header:

  03 1b 43 48 43 0a 00 01 00     Fixed header
  01                             Number of copies
  XX XX                          Number of columns, big endian. Fixed at 1844
  XX XX                          Number of rows, big endian.
  XX                             Unknown, 0x00 or 0x06
  XX                             Laminate, 0x00 (off) or 0x01 (on)
  00

  ************************************************************************

  The data format actually sent to the Kodak 6800 is subtly different.

SP  03 1b 43 48 43 0a 00 01  00 CC WW WW HH HH DD LL 00

->  03 1b 43 48 43 03 00 00  00 00 00 00 00 00 00 00
<-  [51 octets]

    01 02 01 00 00 00 00 00  00 00 a2 7b 00 00 a2 7b
    00 00 02 f4 00 00 e6 b1  00 00 00 1a 00 03 00 e8
    00 01 00 83 00 00 00 00  00 00 00 00 00 00 00 00
    00 00 00

->  03 1b 43 48 43 03 00 00  00 00 00 00 00 00 00 00
<-  [51 octets -- same as above]

->  03 1b 43 48 43 1a 00 00  00 00 00 00 00 00 00 00
<-  [58 octets]

    01 03 00 00 00 00 00 04  06 WW WW 09 82 01 00 00  [09 82 == 2434 == 6x8!]
    00 00 06 WW WW 09 ba 01  02 00 00 00 06 WW WW 04
    d8 01 01 00 00 00 06 WW  WW 09 82 01 03 00 00 00
    00 00 00 00 00 00 00 00  00 00

->  03 1b 43 48 43 0a 00 01  00 01 07 34 04 d8 06 01 01
<-  [51 octets]

    01 02 01 00 00 00 00 00  00 00 a2 7b 00 00 a2 7b
    00 00 02 f4 00 00 e6 b1  00 00 00 1a 00 03 00 e8
    00 01 00 83 01 00 00 01  00 00 00 01 00 00 00 00
    00 00 00

->  [4K of plane data]
->  ...
->  [4K of plane data]
->  [remainder of plane data + 17 bytes of 0xff]

->  03 1b 43 48 43 03 00 00  00 00 00 00 00 00 00 00
<-  [51 octets]

    01 02 01 00 00 00 00 00  00 00 a2 7b 00 00 a2 7b
    00 00 02 f4 00 00 e6 b1  00 00 00 1a 00 03 00 e8
    00 01 00 83 01 00 00 01  00 00 00 01 00 00 00 00
    00 00 00

->  03 1b 43 48 43 03 00 00  00 00 00 00 00 00 00 00
<-  [51 octets, repeats]



*/
