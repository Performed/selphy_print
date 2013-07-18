/*
 *   CUPS Backend common code
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

#include "backend_common.h"

#define BACKEND_VERSION "0.6"
#ifndef URI_PREFIX
#define URI_PREFIX "gutenprint+usb"
#endif

#define USB_VID_CANON       0x04a9
#define USB_PID_CANON_CP10  0x304A
#define USB_PID_CANON_CP100 0x3063
#define USB_PID_CANON_CP200 0x307C
#define USB_PID_CANON_CP220 0x30BD
#define USB_PID_CANON_CP300 0x307D
#define USB_PID_CANON_CP330 0x30BE
#define USB_PID_CANON_CP400 0x30F6
#define USB_PID_CANON_CP500 0x30F5
#define USB_PID_CANON_CP510 0x3128
#define USB_PID_CANON_CP520 520 // XXX 316f? 3172? (related to cp740/cp750)
#define USB_PID_CANON_CP530 0x31b1
#define USB_PID_CANON_CP600 0x310B
#define USB_PID_CANON_CP710 0x3127
#define USB_PID_CANON_CP720 0x3143
#define USB_PID_CANON_CP730 0x3142
#define USB_PID_CANON_CP740 0x3171
#define USB_PID_CANON_CP750 0x3170
#define USB_PID_CANON_CP760 0x31AB
#define USB_PID_CANON_CP770 0x31AA
#define USB_PID_CANON_CP780 0x31DD
#define USB_PID_CANON_CP790 790 // XXX 31ed? 31ef? (related to es40)
#define USB_PID_CANON_CP800 0x3214
#define USB_PID_CANON_CP810 0x3256
#define USB_PID_CANON_CP900 0x3255
#define USB_PID_CANON_ES1   0x3141
#define USB_PID_CANON_ES2   0x3185
#define USB_PID_CANON_ES20  0x3186
#define USB_PID_CANON_ES3   0x31AF
#define USB_PID_CANON_ES30  0x31B0
#define USB_PID_CANON_ES40  0x31EE
#define USB_VID_SONY      0x054C
#define USB_PID_SONY_UPDR150  0x01E8
#define USB_VID_KODAK      0x040A
#define USB_PID_KODAK_6800 0x4021
#define USB_PID_KODAK_1400 0x4022
#define USB_PID_KODAK_805  0x4034
#define USB_VID_SHINKO       0x10CE
#define USB_PID_SHINKO_S2145 0x000E

static struct device_id devices[] = {
	{ USB_VID_CANON, USB_PID_CANON_CP10, P_CP10, "Canon"},
	{ USB_VID_CANON, USB_PID_CANON_CP100, P_CP_XXX, "Canon"},
	{ USB_VID_CANON, USB_PID_CANON_CP200, P_CP_XXX, "Canon"},
	{ USB_VID_CANON, USB_PID_CANON_CP220, P_CP_XXX, "Canon"},
	{ USB_VID_CANON, USB_PID_CANON_CP300, P_CP_XXX, "Canon"},
	{ USB_VID_CANON, USB_PID_CANON_CP330, P_CP_XXX, "Canon"},
	{ USB_VID_CANON, USB_PID_CANON_CP400, P_CP_XXX, "Canon"},
	{ USB_VID_CANON, USB_PID_CANON_CP500, P_CP_XXX, "Canon"},
	{ USB_VID_CANON, USB_PID_CANON_CP510, P_CP_XXX, "Canon"},
	{ USB_VID_CANON, USB_PID_CANON_CP520, P_CP_XXX, "Canon"},
	{ USB_VID_CANON, USB_PID_CANON_CP530, P_CP_XXX, "Canon"},
	{ USB_VID_CANON, USB_PID_CANON_CP600, P_CP_XXX, "Canon"},
	{ USB_VID_CANON, USB_PID_CANON_CP710, P_CP_XXX, "Canon"},
	{ USB_VID_CANON, USB_PID_CANON_CP720, P_CP_XXX, "Canon"},
	{ USB_VID_CANON, USB_PID_CANON_CP730, P_CP_XXX, "Canon"},
	{ USB_VID_CANON, USB_PID_CANON_CP740, P_CP_XXX, "Canon"},
	{ USB_VID_CANON, USB_PID_CANON_CP750, P_CP_XXX, "Canon"},
	{ USB_VID_CANON, USB_PID_CANON_CP760, P_CP_XXX, "Canon"},
	{ USB_VID_CANON, USB_PID_CANON_CP770, P_CP_XXX, "Canon"},
	{ USB_VID_CANON, USB_PID_CANON_CP780, P_CP_XXX, "Canon"},
	{ USB_VID_CANON, USB_PID_CANON_CP790, P_ES40_CP790, "Canon"},
	{ USB_VID_CANON, USB_PID_CANON_CP800, P_CP_XXX, "Canon"},
	{ USB_VID_CANON, USB_PID_CANON_CP810, P_CP_XXX, "Canon"},
	{ USB_VID_CANON, USB_PID_CANON_CP900, P_CP_XXX, "Canon"},
	{ USB_VID_CANON, USB_PID_CANON_ES1, P_ES1, "Canon"},
	{ USB_VID_CANON, USB_PID_CANON_ES2, P_ES2_20, "Canon"},
	{ USB_VID_CANON, USB_PID_CANON_ES20, P_ES2_20, "Canon"},
	{ USB_VID_CANON, USB_PID_CANON_ES3, P_ES3_30, "Canon"},
	{ USB_VID_CANON, USB_PID_CANON_ES30, P_ES3_30, "Canon"},
	{ USB_VID_CANON, USB_PID_CANON_ES40, P_ES40_CP790, "Canon"},

	{ USB_VID_KODAK, USB_PID_KODAK_6800, P_KODAK_6800, "Kodak"},
	{ USB_VID_KODAK, USB_PID_KODAK_1400, P_KODAK_1400_805, "Kodak"},
	{ USB_VID_KODAK, USB_PID_KODAK_805, P_KODAK_1400_805, "Kodak"},

	{ USB_VID_SHINKO, USB_PID_SHINKO_S2145, P_SHINKO_S2145, ""},
	{ USB_VID_SONY, USB_PID_SONY_UPDR150, P_SONY_UPDR150, ""},
};

/* Support Functions */

#define ID_BUF_SIZE 2048
static char *get_device_id(struct libusb_device_handle *dev)
{
	int length;
	int claimed = 0;
	int iface = 0;
	char *buf = malloc(ID_BUF_SIZE + 1);

	claimed = libusb_kernel_driver_active(dev, iface);
	if (claimed)
		libusb_detach_kernel_driver(dev, iface);

	libusb_claim_interface(dev, iface);

	if (libusb_control_transfer(dev,
				    LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_ENDPOINT_IN |
				    LIBUSB_RECIPIENT_INTERFACE,
				    0, 0,
				    (iface << 8),
				    (unsigned char *)buf, ID_BUF_SIZE, 5000) < 0)
	{
		*buf = '\0';
		goto done;
	}

	/* length is the first two bytes, MSB first */
	length = (((unsigned)buf[0] & 255) << 8) |
		((unsigned)buf[1] & 255);

	/* Sanity checks */
	if (length > ID_BUF_SIZE || length < 14)
		length = (((unsigned)buf[1] & 255) << 8) |
			((unsigned)buf[0] & 255);
	
	if (length > ID_BUF_SIZE)
		length = ID_BUF_SIZE;
	
	if (length < 14) {
		*buf = '\0';
		goto done;
	}

	/* Move, and terminate */
	memmove(buf, buf + 2, length);
	buf[length] = '\0';

done:
	libusb_release_interface(dev, iface);
#if 0
	if (claimed)
		libusb_attach_kernel_driver(dev, iface);
#endif
	return buf;
}


int send_data(struct libusb_device_handle *dev, uint8_t endp, 
	      uint8_t *buf, int len)
{
	int num;

	while (len) {
		int ret = libusb_bulk_transfer(dev, endp,
					       buf, (len > 65536) ? 65536: len,
					       &num, 5000);
		if (ret < 0) {
			ERROR("Failure to send data to printer (libusb error %d: (%d/%d to 0x%02x))\n", ret, num, len, endp);
			return ret;
		}
		len -= num;
		buf += num;
//		DEBUG("Sent %d (%d remaining) to 0x%x\n", num, len, endp);
	}

	return 0;
}

int terminate = 0;

static void sigterm_handler(int signum) {
	terminate = 1;
	INFO("Job Cancelled");
}

static char *sanitize_string(char *str) {
	int len = strlen(str);

	while(len && (str[len-1] <= 0x20)) {
		str[len-1] = 0;
		len--;
	}
	return str;
}

static int print_scan_output(struct libusb_device *device,
			     struct libusb_device_descriptor *desc,
			     char *prefix, char *manuf2,
			     int found, int match,
			     int scan_only, char *match_serno)
{
	struct libusb_device_handle *dev;

	unsigned char product[STR_LEN_MAX] = "";
	unsigned char serial[STR_LEN_MAX] = "";
	unsigned char manuf[STR_LEN_MAX] = "";
	
	if (libusb_open(device, &dev)) {
		ERROR("Could not open device %04x:%04x\n", desc->idVendor, desc->idProduct);
		found = -1;
		goto abort;
	}
	
	/* Query detailed info */
	if (desc->iManufacturer) {
		libusb_get_string_descriptor_ascii(dev, desc->iManufacturer, manuf, STR_LEN_MAX);
		sanitize_string((char*)manuf);
	}
	if (desc->iProduct) {
		libusb_get_string_descriptor_ascii(dev, desc->iProduct, product, STR_LEN_MAX);
		sanitize_string((char*)product);
	}
	if (desc->iSerialNumber) {
		libusb_get_string_descriptor_ascii(dev, desc->iSerialNumber, serial, STR_LEN_MAX);
		sanitize_string((char*)serial);
	}
	
	if (!strlen((char*)serial)) {
		uint8_t bus_num;
		uint8_t port_num;

		bus_num = libusb_get_bus_number(device);
		port_num = libusb_get_port_number(device);
		sprintf((char*)serial, "NONE_B%03d_D%03d", bus_num, port_num);
	}
	
	DEBUG("%sPID: %04X Manuf: '%s' Product: '%s' Serial: '%s'\n",
	      match ? "MATCH: " : "",
	      desc->idProduct, manuf, product, serial);
	
	if (scan_only) {
		/* URL-ify model. */
		char buf[128]; // XXX ugly..
		int j = 0, k = 0;
		char *ieee_id = get_device_id(dev);
		while (*(product + j + strlen(manuf2))) {
			buf[k] = *(product + j + (strlen(manuf2) ? (strlen(manuf2) + 1) : 0));
			if(buf[k] == ' ') {
				buf[k++] = '%';
				buf[k++] = '2';
				buf[k] = '0';
			}
			k++;
			j++;
		}
		buf[k] = 0;
		
		fprintf(stdout, "direct %s%s/%s?serial=%s \"%s\" \"%s\" \"%s\" \"\"\n",
			prefix, strlen(manuf2) ? manuf2 : (char*)manuf,
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
abort:
	return found;
}

static int find_and_enumerate(struct libusb_context *ctx,
			      struct libusb_device ***list,
			      char *match_serno,
			      int printer_type,
			      int scan_only)
{
	int num;
	int i, j;
	int found = -1;

	/* Enumerate and find suitable device */
	num = libusb_get_device_list(ctx, list);

	for (i = 0 ; i < num ; i++) {
		struct libusb_device_descriptor desc;
		int match = 0;
		libusb_get_device_descriptor((*list)[i], &desc);

		for (j = 0 ; j < sizeof(devices)/sizeof(struct device_id) ; j++) {
			if (desc.idVendor == devices[j].vid &&
			    desc.idProduct == devices[j].pid) {
				match = 1;
				if (printer_type && printer_type == devices[j].type)
					found = i;
				break;
			}
		}

		if (!match) {
			if (getenv("EXTRA_PID") && getenv("EXTRA_TYPE") && getenv("EXTRA_VID")) {
				int pid = strtol(getenv("EXTRA_PID"), NULL, 16);
				int vid = strtol(getenv("EXTRA_VID"), NULL, 16);
				int type = atoi(getenv("EXTRA_TYPE"));
				if (vid == desc.idVendor &&
				    pid == desc.idProduct) {
					match = 1;
					if (printer_type && printer_type == type)
						found = i;
				}
			}
		}

		if (!match)
			continue;

		found = print_scan_output((*list)[i], &desc,
					  URI_PREFIX, devices[j].manuf_str,
					  found, (found == i),
					  scan_only, match_serno);
	}

	return found;
}

static struct dyesub_backend *backends[] = {
	&updr150_backend,
	NULL,
};

/* MAIN */

int main (int argc, char **argv) 
{
	struct libusb_context *ctx;
	struct libusb_device **list;
	struct libusb_device_handle *dev;
	struct libusb_config_descriptor *config;

	struct dyesub_backend *backend;
	void * backend_ctx = NULL;

	uint8_t endp_up = 0;
	uint8_t endp_down = 0;

	int data_fd = fileno(stdin);

	int i;
	int claimed;

	int ret = 0;
	int iface = 0;
	int found = -1;
	int copies = 1;
	char *uri = getenv("DEVICE_URI");
	char *use_serno = NULL;

	DEBUG("Gutenprint DyeSub CUPS Backend version %s\n",
	      BACKEND_VERSION);

	/* Cmdline help */
	if (argc < 2) {
		DEBUG("Global Usage:\n\t%s [ infile | - ]\n\t%s job user title num-copies options [ filename ]\n\n",
		      argv[0], argv[0]);
		for (i = 0; ; i++) {
			backend = backends[i];
			if (!backend)
				break;
			DEBUG("%s CUPS backend version %s\n",
			      backend->name, backend->version);
			if (backend->cmdline_usage) {
				DEBUG(" Usage:\n");
				backend->cmdline_usage(backend->uri_prefix);
			} else {
				DEBUG(" (Global Usage Only)\n");
			}
		}
		libusb_init(&ctx);
		find_and_enumerate(ctx, &list, NULL, P_SONY_UPDR150, 1);
		libusb_free_device_list(list, 1);
		libusb_exit(ctx);
		exit(1);
	}

	// XXX detect from getenv("BACKEND");
	// XXX otherwise...
	backend = &updr150_backend;  // XXX detect.

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
		/* Start parsing URI 'prefix://PID/SERIAL' */
		if (strncmp(backend->uri_prefix, uri, strlen(backend->uri_prefix))) {
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
		use_serno = getenv("DEVICE");

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

	/* Libusb setup */
	libusb_init(&ctx);
	found = find_and_enumerate(ctx, &list, use_serno, P_SONY_UPDR150, 0);

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

	/* Initialize backend */
	backend_ctx = backend->init(dev, endp_up, endp_down);

	/* Read in data */
	if (backend->read_parse(backend_ctx, data_fd))
		exit(1);

	close(data_fd);

	/* Time for the main processing loop */

	INFO("Printing started (%d copies)\n", copies);

	ret = backend->main_loop(backend_ctx, copies);
	if (ret)
		goto done_claimed;

	/* Done printing */
	INFO("All printing done\n");
	ret = 0;

done_claimed:
	libusb_release_interface(dev, iface);

done_close:
#if 0
	if (claimed)
		libusb_attach_kernel_driver(dev, iface);
#endif
	libusb_close(dev);
done:

	if (backend && backend_ctx)
		backend->teardown(backend_ctx);

	libusb_free_device_list(list, 1);
	libusb_exit(ctx);

	return ret;
}

