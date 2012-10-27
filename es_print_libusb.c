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

// Compile with gcc -o es_print print_libusb.c -lusb-1.0 -Wall

#define VERSION "0.20"

#include <stdio.h>
#include <stdlib.h>
#include <libusb-1.0/libusb.h>

#if (__BYTE_ORDER == __LITTLE_ENDIAN)
#define cpu_to_le32(__x) __x
#else
#define cpu_to_le32(x) \
({ \
        uint32_t __x = (x); \
        ((uint32_t)( \
                (((uint32_t)(__x) & (uint32_t)0x000000ffUL) << 24) | \
                (((uint32_t)(__x) & (uint32_t)0x0000ff00UL) <<  8) | \
                (((uint32_t)(__x) & (uint32_t)0x00ff0000UL) >>  8) | \
                (((uint32_t)(__x) & (uint32_t)0xff000000UL) >> 24) )); \
})
#endif

/* Printer types */
enum {
	P_ES1 = 0,
	P_ES2_20,
	P_ES3_30,
	P_ES40,
	P_CP_XXX,
	P_END
};

static char *models[P_END] = { "SELPHY ES1",
			       "SELPHY ES2/ES20",
			       "SELPHY ES3/ES30",
			       "SELPHY ES40/CP790",
			       "SELPHY CP Series (Except CP790)",
};

#define MAX_HEADER 28

static const int init_lengths[P_END] = { 12, 16, 16, 16, 12 };
static const int foot_lengths[P_END] = { 0, 0, 12, 12, 0 };

/* Does NOT include header length! */
#define RDBUF_LEN 12

static const int es40_plane_lengths[4] = { 2227456, 1601600, 698880, 2976512 };

static const int16_t init_readbacks[P_END][RDBUF_LEN] = { { 0x02, 0x00, 0x00, 0x00, 0x02, 0x01, -1, 0x01, 0x00, 0x00, 0x00, 0x00 },
							  { 0x02, 0x00, 0x00, 0x00, -1, 0x00, -1, -1, 0x00, 0x00, 0x00, 0x00 },
							  { 0x00, 0xff, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 },
							  { 0xde, 0xad, 0xba, 0xbe }, // XXX ES40/CP790
							  { 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, -1, 0x00, 0x00, 0x00, 0x00, 0x00 },
};

static const int16_t ready_y_readbacks[P_END][RDBUF_LEN] = { { 0x04, 0x00, 0x01, 0x00, 0x02, 0x01, -1, 0x01, 0x00, 0x00, 0x00, 0x00 },
							     { 0x03, 0x00, 0x01, 0x00, -1, 0x00, -1, -1, 0x00, 0x00, 0x00, 0x00 },
							     { 0x01, 0xff, 0x01, 0x00, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 },
							     { 0xde, 0xad, 0xba, 0xbe }, // XXX ES40/CP790
							     { 0x02, 0x00, 0x00, 0x00, 0x70, 0x00, -1, 0x00, 0x00, 0x00, 0x00, 0x00 },
};

static const int16_t ready_m_readbacks[P_END][RDBUF_LEN] = { { 0x04, 0x00, 0x03, 0x00, 0x02, 0x01, -1, 0x01, 0x00, 0x00, 0x00, 0x00 },
							     { 0x06, 0x00, 0x03, 0x00, -1, 0x00, -1, -1, 0x00, 0x00, 0x00, 0x00 },
							     { 0x03, 0xff, 0x02, 0x00, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 },
							     { 0xde, 0xad, 0xba, 0xbe }, // XXX ES40/CP790
							     { 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, -1, 0x00, 0x00, 0x00, 0x00, 0x00 },
};

static const int16_t ready_c_readbacks[P_END][RDBUF_LEN] = { { 0x04, 0x00, 0x07, 0x00, 0x02, 0x01, -1, 0x01, 0x00, 0x00, 0x00, 0x00 },
							     { 0x09, 0x00, 0x07, 0x00, -1, 0x00, -1, -1, 0x00, 0x00, 0x00, 0x00 },
							     { 0x05, 0xff, 0x03, 0x00, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 },
							     { 0xde, 0xad, 0xba, 0xbe }, // XXX ES40/CP790
							     { 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, -1, 0x00, 0x00, 0x00, 0x00, 0x00 },
};

static const int16_t done_c_readbacks[P_END][RDBUF_LEN] = { { 0x04, 0x00, 0x00, 0x00, 0x02, 0x01, -1, 0x01, 0x00, 0x00, 0x00, 0x00 },
							    { 0x09, 0x00, 0x00, 0x00, -1, 0x00, -1, -1, 0x00, 0x00, 0x00, 0x00 },
							    { 0x00, 0xff, 0x10, 0x00, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 },
							    { 0xde, 0xad, 0xba, 0xbe }, // XXX ES40/CP790
							    { 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, -1, 0x00, 0x00, 0x00, 0x00, 0x00 },
};

static int16_t paper_codes[P_END][256];
static const int16_t paper_code_offsets[P_END] = { 6, 4, -1, -1, 6 };

static void setup_paper_codes(void)
{
	/* Default all to IGNORE */
	int i, j;
	for (i = 0; i < P_END ; i++)
		for (j = 0 ; j < 256 ; j++) 
			paper_codes[i][j] = -1;
	
	/* SELPHY ES1 paper codes */
	paper_codes[P_ES1][0x11] = 0x01;
	paper_codes[P_ES1][0x12] = 0x02;  // ??
	paper_codes[P_ES1][0x13] = 0x03;
	
	/* SELPHY ES2/20 paper codes */
	paper_codes[P_ES2_20][0x01] = 0x01;
	paper_codes[P_ES2_20][0x02] = 0x02; // ??
	paper_codes[P_ES2_20][0x03] = 0x03;
	
	/* SELPHY ES3/30 paper codes */
	//  paper_codes[P_ES3_30][0x01] = -1;
	//  paper_codes[P_ES3_30][0x02] = -1;
	//  paper_codes[P_ES3_30][0x03] = -1;
	
	/* SELPHY ES40/CP790 paper codes */
	//  paper_codes[P_ES40][0x00] = -1;
	//  paper_codes[P_ES40][0x01] = -1;
	//  paper_codes[P_ES40][0x02] = -1;
	//  paper_codes[P_ES40][0x03] = -1;

	/* SELPHY CP-760 paper codes */
	paper_codes[P_CP_XXX][0x01] = 0x11;
	paper_codes[P_CP_XXX][0x02] = 0x22;
	//  paper_codes[P_CP_XXX][0x03] = -1;
	//  paper_codes[P_CP_XXX][0x04] = -1;
}

/* USB Identifiers */
#define USB_VID_CANON     0x0a49
#define USB_PID_CANON_ES1 0x3184 // XXX
#define USB_PID_CANON_ES2 0x3185

/* Program states */
enum {
	S_IDLE = 0,
	S_PRINTER_READY,
	S_PRINTER_INIT_SENT,
	S_PRINTER_READY_Y,
	S_PRINTER_Y_SENT,
	S_PRINTER_READY_M,
	S_PRINTER_M_SENT,
	S_PRINTER_READY_C,
	S_PRINTER_C_SENT,
	S_PRINTER_DONE,
	S_FINISHED,
};

int main (int argc, char **argv)
{
	struct libusb_context *ctx;
	struct libusb_device **list;
	struct libusb_device_handle *dev;

	int printer_type = P_END;

	int num, i;
	int ret = 0;
	int claimed;

	/* Cmdline help */
	if (argc < 2) {
		fprintf(stderr, "SELPHY ES Print Assist version %s\n\nUsage:\n\t%s infile\n",
			VERSION,
			argv[0]);
		fprintf(stderr, "\n");
		exit(1);
	}

	/* Static initialization */
	setup_paper_codes();

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
			printer_type = P_ES1;
		case USB_PID_CANON_ES2:
			printer_type = P_ES2_20;
			break;
		default:
			fprintf(stderr, "Found Unrecognized Canon Printer: %04x\n", 
				desc.idProduct);
			ret = 1;
			goto done;
		}
		
		break;
	}

	if (i == num) {
		ret = 1;
		fprintf(stderr, "No suitable printers found\n");
		goto done;
	}

	fprintf(stderr, "Found a %s printer\r\n", models[printer_type]);

	libusb_open(list[i], &dev);
	
	claimed = libusb_kernel_driver_active(dev, 0);
	if (claimed)
		libusb_detach_kernel_driver(dev, 0);

	libusb_claim_interface(dev, 0);


	//XXX do something..


	libusb_release_interface(dev, 0);

	if (claimed)
		libusb_attach_kernel_driver(dev, 0);

	libusb_close(dev);

done:
	libusb_free_device_list(list, 1);
	libusb_exit(ctx);
	return ret;
}
