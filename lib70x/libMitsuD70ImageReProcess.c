/* LibMitsuD70ImageReProcess -- Re-implemented image processing library for
                                the Mitsubishi CP-D70 family of printers

   Copyright (c) 2016-2017 Solomon Peachy <pizza@shaftnet.org>

   ** ** ** ** Do NOT contact Mitsubishi about this library! ** ** ** **

   This library is a platform-independent reimplementation of the image
   processing algorithms that are necessary to utilize the Mitsubishi
   CP-D70 family of printers.

   Mitsubishi was *NOT* involved in the creation of this library, and is
   not responsible in any way for the library or any deficiencies in its
   output.  They will provide no support if it is used.

   However, without this library, it is nearly impossible to utilize the
   D70 family of printers under Linux and similar operating systems.

   The following printers are known (or expected) to function with this
   library:

     * Mitsubishi CP-D70DW
     * Mitsubishi CP-D707DW
     * Mitsubishi CP-K60DW-S
     * Mitsubishi CP-D80DW
     * Kodak 305
     * Fuji ASK-300

   ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** ** **

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the Free
   Software Foundation; either version 3 of the License, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
   for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

          [http://www.gnu.org/licenses/gpl-3.0.html]

*/

#define LIB_VERSION "0.5"
#define LIB_APIVERSION 2

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

//#include "Mitsu_D70.h"

//-------------------------------------------------------------------------
// Endian Manipulation macros

#if (__BYTE_ORDER == __LITTLE_ENDIAN)
#define le32_to_cpu(__x) __x
#define le16_to_cpu(__x) __x
#define be16_to_cpu(__x) ntohs(__x)
#define be32_to_cpu(__x) ntohl(__x)
#else
#define le32_to_cpu(x)                                                  \
        ({                                                              \
                uint32_t __x = (x);                                     \
                ((uint32_t)(                                            \
                        (((uint32_t)(__x) & (uint32_t)0x000000ffUL) << 24) | \
                        (((uint32_t)(__x) & (uint32_t)0x0000ff00UL) <<  8) | \
                        (((uint32_t)(__x) & (uint32_t)0x00ff0000UL) >>  8) | \
                        (((uint32_t)(__x) & (uint32_t)0xff000000UL) >> 24) )); \
        })
#define le16_to_cpu(x)                                                  \
        ({                                                              \
                uint16_t __x = (x);                                     \
                ((uint16_t)(                                            \
                        (((uint16_t)(__x) & (uint16_t)0x00ff) <<  8) | \
                        (((uint16_t)(__x) & (uint16_t)0xff00) >>  8))); \
        })
#define be32_to_cpu(__x) __x
#define be16_to_cpu(__x) __x
#endif

#define cpu_to_le16 le16_to_cpu
#define cpu_to_le32 le32_to_cpu
#define cpu_to_be16 be16_to_cpu
#define cpu_to_be32 be32_to_cpu


//-------------------------------------------------------------------------
// Data declarations

#define LUT_LEN 14739
#define CPC_DATA_ROWS 2730

#define CHUNK_LEN (256*1024)

struct CColorConv3D {
	uint8_t lut[17][17][17][3];
};

/* Defines an image.  Note that origin_cols/origin_rows should always = 0 */
struct BandImage {
	   void  *imgbuf;      // @ 0
	 int32_t bytes_per_row;// @ 4  bytes per row (respect 8bpp and 16bpp!)
	uint16_t origin_cols;  // @ 8  origin_cols
	uint16_t origin_rows;  // @12  origin_rows
	uint16_t cols;         // @16  cols
	uint16_t rows;         // @20  rows
	                       // @24
};

/* State for image processing algorithm */
struct CImageEffect70 {
	uint32_t pad;            // @0
	  double *unk_0001;      // @4/1         // array [(cols+6) * 3],
	  double *unk_0002;      // @8/2         // pointer into _0001, used in HTD/TTD
	  double *unk_0003;      // @12/3        // pointer into _0002, used in HTD/TTD
	  double *unk_0004;      // @16/4        // array [band_pixels], HTD generates, TTD consumes for the next line.
	  double unk_0005[3];    // @20/5        // FCC generates, YMC consumes. final scaling factor for thermal compensation?
	uint32_t unk_0011[3][128];  // @44/11    // HTD generates, FCC consumes.
	  double unk_0395[3][128];  // @1580/395 // FCC generates, used by YMC
	  double *unk_1163;      // @4652/1163   // array of [3 * row_count], used by FCC.  Per-row correction factor.
	uint16_t *unk_1164;      // @4656/1164   // array of [22 * _1202], sharpening buffer
	uint16_t *unk_1165[11];  // @4660/1165   // sharpening state?
	uint16_t *unk_1176[11];  // @4704/1176   // sharpening state?
	uint16_t *unk_1187[8];   // @4748/1187   // sharpening state?
	struct CPCData *cpc;     // @4780/1195
	 int32_t sharpen;        // @4784/1196   // -1 off, max 8.
	uint32_t columns;        // @4788/1197
	uint32_t rows;           // @4792/1198
	uint32_t pixel_count;    // @4796/1199
	uint32_t cur_row;        // @4800/1200
	uint32_t band_pixels;    // @4804/1201
	uint32_t unk_1202;       // @4808/1202   // band_pixels + 6 (sharpening state?)
	double   unk_1203;       // @4812/1203   // FH[0]
	double   unk_1205;       // @4820/1205   // FH[1]
	double   unk_1207;       // @4828/1207   // FH[2]
	double   unk_1209;       // @4836/1209   // FH[3] - FH[2]
	double   unk_1211;       // @4844/1211   // FH[4] - FH[3]
	double   unk_1213;       // @4852/1213   // FH[4]
	                         // @4860/1215
};

/* The parsed data out of the CPC files */
struct CPCData {
	/* One per output row, Used for HTD. */
	uint32_t LINEy[2730];    // @0      // can be uint16?
	uint32_t LINEm[2730];    // @10920  // can be uint16?
	uint32_t LINEc[2730];    // @21840  // can be uint16?
	/* Maps input color to gamma-corrected 16bpp inverse */
	uint16_t GNMby[256];     // @32760
	uint16_t GNMgm[256];     // @33272
	uint16_t GNMrc[256];     // @33784
	/* Used for FCC */
	double   FM[256];        // @34296
	/* Used for TTD */
	double   KSP[128];       // @36344
	double   KSM[128];       // @37368
	double   OSP[128];       // @38392
	double   OSM[128];       // @39416
	double   KP[11];         // @40440
	double   KM[11];         // @40528
	/* Used for HTD */
	double   HK[4];          // @40616
	
	uint32_t Speed[3];       // @40648 -- Unused!
	double   FH[5];          // @40660
	/* Used for sharpening */
	double   SHK[72];        // @40700
	/* Used for YMC6 */
	double   UH[101];        // @41276
	
	/* Used by roller mark correction (K60/D80/EK305) -- Unused! */
	uint32_t ROLK[13];       // @42084
	/* Used by reverse/skip logic (K60/D80/EK305) */
	 int32_t REV[76];        // @42136
	                         // @42440
};

/*** Version ***/
int lib70x_getapiversion(void)
{
	return LIB_APIVERSION;
}

/*** 3D color Lookup table ****/

/* Load the Lookup table off of disk into *PRE-ALLOCATED* buffer */
int CColorConv3D_Get3DColorTable(uint8_t *buf, const char *filename)
{
	FILE *stream;

	if (!filename)
		return 1;
	if (!*filename)
		return 2;
	if (!buf)
		return 3;

	stream = fopen(filename, "rb");
	if (!stream)
		return 4;

	fseek(stream, 0, SEEK_END);
	if (ftell(stream) < LUT_LEN) {
		fclose(stream);
		return 5;
	}
	fseek(stream, 0, SEEK_SET);
	fread(buf, 1, LUT_LEN, stream);
	fclose(stream);

	return 0;
}

/* Parse the on-disk LUT data into the structure.... */
struct CColorConv3D *CColorConv3D_Load3DColorTable(const uint8_t *ptr)
{
	struct CColorConv3D *this;
	this = malloc(sizeof(*this));
	if (!this)
		return NULL;

	int i, j, k;

	for (i = 0 ; i <= 16 ; i++) {
		for (j = 0 ; j <= 16 ; j++) {
			for (k = 0; k <= 16; k++) {
				this->lut[k][j][i][2] = *ptr++;
				this->lut[k][j][i][1] = *ptr++;
				this->lut[k][j][i][0] = *ptr++;
			}
		}
	}
	return this;
}
void CColorConv3D_Destroy3DColorTable(struct CColorConv3D *this)
{
	free(this);
}

/* Transform a single pixel. */
static void CColorConv3D_DoColorConvPixel(struct CColorConv3D *this, uint8_t *redp, uint8_t *grnp, uint8_t *blup)
{
	int red_h;
	int grn_h;
	int blu_h;
	int grn_li;
	int red_li;
	int blu_li;
	int red_l;
	int grn_l;
	int blu_l;

	uint8_t *tab0;       // @ 14743
	uint8_t *tab1;       // @ 14746
	uint8_t *tab2;       // @ 14749
	uint8_t *tab3;       // @ 14752
	uint8_t *tab4;       // @ 14755
	uint8_t *tab5;       // @ 14758
	uint8_t *tab6;       // @ 14761
	uint8_t *tab7;       // @ 14764	
  
	red_h = *redp >> 4;
	red_l = *redp & 0xF;
	red_li = 16 - red_l;
  
	grn_h = *grnp >> 4;
	grn_l = *grnp & 0xF;
	grn_li = 16 - grn_l;

	blu_h = *blup >> 4;
	blu_l = *blup & 0xF;
	blu_li = 16 - blu_l;

//	printf("%d %d %d =>", *redp, *grnp, *blup);

	tab0 = this->lut[red_h+0][grn_h+0][blu_h+0];
	tab1 = this->lut[red_h+1][grn_h+0][blu_h+0];
	tab2 = this->lut[red_h+0][grn_h+1][blu_h+0];
	tab3 = this->lut[red_h+1][grn_h+1][blu_h+0];
	tab4 = this->lut[red_h+0][grn_h+0][blu_h+1];
	tab5 = this->lut[red_h+1][grn_h+0][blu_h+1];
	tab6 = this->lut[red_h+0][grn_h+1][blu_h+1];
	tab7 = this->lut[red_h+1][grn_h+1][blu_h+1];

#if 0
	printf(" %d %d %d ", tab0[0], tab0[1], tab0[2]);
	printf(" %d %d %d ", tab1[0], tab1[1], tab1[2]);
	printf(" %d %d %d ", tab2[0], tab2[1], tab2[2]);
	printf(" %d %d %d ", tab3[0], tab3[1], tab3[2]);
	printf(" %d %d %d ", tab4[0], tab4[1], tab4[2]);
	printf(" %d %d %d ", tab5[0], tab5[1], tab5[2]);
	printf(" %d %d %d ", tab6[0], tab6[1], tab6[2]);
	printf(" %d %d %d ", tab7[0], tab7[1], tab7[2]);
#endif
	*redp = (blu_li
		 * (grn_li * (red_li * tab0[0] + red_l * tab1[0])
		    + grn_l * (red_li * tab2[0] + red_l * tab3[0]))
		 + blu_l
		 * (grn_li * (red_li * tab4[0] + red_l * tab5[0])
		    + grn_l * (red_li * tab6[0] + red_l * tab7[0]))
		 + 2048) >> 12;
	*grnp = (blu_li
		 * (grn_li * (red_li * tab0[1] + red_l * tab1[1])
		    + grn_l * (red_li * tab2[1] + red_l * tab3[1]))
		 + blu_l
		 * (grn_li * (red_li * tab4[1] + red_l * tab5[1])
		    + grn_l * (red_li * tab6[1] + red_l * tab7[1]))
		 + 2048) >> 12;
	*blup = (blu_li
		 * (grn_li * (red_li * tab0[2] + red_l * tab1[2])
		    + grn_l * (red_li * tab2[2] + red_l * tab3[2]))
		 + blu_l
		 * (grn_li * (red_li * tab4[2] + red_l * tab5[2])
		    + grn_l * (red_li * tab6[2] + red_l * tab7[2]))
		 + 2048) >> 12;
  
//	printf("=> %d %d %d\n", *redp, *grnp, *blup);
}

#define COLORCONV_RGB 0
#define COLORCONV_BGR 1

/* Perform a total conversion on an entire image */
void CColorConv3D_DoColorConv(struct CColorConv3D *this, uint8_t *data, uint16_t cols, uint16_t rows, uint32_t stride, int rgb_bgr)
{
	uint16_t i, j;

	uint8_t *ptr;

	for ( i = 0; i < rows ; i++ )
	{
		ptr = data;
		for ( j = 0; cols > j; j++ )
		{
			if (rgb_bgr) {
				CColorConv3D_DoColorConvPixel(this, ptr + 2, ptr + 1, ptr);
			} else {
				CColorConv3D_DoColorConvPixel(this, ptr, ptr + 1, ptr + 2);
			}
			ptr += 3;
		}
		data += stride;
	}
}

/*** CPC Data ***/

/* Load and parse the CPC data */
struct CPCData *get_CPCData(const char *filename)
{
	struct CPCData *data;
	FILE *f;
	char buf[4096];
	int line;
	char *ptr;

	const char *delim = " ,\t\n";	

	if (!filename)
		return NULL;
	data = malloc(sizeof(struct CPCData));
	if (!data)
		return NULL;

	f = fopen(filename, "r");
	if (!f)
		goto done_free;

	/* Skip the first two rows */
	for (line = 0 ; line < 2 ; line++) {
		if (fgets(buf, sizeof(buf), f) == NULL)
			goto abort;
	}

	/* Init the REV and ROLK first rows */
	data->REV[0] = 0;
	data->ROLK[0] = 0;

	/* Start reading in data */
	for (line = 0 ; line < CPC_DATA_ROWS ; line++) {
		if (fgets(buf, sizeof(buf), f) == NULL)
			goto abort;
		ptr = strtok(buf, delim);  // Always skip first column
		if (!ptr)
			goto abort;
		if (line < CPC_DATA_ROWS) {
			ptr = strtok(NULL, delim);
			if (!ptr)
				goto abort;
			data->LINEy[line] = strtol(ptr, NULL, 10);
			ptr = strtok(NULL, delim);
			if (!ptr)
				goto abort;
			data->LINEm[line] = strtol(ptr, NULL, 10);
			ptr = strtok(NULL, delim);
			if (!ptr)
				goto abort;
			data->LINEc[line] = strtol(ptr, NULL, 10);
		}
		if (line < 256) {
			ptr = strtok(NULL, delim);
			if (!ptr)
				goto abort;
			data->GNMby[line] = strtol(ptr, NULL, 10);
			ptr = strtok(NULL, delim);
			if (!ptr)
				goto abort;
			data->GNMgm[line] = strtol(ptr, NULL, 10);
			ptr = strtok(NULL, delim);
			if (!ptr)
				goto abort;
			data->GNMrc[line] = strtol(ptr, NULL, 10);
			ptr = strtok(NULL, delim);
			if (!ptr)
				goto abort;
			data->FM[line] = strtod(ptr, NULL);
		}
		if (line < 128) {
			ptr = strtok(NULL, delim);
			if (!ptr)
				goto abort;
			data->KSP[line] = strtod(ptr, NULL);
			ptr = strtok(NULL, delim);
			if (!ptr)
				goto abort;
			data->KSM[line] = strtod(ptr, NULL);
			ptr = strtok(NULL, delim);
			if (!ptr)
				goto abort;
			data->OSP[line] = strtod(ptr, NULL);
			ptr = strtok(NULL, delim);
			if (!ptr)
				goto abort;
			data->OSM[line] = strtod(ptr, NULL);
		}
		if (line < 11) {
			ptr = strtok(NULL, delim);
			if (!ptr)
				goto abort;
			data->KP[line] = strtod(ptr, NULL);
			ptr = strtok(NULL, delim);
			if (!ptr)
				goto abort;
			data->KM[line] = strtod(ptr, NULL);
		}
		if (line < 4) {
			ptr = strtok(NULL, delim);
			if (!ptr)
				goto abort;
			data->HK[line] = strtod(ptr, NULL);
		}
		if (line < 3) {
			ptr = strtok(NULL, delim);
			if (!ptr)
				goto abort;
			data->Speed[line] = strtol(ptr, NULL, 10);
		}
		if (line < 5) {
			ptr = strtok(NULL, delim);
			if (!ptr)
				goto abort;
			data->FH[line] = strtod(ptr, NULL);
		}
		if (line < 72) {
			ptr = strtok(NULL, delim);
			if (!ptr)
				goto abort;
			data->SHK[line] = strtod(ptr, NULL);
		}
		if (line < 101) {
			ptr = strtok(NULL, delim);
			if (!ptr)
				goto abort;
			data->UH[line] = strtod(ptr, NULL);
		}
		if (line < 13) { // ROLK, not present on D70
			ptr = strtok(NULL, delim);
			if (!ptr)
				continue;
			data->ROLK[line] = strtol(ptr, NULL, 10);

		}
		if (line < 76) { // REV, not present on D70
			ptr = strtok(NULL, delim);
			if (!ptr)
				continue;
			data->REV[line] = strtol(ptr, NULL, 10);
		}
	}

	fclose(f);
	return data;
		
abort:
	fclose(f);
done_free:
	free(data);
	return NULL;
}

void destroy_CPCData(struct CPCData *data) {
	free(data);
}

/*** Image Processing ***/
static struct CImageEffect70 *CImageEffect70_Create(struct CPCData *cpc)
{
	struct CImageEffect70 *data = malloc(sizeof (struct CImageEffect70));
	if (!data)
		return NULL;
	
	memset(data, 0, sizeof(*data));
	data->sharpen = -1;
	data->unk_1203 = 1.0;
	data->unk_1205 = 1.0;
	data->cpc = cpc;
	return data;
}

static void CImageEffect70_Destroy(struct CImageEffect70 *data)
{
	free(data);
}

static void CImageEffect70_InitMidData(struct CImageEffect70 *data)
{
	data->unk_0002 = NULL;
	data->unk_0001 = NULL;
	data->unk_0004 = NULL;
	data->unk_1163 = NULL;
	data->unk_1164 = NULL;

	data->unk_0005[0] = 1.0;
	data->unk_0005[1] = 1.0;
	data->unk_0005[2] = 1.0;
	
	memset(data->unk_1165, 0, sizeof(data->unk_1165));
	memset(data->unk_1176, 0, sizeof(data->unk_1176));
	memset(data->unk_0011, 0, sizeof(data->unk_0011));
	memset(data->unk_0395, 0, sizeof(data->unk_0395));
}

static void CImageEffect70_CreateMidData(struct CImageEffect70 *data)
{
	int i;
	
	data->unk_0001 = malloc(sizeof(double) * 3 * (data->columns + 6));
	memset(data->unk_0001, 0, (sizeof(double) * 3 * (data->columns + 6)));
	data->unk_0002 = data->unk_0001 + 9;
	data->unk_0003 = data->unk_0002 + 3 * (data->columns - 1);
	data->unk_0004 = malloc(sizeof(double) * data->band_pixels);
	memset(data->unk_0004, 0, (sizeof(double) * data->band_pixels));
	data->unk_1163 = malloc(3 * sizeof(double) * data->rows);
	memset(data->unk_1163, 0, (3 * sizeof(double) * data->rows));
	data->unk_1202 = data->band_pixels + 6;
	data->unk_1164 = malloc(11 * sizeof(uint16_t) * data->unk_1202);
	memset(data->unk_1164, 0, (11 * sizeof(uint16_t) * data->unk_1202));
	data->unk_1176[0] = data->unk_1164;
	data->unk_1165[0] = data->unk_1176[0] + 3; // ie 6 bytes.

	for (i = 1 ; i < 11 ; i++ ) {
		data->unk_1176[i] = data->unk_1176[i-1] + /* 2* */ data->unk_1202;
		data->unk_1165[i] = data->unk_1176[i] + 3; // ie 6 bytes
	}
	memset(data->unk_0011, 0, sizeof(data->unk_0011));
	memset(data->unk_0395, 0, sizeof(data->unk_0395));	
}

static void CImageEffect70_DeleteMidData(struct CImageEffect70 *data)
{
	int i;

	if (data->unk_0001) {
		free(data->unk_0001);
		data->unk_0001 = NULL;
		data->unk_0002 = NULL;		
	}
	if (data->unk_0004) {
		free(data->unk_0004);
		data->unk_0004 = NULL;
	}
	if (data->unk_1163) {
		free(data->unk_1163);
		data->unk_1163 = NULL;
	}
	if (data->unk_1164) {
		free(data->unk_1164);
		data->unk_1164 = NULL;
	}

	for (i = 0 ; i < 3 ; i++) {
		data->unk_0005[i] = 0.0;
	}
	memset(data->unk_1165, 0, sizeof(data->unk_1165));
	memset(data->unk_1176, 0, sizeof(data->unk_1176));
	memset(data->unk_0011, 0, sizeof(data->unk_0011));
	memset(data->unk_0395, 0, sizeof(data->unk_0395));	
}

static void CImageEffect70_Sharp_CopyLine(struct CImageEffect70 *data,
					  int a2, const uint16_t *row, int a4)
{
	uint16_t *src, *v5;

	src = data->unk_1165[a2 + 5];
	v5 = src + 3 * (data->columns - 1);

	memcpy(src, row -(a4 * data->pixel_count), 2 * data->band_pixels);
	
	memcpy(src - 3, src, 6);
	memcpy(v5 + 3, v5, 6);
}

static void CImageEffect70_Sharp_PrepareLine(struct CImageEffect70 *data,
					     const uint16_t *row)
{
	uint32_t i;
	
	CImageEffect70_Sharp_CopyLine(data, 0, row, 0);
	for (i = 0 ; i < 5 ; i++) {
		memcpy(data->unk_1176[i], data->unk_1176[5], 2 * data->unk_1202);
	}
	for (i = 1 ; i <= 5 ; i++) {
		if (data->rows -1 >= i)
			CImageEffect70_Sharp_CopyLine(data, i, row, i);
		else
			CImageEffect70_Sharp_CopyLine(data, i, row, data->rows - 1);
	}
}

static void CImageEffect70_Sharp_ShiftLine(struct CImageEffect70 *data)
{
	memmove(data->unk_1176[0], data->unk_1176[1], 20 * data->unk_1202);
	// XXX was memcpy..
}

static void CImageEffect70_Sharp_SetRefPtr(struct CImageEffect70 *data)
{
	data->unk_1187[0] = data->unk_1165[4] - 3; // 6 bytes
	data->unk_1187[1] = data->unk_1165[4];
	data->unk_1187[2] = data->unk_1165[4] + 3; // 6 bytes
	data->unk_1187[3] = data->unk_1165[5] - 3; // 6 bytes
	data->unk_1187[4] = data->unk_1165[5] + 3; // 6 bytes
	data->unk_1187[5] = data->unk_1165[6] - 3; // 6 bytes
	data->unk_1187[6] = data->unk_1165[6];
	data->unk_1187[7] = data->unk_1165[6] + 3; // 6 bytes
}

/* Applies the final correction factor to a row. */
static void CImageEffect70_CalcYMC6(struct CImageEffect70 *data,
				    const double *a2, uint16_t *imgdata)
{
	uint16_t i, j;
	uint32_t offset;	
	double uh_val;

	offset = data->rows - 1 - data->cur_row;
	if ( offset > 100 )
		offset = 100;
	uh_val = data->cpc->UH[offset];

	offset = 0;
	for ( i = 0; i < data->columns; i++ ) {
		for ( j = 0; j < 3; j++ ) {
			double v4 = data->unk_0005[j] * data->unk_0395[j][((int)a2[offset] >> 9)] * a2[offset] * uh_val;
			if ( v4 > 65535.0)
				imgdata[offset] = 65535;
			else if ( v4 < 0.0)
				imgdata[offset] = 0;
			else
				imgdata[offset] = (int)v4;
			++offset;
		}
	}
}

static void CImageEffect70_CalcFCC(struct CImageEffect70 *data)
{
	double s[3];
	double *cur_ptr;
	int i, j;
	double *v12, *v11, *v10;
	
	cur_ptr = &data->unk_1163[3*data->cur_row];

	for (j = 0 ; j < 3 ; j++) {
		cur_ptr[j] = 127 * data->unk_0011[j][127];
	}
	for (i = 126 ; i >= 0 ; i--) {
		for (j = 0 ; j < 3 ; j++) {
			cur_ptr[j] += i * data->unk_0011[j][i];
			data->unk_0011[j][i] += data->unk_0011[j][i+1];
		}
	}
	if (data->cur_row > 2) {
		v12 = cur_ptr - 3;
		v11 = cur_ptr - 6;
		v10 = cur_ptr - 9;
	} else if (data->cur_row == 2) {
		v12 = cur_ptr - 3;
		v11 = cur_ptr - 6;
		v10 = cur_ptr - 6;
	} else if (data->cur_row == 1) {
		v12 = cur_ptr - 3;
		v11 = cur_ptr - 3;
		v10 = cur_ptr - 3;
	} else {
		v12 = cur_ptr;
		v11 = cur_ptr;
		v10 = cur_ptr;
	}

	for (i = 0 ; i < 3 ; i++) {
		double v5;
		cur_ptr[i] /= data->columns;

		v5 = data->unk_1207 * cur_ptr[i]
			+ data->unk_1209 * v12[i]
			+ data->unk_1211 * v11[i]
			- data->unk_1213 * v10[i];
		if (v5 > 0.0) {
			data->unk_0005[i] = v5 / data->unk_1203 + 1.0;
		} else {
			data->unk_0005[i] = v5 / data->unk_1205 + 1.0;
		}
	}

	memset(s, 0, sizeof(s));
	
	for (i = 0 ; i < 128 ; i++) {
		for (j = 0 ; j < 3 ; j++) {
			int v3 = 255 * data->unk_0011[j][i] / 1864;
			if (v3 > 255)
				v3 = 255;
			s[j] += data->cpc->FM[v3];
			data->unk_0395[j][i] = s[j] / (i + 1);
		}
	}
}

/* Heat Transfer compensation (I think)

   Take the raw data
   add in the scaling factor from the adoining rows, using HK[]
   add in the fixed overhead from LINEy/m/c[]
   cap at 0-65535.

   Also populates unk_0004, which informs the NEXT CalcTTD run what to do.
*/
   
static void CImageEffect70_CalcHTD(struct CImageEffect70 *data, const double *a2, double *a3)
{
	int v16;
	double *v9;
	double *v5;
	double *src;
	int offset;
	unsigned int i, k;
	int v4[3];
	int v11;

	v9 = data->cpc->HK;
	src = data->unk_0002;

	memset(data->unk_0011, 0, sizeof(data->unk_0011));
	v16 = data->cur_row;
	if (v16 > 2729)
		v16 = 2729;

	v4[0] = data->cpc->LINEy[v16];
	v4[1] = data->cpc->LINEm[v16];
	v4[2] = data->cpc->LINEc[v16];

	v5 = data->unk_0003;
	memcpy(src - 9, src, 0x18);
	memcpy(src - 6, src, 0x18);
	memcpy(src - 3, src, 0x18);
	memcpy(v5 + 3, v5, 0x18);
	memcpy(v5 + 6, v5, 0x18);
	memcpy(v5 + 9, v5, 0x18);
	offset = 0;
	for (i = 0; i < data->columns; i++) {
		for (k = 0; k < 3 ; k++) {
			data->unk_0004[offset] = v9[0] * (src[offset] + src[offset]) +
				v9[1] * (src[offset - 3] + src[offset + 3]) +
				v9[2] * (src[offset - 6] + src[offset + 6]) +
				v9[3] * (src[offset - 9] + src[offset + 9]);

			a3[offset] = a2[offset] + v4[k];
			v11 = a3[offset];
			if ( a3[offset] < 65535.0 ) {
				if (a3[offset] >= 0.0) {
					v11 >>= 9;
				} else {
					a3[offset] = 0.0;
					v11 = 0;
				}
			} else {
				a3[offset] = 65535.0;
				v11 = 127;
			}

			data->unk_0011[k][v11]++;
			offset++;
		}
	}
}

static void CImageEffect70_CalcTTD(struct CImageEffect70 *data,
				   const uint16_t *a2, double *a3)
{
	double *km, *kp, *osm, *osp, *ksm, *ksp;
	double *sharp = NULL;
	uint32_t i;
	ksp = data->cpc->KSP;  // KS Plus
	ksm = data->cpc->KSM;  // KS Minus
	osp = data->cpc->OSP;  // OS Plus
	osm = data->cpc->OSM;  // OS Minus
	kp = data->cpc->KP;    // K  Plus
	km = data->cpc->KM;    // K  Minus

	if (data->sharpen >= 0)
		sharp = &data->cpc->SHK[8 * data->sharpen];

	for (i = 0 ; i < data->band_pixels ; i++) {
		int v5;
		double v4, v6, v7, v8;
		int v29;
		int v25;
		int v17;
		double v20, k_comp, ks_comp, os_comp, sharp_comp;
		int j, k;

		v8 = a2[i];
		v7 = data->unk_0004[i] - v8;
		v29 = v7;
		if (v29 >= 0) {
			int v31 = 127;
			if (v29 <= 65535)
				v31 = v29 >> 9;
			ks_comp = ksp[v31];
		} else {
			int v30 = 127;
			if (-v29 <= 65535)
				v30 = -v29 >> 9;
			ks_comp = ksm[v30];
		}
		v6 = v7 * ks_comp + v8 - v8;  // XXX WTF?
		v25 = v6;
		if (v25 >= 0) {
			int v27 = 127;
			if (v25 <= 65535)
				v27 = v25 >> 9;
			os_comp = osp[v27];
		} else {
			int v26 = 127;
			if (-v25 <= 65535)
				v26 = -v25 >> 9;
			os_comp = osm[v26];
		}
		k_comp = 0.0;
		for ( j = 0 ; j < 11 ; j++) {
			if (j == 5)
				continue;

			v5 = a2[i] - data->unk_1165[j][i];
			if (v5 >= 0)
				k_comp += kp[j] * v5;
			else
				k_comp += km[j] * v5;
		}
		sharp_comp = 0.0;
		if (sharp) {
			for (k = 0 ; k < 8 ; k++) {
				sharp_comp += sharp[k] * (a2[i] - data->unk_1187[k][i]);
			}
		}
		a3[i] = v8 - v6 * os_comp + k_comp + sharp_comp;
		v4 = data->unk_0004[i] - a3[i];
		v17 = v4;

		if ( v17 >= 0 )
		{
			int v19 = 127;
			if ( v17 <= 65535 )
				v19 = v17 >> 9;
			v20 = ksp[v19];
		} else {
			int v18 = 127;
			if ( -v17 <= 65535 )
				v18 = -v17 >> 9;
			v20 = ksm[v18];
		}
		data->unk_0002[i] = a3[i] + v4 * v20;
	}
}

static void CImageEffect70_CalcSA(struct BandImage *img,
				  int always_1, int32_t *ptr1,
				  int32_t revX, int32_t *ptr2)
{
  int cols; // edi@1
  int rows; // ebx@1
  unsigned int v9; // ecx@2
  unsigned int v10; // ecx@3
  unsigned int v11; // ecx@6
  int v12; // edx@9
  int v13; // eax@9
  int v14; // edi@17
  int v15; // esi@17
  int v16; // ebx@17
  int v17; // edx@18
  int16_t *v18; // ecx@18
  int v19; // al@19
  int16_t *v21; // [sp+0h] [bp-2Ch]@6
  int v22; // [sp+4h] [bp-28h]@17
  int v24; // [sp+Ch] [bp-20h]@13
  int v25; // [sp+10h] [bp-1Ch]@15
  int v26; // [sp+14h] [bp-18h]@17
  int v27; // [sp+18h] [bp-14h]@15
  int16_t *v28; // [sp+1Ch] [bp-10h]@17
  
  cols = img->cols - img->origin_cols;
  rows = img->rows - img->origin_rows;

  if ( img->bytes_per_row >= 0 )
  {
    if ( always_1 )
    {
      v10 = img->bytes_per_row;
      goto LABEL_6;
    }
    v9 = -img->bytes_per_row;
  }
  else
  {
    v9 = img->bytes_per_row;
    if ( !always_1 )
    {
      v10 = -img->bytes_per_row;
LABEL_6:
      v11 = v10 >> 1;
      v21 = (int16_t*)img->imgbuf + v11 * (rows - 1);
      goto LABEL_9;
    }
  }
  v11 = v9 >> 1;
  v21 = img->imgbuf;

LABEL_9:
  v12 = ptr1[1];
  v13 = 0;
  if ( v12 < 0 )
    v12 = 0;
  if ( rows >= ptr1[3] )
    rows = ptr1[3];
  v24 = rows;
  if ( ptr1[0] >= 0 )
    v13 = ptr1[0];
  v25 = v13;
  v27 = v12;
  if ( cols > ptr1[2] )
    cols = ptr1[2];
  v26 = cols;
  v14 = 0;
  v15 = 0;
  v28 = v21 - v12 * v11;
  v16 = 0;
  v22 = 3 * v13;
  while ( v24 > v27 ) {
    v17 = v25;
    v18 = v22 + v28;
    while ( v26 > v17 ) {
      v16 += revX <= v18[0];
      v15 += revX <= v18[1];
      v19 = revX <= v18[2];
      v18 += 3;
      ++v17;
      v14 += v19;
    }
    v28 -= v11;
    ++v27;
  }
  ptr2[0] = v16;
  ptr2[1] = v15;
  ptr2[2] = v14;
}

static int CImageEffect70_JudgeReverseSkipRibbon_int(struct BandImage *img,
						     int32_t *REV,
						     int always_1)
{
  int32_t rows, cols;

  int32_t v15; // [sp+4Ch] [bp-8Ch]@1  

  rows = img->rows - img->origin_rows;
  cols = img->cols - img->origin_cols;

  int32_t v16[4] = { REV[0], REV[2], REV[1], rows };
  int32_t v20[4] = { REV[1], 0, cols, rows };
  int32_t v24[4] = { 0, 0, REV[0], rows };
  int32_t v28[4] = { REV[0], 0, REV[1], REV[2] };

  int32_t v32[3] = { 0, 0, 0 };
  int32_t v35[3] = { 0, 0, 0 };
  int32_t v38[3] = { 0, 0, 0 };
  int32_t v41[3] = { 0, 0, 0 };

  CImageEffect70_CalcSA(img, always_1, v24, REV[3], v32);
  CImageEffect70_CalcSA(img, always_1, v20, REV[7], v41);
  CImageEffect70_CalcSA(img, always_1, v16, REV[11], v38);
  CImageEffect70_CalcSA(img, always_1, v28, REV[15], v35);

  for (v15 = 0 ; v15 < 3 ; v15++) {
    int32_t v10 = v32[v15];
    int32_t v11 = v41[v15];
    int32_t v12 = v38[v15];
    int32_t v13 = v35[v15];

    if ( v10 >= REV[4]
	 && (v10 >= REV[5]
	     || v38[v15] >= REV[14]
	     || v35[v15] >= REV[18]) )
    {
	    return 0;
    }

    if ( v11 >= REV[8]
	 && (v11 >= REV[9]
	     || v38[v15] >= REV[14]
	     || v35[v15] >= REV[18]) )
    {
	    return 0;
    }

    if ( v12 >= REV[12]
	 && (v12 >= REV[13]
	     || v10 >= REV[6]
	     || v11 >= REV[10]
	     || v35[v15] >= REV[18]) )
    {
	    return 0;
    }

    if ( v13 >= REV[16]
	 && (v13 >= REV[17]
	     || v10 >= REV[6]
	     || v11 >= REV[10]
	     || v12 >= REV[14]) )
    {
	    return 0;
    }
  }
  return 1;
}

// called twice, once with param1 == 1, once with param1 == 2.
static int CImageEffect70_JudgeReverseSkipRibbon(struct CPCData *cpc,						 
						 struct BandImage *img,
						 int is_6inch,
						 int param1)
{
	int offset = -1;

	if (param1 == 1) {
		if (is_6inch) {
			offset = 0;
		} else {
			offset = 19;
		}
	} else if (param1 == 2) {
		if (is_6inch) {
			offset = 38;
		} else {
			offset = 57;
		}
	}
	if (offset != -1) {
		return CImageEffect70_JudgeReverseSkipRibbon_int(img, &cpc->REV[offset], 1);
	}
	
	return 0;
}

static void CImageEffect70_DoConv(struct CImageEffect70 *data,
				  struct CPCData *cpc,
				  struct BandImage *in,
				  struct BandImage *out,
				  int sharpen)
{
	double v8[3];
	double *v9 = NULL;
	double *v10 = NULL;

	uint32_t i, j;
	int v12;
	int outstride;
	uint16_t *outptr;
	uint16_t *inptr;
	
	CImageEffect70_InitMidData(data);

	if (sharpen > 8)
		sharpen = 8;
	data->sharpen = sharpen;
	
	data->unk_1203 = cpc->FH[0];
	data->unk_1205 = cpc->FH[1];
	data->unk_1207 = cpc->FH[2];
	data->unk_1209 = cpc->FH[3] - cpc->FH[2];
	data->unk_1211 = cpc->FH[4] - cpc->FH[3];
	data->unk_1213 = cpc->FH[4];

	data->columns = in->cols - in->origin_cols;
	data->rows = in->rows - in->origin_rows;
	data->band_pixels = data->columns * 3;

	if (data->columns <= 0 || data->rows <= 0 ||
	    cpc->FH[0] < 1.0 || cpc->FH[1] < 1.0)
		return;

	if (in->bytes_per_row >= 0) {
		data->pixel_count = in->bytes_per_row / 2; // numbers of pixels per input band

		outstride = out->bytes_per_row / 2; // pixels per dest band
		inptr = (uint16_t*) in->imgbuf + data->pixel_count * (data->rows - 1); // ie last row of input buffer
		outptr = (uint16_t*) out->imgbuf + outstride * (data->rows - 1); // last row of output buffer
	} else {
		data->pixel_count = in->bytes_per_row / 2;
		outstride = out->bytes_per_row / 2;
		inptr = in->imgbuf;
		outptr = out->imgbuf;
	}
	
	CImageEffect70_CreateMidData(data);

	v10 = malloc(data->band_pixels * sizeof(double));
	memset(v10, 0, (data->band_pixels * sizeof(double)));
	v9 = malloc(data->band_pixels * sizeof(double));
	memset(v9, 0, (data->band_pixels * sizeof(double)));	
	v8[0] = cpc->GNMby[255];
	v8[1] = cpc->GNMgm[255];
	v8[2] = cpc->GNMrc[255];

	v12 = 0;
	for(j = 0; j < data->columns ; j++) {
		for (i = 0 ; i < 3 ; i++) {
			data->unk_0001[v12++] = v8[i];
		}
	}
	
	CImageEffect70_Sharp_PrepareLine(data, inptr);

	if (data->sharpen >= 0)
		CImageEffect70_Sharp_SetRefPtr(data);

	for (data->cur_row = 0 ; data->cur_row < data->rows ; data->cur_row++) {
		if (data->cur_row + 5 < data->rows)
			CImageEffect70_Sharp_CopyLine(data, 5, inptr, 5);
		CImageEffect70_CalcTTD(data, inptr, v10);
		CImageEffect70_CalcHTD(data, v10, v9);
		CImageEffect70_CalcFCC(data);
		CImageEffect70_CalcYMC6(data, v9, outptr);
		inptr -= data->pixel_count; // work backwards one input row
		outptr -= outstride;        // work backwards one output row
		CImageEffect70_Sharp_ShiftLine(data);
	}
	CImageEffect70_DeleteMidData(data);

	if (v10)
		free(v10);
	if (v9)
		free(v9);
}

static void CImageEffect70_DoGamma(struct CImageEffect70 *data, struct BandImage *input, struct BandImage *out)
{
	int cols, rows;
	int i, j;

	uint8_t *outptr, *inptr;
	uint32_t in_stride, out_stride;

	struct CPCData *cpc = data->cpc;
	
	cols = input->cols - input->origin_cols;
	rows = input->rows - input->origin_rows;

	in_stride = abs(input->bytes_per_row);
	out_stride = abs(out->bytes_per_row);

	if (cols <= 0 || rows <= 0)
	    return;

	inptr = (uint8_t*) input->imgbuf;
	outptr = out->imgbuf;

	for (i = 0; i < rows; i++) {
		uint8_t *v10 = inptr;
		uint16_t *v9 = (uint16_t*)outptr;
		for (j = 0 ; j < cols ; j++) {
			v9[0] = cpc->GNMby[v10[0]];
			v9[1] = cpc->GNMgm[v10[1]];
			v9[2] = cpc->GNMrc[v10[2]];
			v10 += 3;
			v9 += 3;
		}
		inptr += in_stride;
		outptr += out_stride;
	}
}

int do_image_effect(struct CPCData *cpc, struct BandImage *input, struct BandImage *output, int sharpen, uint8_t rew[2])
{
	struct CImageEffect70 *data;

	fprintf(stderr, "INFO: libMitsuD70ImageReProcess version '%s' API %d\n", LIB_VERSION, LIB_APIVERSION);
	fprintf(stderr, "INFO: Copyright (c) 2016-2017 Solomon Peachy\n");
	fprintf(stderr, "INFO: This free software comes with ABSOLUTELY NO WARRANTY!\n");
	fprintf(stderr, "INFO: Licensed under the GNU GPL.\n");
	fprintf(stderr, "INFO: *** This code is NOT supported or endorsed by Mitsubishi! ***\n");
	
	data = CImageEffect70_Create(cpc);
	if (!data)
		return -1;
	
	CImageEffect70_DoGamma(data, input, output);
	CImageEffect70_DoConv(data, cpc, output, output, sharpen);

	/* Figure out if we can get away with rewinding, or not... */
	rew[0] = 1;
	rew[1] = 1;	
	if (cpc->REV[0]) {
		int is_6 = -1;

		/* Only allow rewinds for 4x6 and 5x3.5" prints */
		if (input->cols == 0x0620 && input->rows == 0x0434)
			is_6 = 0;
		else if (input->cols == 0x0748 && input->rows == 0x04c2)
			is_6 = 1;
		
		if (is_6 != -1) {
			rew[0] = CImageEffect70_JudgeReverseSkipRibbon(cpc, output, is_6, 1);
			rew[1] = CImageEffect70_JudgeReverseSkipRibbon(cpc, output, is_6, 2);
		}
	}
	CImageEffect70_Destroy(data);
	
	return 0;
}

int send_image_data(struct BandImage *out, void *context,
		    int (*callback_fn)(void *context, void *buffer, uint32_t len))
{
	uint32_t rows, cols;
	uint16_t *buf;
	uint32_t i, j, k;
	int ret = 1;
	uint16_t *v15;
	size_t count;
	
	cols = out->cols - out->origin_cols;
	rows = out->rows - out->origin_rows;
	buf = malloc(CHUNK_LEN);
	if (!buf)
		goto done;
	if (!callback_fn)
		goto done;

	// XXX hacky workaround.  Doing this right results in the image
	// being incorrectly mirrored. There's a missing step somewhere
	// that flips each row. Until then, this stays in.
	out->bytes_per_row = -out->bytes_per_row;

	if (out->bytes_per_row > 0) {
		v15 = out->imgbuf + ((rows - 1) * out->bytes_per_row);
	} else {
		v15 = out->imgbuf;
	}

	for ( i = 0 ; i < 3 ; i++) {
		uint16_t *v13 = &v15[i];
		uint16_t *outptr = buf;

		count = 0;
		memset(buf, 0, CHUNK_LEN);
		for (j = 0 ; j < rows ; j++) {
			uint16_t *v9 = v13;
			for (k = 0 ; k < cols ; k++) {
				*outptr++ = cpu_to_be16(*v9);
				v9 += 3;
				count += 2;
				if ( count == CHUNK_LEN )
				{
					if (callback_fn(context, buf, count))
						goto done;
					count = 0;
					outptr = buf;
					memset(buf, 0, CHUNK_LEN);
				}
			}
			v13 -= out->bytes_per_row / 2;
		}
		if (count) {
			if (callback_fn(context, buf, (count + 511) / 512 * 512))
				goto done;
		}
	}

	ret = 0;
done:
	if (buf)
		free(buf);
	return ret;
}

#if 0

struct PrintSetting {
	uint32_t  pad;   // @0
	uint32_t  cols;  // @4
	uint32_t  rows;  // @8
	uint32_t  deck;  // @12 // Deck selection, 0, 1, 2.
	uint32_t  sharp; // @16 // sharpness, -1 is none, 0->8, 4 is normal
	uint32_t  matte; // @20 // 1 or 0.
	uint32_t  mode;  // @24 // 0/1/2 Fine/SuperFine/Ultrafine
	uint32_t  cconv; // @28 // -1/0 none/Table1
	uint32_t  unk6;  // @32 // multicut (0, 4x6*2 = 1, 2x6*2 = 5)
	uint32_t  unk7;  // @36 // image doubled up, (0 or 1)
	uint16_t  unk8;  // @40 // actual print cols in image doubled mode 
	uint16_t  unk9;  // @42 // actual print rows in image doubled mode
	uint32_t  unk10; // @44 // rows to cut in doubled mode?  (38 for 4x6T2)
	uint16_t  unk11; // @48 // 4x6 doubled mode, type 3. (0 or 1) ??
	uint16_t  gammaR; // @50
	uint16_t  gammaG; // @52
	uint16_t  gammaB; // @54
	uint16_t  brighr; // @56
	uint16_t  brighg; // @58
	uint16_t  brighb; // @60
	uint16_t  contrr; // @62
	uint16_t  cpntrg; // @64		
	uint16_t  contrb; // @66	
};

// XXX replace 'args'. buf is >= 512 bytes.
void create_header(uint8_t *buf, uint8_t *args, uint16_t *dim, uint16_t *dim_lam)
{
	int laminate;
	int deck;

	memset((buf + 4), 0, 0x1FCu);
	*(buf + 4) = 0;
	*(buf + 5) = 0;
	*(buf + 16) = dim[0] >> 8;
	*(buf + 17) = dim[0];
	*(buf + 18) = dim[1] >> 8;
	*(buf + 19) = dim[1];
	if ( *(uint32_t *)(args + 12) ) { // laminate dimensions
		*(buf + 20) = dim_lam[0] >> 8;
		*(buf + 21) = dim_lam[0];
		*(buf + 22) = dim_lam[1] >> 8;
		*(buf + 23) = dim_lam[1];
	}
	laminate = *(uint32_t *)(args + 4); // speed/mode
	if ( laminate == 1 ) {
		*(buf + 24) = 3;
	} else if ( laminate == 2 ) {
		*(buf + 24) = 4;
	} else {
		*(buf + 24) = 0;
	}
	deck = *(uint32_t *)(args + 8); // deck
	if ( deck == 1 ) 
		*(buf + 32) = 2;
	else
		*(buf + 32) = deck == 2;
	*(buf + 40) = 0;
	if ( *(uint32_t *)(args + 12) ) // laminate type
		*(buf + 41) = 2;  
	else
		*(buf + 41) = 0;

	*(buf + 48) = *(uint32_t *)(args + 16); // multicut mode
}

void CImageUtility_CreateBandImage16(struct BandImage *img, uint32_t *a2, int32_t a3)
{
	img->bytes_per_row = 3 * sizeof(uint16_t) * (a2[2] - a2[0]);
	if (a3 < 0)
		img->bytes_per_row = -img->bytes_per_row;
	img->origin_cols = a2[0];  // origin_cols
	img->origin_rows = a2[1];  // origin_rows
	img->cols = a2[2];  // cols
	img->rows = a2[3];  // rows

	img->imgbuf = malloc(3 * sizeof(uint16_t) * (a2[3] - a2[1]) * (a2[2] - a2[0]));
}

#endif
