/*
 *   Mitsubishi Photo Printer Comon Code
 *
 *   (c) 2013-2020 Solomon Peachy <pizza@shaftnet.org>
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
 *   along with this program; if not, see <https://www.gnu.org/licenses/>.
 *
 *   SPDX-License-Identifier: GPL-3.0+
 *
 */

#include "backend_common.h"
#include "backend_mitsu.h"

int mitsu_loadlib(struct mitsu_lib *lib, int type)
{
	DL_INIT();

	memset(lib, 0, sizeof(*lib));

#if defined(WITH_DYNAMIC)
	DEBUG("Attempting to load image processing library\n");
	lib->dl_handle = DL_OPEN(LIB_NAME_RE);
	if (!lib->dl_handle)
		WARNING("Image processing library not found, using internal fallback code\n");
	if (lib->dl_handle) {
		lib->GetAPIVersion = DL_SYM(lib->dl_handle, "lib70x_getapiversion");
		if (!lib->GetAPIVersion) {
			ERROR("Problem resolving API Version symbol in imaging processing library, too old or not installed?\n");
			DL_CLOSE(lib->dl_handle);
			lib->dl_handle = NULL;
			return CUPS_BACKEND_FAILED;
		}
		if (lib->GetAPIVersion() != REQUIRED_LIB_APIVERSION) {
			ERROR("Image processing library API version mismatch!\n");
			DL_CLOSE(lib->dl_handle);
			lib->dl_handle = NULL;
			return CUPS_BACKEND_FAILED;
		}

		lib->Load3DColorTable = DL_SYM(lib->dl_handle, "CColorConv3D_Load3DColorTable");
		lib->Destroy3DColorTable = DL_SYM(lib->dl_handle, "CColorConv3D_Destroy3DColorTable");
		lib->DoColorConv = DL_SYM(lib->dl_handle, "CColorConv3D_DoColorConv");
		lib->GetCPCData = DL_SYM(lib->dl_handle, "get_CPCData");
		lib->DestroyCPCData = DL_SYM(lib->dl_handle, "destroy_CPCData");
		lib->DoImageEffect60 = DL_SYM(lib->dl_handle, "do_image_effect60");
		lib->DoImageEffect70 = DL_SYM(lib->dl_handle, "do_image_effect70");
		lib->DoImageEffect80 = DL_SYM(lib->dl_handle, "do_image_effect80");
		lib->SendImageData = DL_SYM(lib->dl_handle, "send_image_data");
		if (!lib->Load3DColorTable ||
		    !lib->Destroy3DColorTable || !lib->DoColorConv ||
		    !lib->GetCPCData || !lib->DestroyCPCData ||
		    !lib->DoImageEffect60 || !lib->DoImageEffect70 ||
		    !lib->DoImageEffect80 || !lib->SendImageData) {
			ERROR("Problem resolving symbols in imaging processing library\n");
			DL_CLOSE(lib->dl_handle);
			lib->dl_handle = NULL;
			return CUPS_BACKEND_FAILED;
		} else {
			DEBUG("Image processing library successfully loaded\n");
		}
	}

	switch (type) {
	case P_MITSU_D80:
		lib->DoImageEffect = lib->DoImageEffect80;
		break;
	case P_MITSU_K60:
	case P_KODAK_305:
		lib->DoImageEffect = lib->DoImageEffect60;
		break;
	case P_MITSU_D70X:
	case P_FUJI_ASK300:
		lib->DoImageEffect = lib->DoImageEffect70;
		break;
	default:
		lib->DoImageEffect = NULL;
	}

	return CUPS_BACKEND_OK;
#else
	return CUPS_BACKEND_FAILED;
#endif
}

int mitsu_destroylib(struct mitsu_lib *lib)
{
#if defined(WITH_DYNAMIC)
	if (lib->dl_handle) {
		if (lib->cpcdata)
			lib->DestroyCPCData(lib->cpcdata);
		if (lib->ecpcdata)
			lib->DestroyCPCData(lib->ecpcdata);
		if (lib->lut)
			lib->Destroy3DColorTable(lib->lut);
		DL_CLOSE(lib->dl_handle);
	}

	memset(lib, 0, sizeof(*lib));
	DL_EXIT();

#endif
	return CUPS_BACKEND_OK;
}

int mitsu_apply3dlut(struct mitsu_lib *lib, char *lutfname, uint8_t *databuf,
		     uint16_t cols, uint16_t rows, uint16_t stride,
		     int rgb_bgr)
{
#if defined(WITH_DYNAMIC)
	int i;

	if (!lutfname)
		return CUPS_BACKEND_OK;

	if (!lib->lut) {
		uint8_t *buf = malloc(LUT_LEN);
		if (!buf) {
			ERROR("Memory allocation failure!\n");
			return CUPS_BACKEND_RETRY_CURRENT;
		}
		if ((i = dyesub_read_file(lutfname, buf, LUT_LEN, NULL)))
			return i;
		lib->lut = lib->Load3DColorTable(buf);
		free(buf);
		if (!lib->lut) {
			ERROR("Unable to parse LUT file '%s'!\n", lutfname);
			return CUPS_BACKEND_CANCEL;
		}
	}

	if (lib->lut) {
		DEBUG("Running print data through 3D LUT\n");
		lib->DoColorConv(lib->lut, databuf, cols, rows, stride, rgb_bgr);
	}
#endif
	return CUPS_BACKEND_OK;
}

int mitsu_readlamdata(const char *fname, uint16_t lamstride,
		      uint8_t *databuf, uint32_t *datalen,
		      uint16_t rows, uint16_t cols, uint8_t bpp)
{
	int i, j, fd;
	int remain = cols * rows * bpp;

	DEBUG("Reading %d bytes of matte data from disk (%d/%d)\n", cols * rows * bpp, cols, lamstride);
	fd = open(fname, O_RDONLY);
	if (fd < 0) {
		ERROR("Unable to open matte lamination data file '%s'\n", fname);
		return CUPS_BACKEND_CANCEL;
	}

	/* Read in the matte data plane */
	for (j = 0 ; j < rows ; j++) {
		remain = lamstride * bpp;

		/* Read one row of lamination data at a time */
		while (remain) {
			i = read(fd, databuf + *datalen, remain);
			if (i < 0)
				return CUPS_BACKEND_CANCEL;
			if (i == 0) {
				/* We hit EOF, restart from beginning */
				lseek(fd, 0, SEEK_SET);
				continue;
			}
			*datalen += i;
			remain -= i;
		}
		/* Back off the buffer so we "wrap" on the print row. */
		*datalen -= ((lamstride - cols) * 2);
	}

	return CUPS_BACKEND_OK;
}
