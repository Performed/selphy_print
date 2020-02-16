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

#include "lib70x/libMitsuD70ImageReProcess.h"

/* If we don't have the libMitsu header */
#ifndef LUT_LEN
#define COLORCONV_RGB 0
#define COLORCONV_BGR 1

#define LUT_LEN 14739

struct BandImage {
	   void  *imgbuf;      //  @0
	 int32_t bytes_per_row;//  @4  bytes per row (respect 8bpp and 16bpp!)
	uint16_t origin_cols;  // @8  origin_cols
	uint16_t origin_rows;  // @12  origin_rows
	uint16_t cols;         // @16  cols
	uint16_t rows;         // @20  rows
	                       // @24
};
struct mitsu98xx_data;  /* Forward declaration */
#endif

typedef int (*lib70x_getapiversionFN)(void);
typedef int (*Get3DColorTableFN)(uint8_t *buf, const char *filename);
typedef struct CColorConv3D *(*Load3DColorTableFN)(const uint8_t *ptr);
typedef void (*Destroy3DColorTableFN)(struct CColorConv3D *this);
typedef void (*DoColorConvFN)(struct CColorConv3D *this, uint8_t *data, uint16_t cols, uint16_t rows, uint32_t bytes_per_row, int rgb_bgr);
typedef struct CPCData *(*get_CPCDataFN)(const char *filename);
typedef void (*destroy_CPCDataFN)(struct CPCData *data);
typedef int (*do_image_effectFN)(struct CPCData *cpc, struct CPCData *ecpc, struct BandImage *input, struct BandImage *output, int sharpen, int reverse, uint8_t rew[2]);
typedef int (*send_image_dataFN)(struct BandImage *out, void *context,
			       int (*callback_fn)(void *context, void *buffer, uint32_t len));

typedef int (*CP98xx_DoConvertFN)(const struct mitsu98xx_data *table,
				  const struct BandImage *input,
				  struct BandImage *output,
				  uint8_t type, int sharpness, int reversed);
typedef struct mitsu98xx_data *(*CP98xx_GetDataFN)(const char *filename);
typedef void (*CP98xx_DestroyDataFN)(const struct mitsu98xx_data *data);

#ifndef WITH_DYNAMIC
#warning "No dynamic loading support!"
#endif

#define REQUIRED_LIB_APIVERSION 5

#define LIBMITSU_VER "0.04"

/* Image processing library function prototypes */
#define LIB_NAME_RE "libMitsuD70ImageReProcess" DLL_SUFFIX

struct mitsu_lib {
	void *dl_handle;
	lib70x_getapiversionFN GetAPIVersion;
	Load3DColorTableFN Load3DColorTable;
	Destroy3DColorTableFN Destroy3DColorTable;
	DoColorConvFN DoColorConv;
	get_CPCDataFN GetCPCData;
	destroy_CPCDataFN DestroyCPCData;
	do_image_effectFN DoImageEffect60;
	do_image_effectFN DoImageEffect70;
	do_image_effectFN DoImageEffect80;
	do_image_effectFN DoImageEffect;
	send_image_dataFN SendImageData;
	CP98xx_DoConvertFN CP98xx_DoConvert;
	CP98xx_GetDataFN CP98xx_GetData;
	CP98xx_DestroyDataFN CP98xx_DestroyData;

	struct CColorConv3D *lut;
	struct CPCData *cpcdata;
	struct CPCData *ecpcdata;
};

int mitsu_loadlib(struct mitsu_lib *lib, int type);
int mitsu_destroylib(struct mitsu_lib *lib);
int mitsu_apply3dlut(struct mitsu_lib *lib, char *lutfname, uint8_t *databuf,
		     uint16_t cols, uint16_t rows, uint16_t stride,
		     int rgb_bgr);
int mitsu_readlamdata(const char *fname, uint16_t lamstride,
		      uint8_t *databuf, uint32_t *datalen,
		      uint16_t rows, uint16_t cols, uint8_t bpp);

#define TEMPERATURE_NORMAL  0x00
#define TEMPERATURE_PREHEAT 0x40
#define TEMPERATURE_COOLING 0x80

const char *mitsu_temperatures(uint8_t temp);
const char *mitsu_media_types(uint8_t brand, uint8_t type);
