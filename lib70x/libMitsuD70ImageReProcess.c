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

   SPDX-License-Identifier: GPL-3.0+

*/

#define LIB_VERSION "0.7"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#include "libMitsuD70ImageReProcess.h"

#define UNUSED(expr) do { (void)(expr); } while (0)

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

#define CPC_DATA_ROWS 2730

#define CHUNK_LEN (256*1024)

struct CColorConv3D {
	uint8_t lut[17][17][17][3];
};

/* State for image processing algorithm */
struct CImageEffect70 {
	uint32_t pad;            // @0
	  double *ttd_htd_scratch;  // @4/1         // array [(cols+6) * 3], single row, plus padding.  processing buffer from TTD->HTD
	  double *ttd_htd_first; // @8/2         // first pixel of ttd_htd_scratch
	  double *ttd_htd_last;  // @12/3        // last pixel of ttd_htd_scratch
	  double *htd_ttdnext;   // @16/4        // array [band_pixels], state from HTD->TTDnext.
	  double fcc_ymc_scale[3];  // @20/5   // FCC generates, YMC consumes. per-row scaling factor for thermal compensation.
	uint32_t htd_fcc_scratch[3][128];  // @44/11    // state from HTD->FCC
	  double fcc_ymc_scratch[3][128];  // @1580/395 // state from FCC->YMC6
	  double *fcc_rowcomps;  // @4652/1163   // array of [3 * row_count], Per-row/color correction factor?  Used internally by FCC code.
	uint16_t *linebuf;       // @4656/1164   // array of [11 * sizeof(uint16_t) * linebuf_stride], Historical line buffer
	uint16_t *linebuf_row[11];  // @4660/1165   // Pointers into rows in line buffer, minus padding!
	uint16_t *linebuf_line[11]; // @4704/1176   // Pointers to raw rows in line buffer (w/ padding on either side)
	uint16_t *linebuf_shrp[8];  // @4748/1187   // Pointers into line buffer, for pixels used for sharpening
	struct CPCData *cpc;     // @4780/1195   // Loaded from disk..
	 int32_t sharpen;        // @4784/1196   // -1 off, 0-8, "normal" is 4.
	uint32_t columns;        // @4788/1197   // columns in image
	uint32_t rows;           // @4792/1198   // rows in image
	uint32_t pixel_count;    // @4796/1199   // pixels per input band (ie input stride / 2)
	uint32_t cur_row;        // @4800/1200   // row index.
	uint32_t band_pixels;    // @4804/1201   // pixels per output band (always columns * 3)
	uint32_t linebuf_stride; // @4808/1202   // band_pixels + 6 -- line buffer row stride
	double   fhdiv_up;       // @4812/1203   // FH[0]  // division factor for positive comp
	double   fhdiv_dn;       // @4820/1205   // FH[1]  // divison factor for negative comp
	double   fh_cur;         // @4828/1207   // FH[2]
	double   fh_prev1;       // @4836/1209   // FH[3] - FH[2]
	double   fh_prev2;       // @4844/1211   // FH[4] - FH[3]
	double   fh_prev3;       // @4852/1213   // FH[4]
	                         // @4860/1215
};

/* The parsed data out of the CPC files */
struct CPCData {
	/* One per output row, Used for HTD. */
	uint32_t LINEy[2730];    // @0      // can be uint16?
	uint32_t LINEm[2730];    // @10920  // can be uint16?
	uint32_t LINEc[2730];    // @21840  // can be uint16?
	/* Maps input color to gamma-corrected 16bpp inverse */
	uint16_t GNMby[256];     // @32760  // Gamma map Blue->Yellow
	uint16_t GNMgm[256];     // @33272  // Gamma map Green->Magenta
	uint16_t GNMrc[256];     // @33784  // Gamma map Red->Cyan
	/* Used for FCC */
	double   FM[256];        // @34296
	/* Used for TTD */
	double   KSP[128];       // @36344
	double   KSM[128];       // @37368
	double   OSP[128];       // @38392
	double   OSM[128];       // @39416
	double   KP[11];         // @40440 // weights for line buffer!
	double   KM[11];         // @40528 // weights for line buffer!
	/* Used for HTD */
	double   HK[4];          // @40616
	
	uint32_t Speed[3];       // @40648 -- Unused!
	double   FH[5];          // @40660
	/* Used for sharpening */
	double   SHK[72];        // @40700 // sharpening coefficients, actually double[9][8]
	/* Used for YMC6 */
	double   UH[101];        // @41276
	
	/* Used by roller mark correction (K60/D80/EK305) -- Unused! */
	uint32_t ROLK[13];       // @42084
	/* Used by reverse/skip logic (K60/D80/EK305) */
	 int32_t REV[76];        // @42136 // ACtually int32_t[4][19]
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
	data->fhdiv_up = 1.0;
	data->fhdiv_dn = 1.0;
	data->cpc = cpc;
	return data;
}

static void CImageEffect70_Destroy(struct CImageEffect70 *data)
{
	free(data);
}

static void CImageEffect70_InitMidData(struct CImageEffect70 *data)
{
	data->ttd_htd_first = NULL;
	data->ttd_htd_scratch = NULL;
	data->htd_ttdnext = NULL;
	data->fcc_rowcomps = NULL;
	data->linebuf = NULL;

	data->fcc_ymc_scale[0] = 1.0;
	data->fcc_ymc_scale[1] = 1.0;
	data->fcc_ymc_scale[2] = 1.0;

	memset(data->linebuf_row, 0, sizeof(data->linebuf_row));
	memset(data->linebuf_line, 0, sizeof(data->linebuf_line));
//	memset(data->htd_fcc_scratch, 0, sizeof(data->htd_fcc_scratch));  // redundant
//	memset(data->fcc_ymc_scratch, 0, sizeof(data->fcc_ymc_scratch)); // redundant
}

static void CImageEffect70_CreateMidData(struct CImageEffect70 *data)
{
	int i;
	
	data->ttd_htd_scratch = malloc(sizeof(double) * 3 * (data->columns + 6));
	memset(data->ttd_htd_scratch, 0, (sizeof(double) * 3 * (data->columns + 6)));
	data->ttd_htd_first = data->ttd_htd_scratch + 9;
	data->ttd_htd_last = data->ttd_htd_first + 3 * (data->columns - 1);
	data->htd_ttdnext = malloc(sizeof(double) * data->band_pixels);
	memset(data->htd_ttdnext, 0, (sizeof(double) * data->band_pixels));
	data->fcc_rowcomps = malloc(3 * sizeof(double) * data->rows);
	memset(data->fcc_rowcomps, 0, (3 * sizeof(double) * data->rows));
	data->linebuf_stride = data->band_pixels + 6;
	data->linebuf = malloc(11 * sizeof(uint16_t) * data->linebuf_stride);
	memset(data->linebuf, 0, (11 * sizeof(uint16_t) * data->linebuf_stride));
	data->linebuf_line[0] = data->linebuf;
	data->linebuf_row[0] = data->linebuf_line[0] + 3; // ie 6 bytes.

	for (i = 1 ; i < 11 ; i++ ) {
		data->linebuf_line[i] = data->linebuf_line[i-1] + /* 2* */ data->linebuf_stride;
		data->linebuf_row[i] = data->linebuf_line[i] + 3; // ie 6 bytes
	}
	memset(data->htd_fcc_scratch, 0, sizeof(data->htd_fcc_scratch));
	memset(data->fcc_ymc_scratch, 0, sizeof(data->fcc_ymc_scratch));
}

static void CImageEffect70_DeleteMidData(struct CImageEffect70 *data)
{
	int i;

	if (data->ttd_htd_scratch) {
		free(data->ttd_htd_scratch);
		data->ttd_htd_scratch = NULL;
		data->ttd_htd_first = NULL;		
	}
	if (data->htd_ttdnext) {
		free(data->htd_ttdnext);
		data->htd_ttdnext = NULL;
	}
	if (data->fcc_rowcomps) {
		free(data->fcc_rowcomps);
		data->fcc_rowcomps = NULL;
	}
	if (data->linebuf) {
		free(data->linebuf);
		data->linebuf = NULL;
	}

	for (i = 0 ; i < 3 ; i++) {
		data->fcc_ymc_scale[i] = 0.0;
	}
	memset(data->linebuf_row, 0, sizeof(data->linebuf_row));
	memset(data->linebuf_line, 0, sizeof(data->linebuf_line));
	memset(data->htd_fcc_scratch, 0, sizeof(data->htd_fcc_scratch));
	memset(data->fcc_ymc_scratch, 0, sizeof(data->fcc_ymc_scratch));	
}

static void CImageEffect70_Sharp_CopyLine(struct CImageEffect70 *data,
					  int offset, const uint16_t *row, int a4)
{
	uint16_t *src, *v5;

	src = data->linebuf_row[offset + 5];
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
		memcpy(data->linebuf_line[i], data->linebuf_line[5], 2 * data->linebuf_stride);
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
	// XXX was memcpy, but src and dest definitely overlap!
	memmove(data->linebuf_line[0], data->linebuf_line[1], 10 * sizeof(uint16_t) * data->linebuf_stride);
}

/* Sets up reference pointers for the sharpening algorithm */
static void CImageEffect70_Sharp_SetRefPtr(struct CImageEffect70 *data)
{
	data->linebuf_shrp[0] = data->linebuf_row[4] - 3; // 6 bytes
	data->linebuf_shrp[1] = data->linebuf_row[4];
	data->linebuf_shrp[2] = data->linebuf_row[4] + 3; // 6 bytes
	data->linebuf_shrp[3] = data->linebuf_row[5] - 3; // 6 bytes
	data->linebuf_shrp[4] = data->linebuf_row[5] + 3; // 6 bytes
	data->linebuf_shrp[5] = data->linebuf_row[6] - 3; // 6 bytes
	data->linebuf_shrp[6] = data->linebuf_row[6];
	data->linebuf_shrp[7] = data->linebuf_row[6] + 3; // 6 bytes
}

/* Applies the final correction factor to a row. */
static void CImageEffect70_CalcYMC6(struct CImageEffect70 *data,
				    const double *in, uint16_t *imgdata)
{
	uint16_t i, j;
	uint32_t offset;	
	double uh_val;

	/* Work out the UH value we need based on our row count */
	offset = data->rows - 1 - data->cur_row;
	if ( offset > 100 )
		offset = 100;
	uh_val = data->cpc->UH[offset];

	/* Now apply the final correction to each pixel in the row */
	offset = 0;
	for ( i = 0; i < data->columns; i++ ) {
		for ( j = 0; j < 3; j++ ) {
			/* Per-row scaling * per-bucket scaling * input pixel * uh_factor */
			double pixel = data->fcc_ymc_scale[j] * data->fcc_ymc_scratch[j][((int)in[offset] >> 9)] * in[offset] * uh_val;
			if ( pixel > 65535.0)
				imgdata[offset] = 65535;
			else if ( pixel < 0.0)
				imgdata[offset] = 0;
			else
				imgdata[offset] = (int)pixel;
			++offset;
		}
	}
}

static void CImageEffect70_CalcFCC(struct CImageEffect70 *data)
{
	double s[3];
	double *row_comp;
	int i, j;
	double *prev1, *prev2, *prev3;

	/* Figure out where we need to be */
	row_comp = &data->fcc_rowcomps[3*data->cur_row];

	/* Initialize correction factor for this row based on the
	   buckets that CalcHTD handed us */
	for (j = 0 ; j < 3 ; j++) {
		row_comp[j] = 127 * data->htd_fcc_scratch[j][127];
	}
	for (i = 126 ; i >= 0 ; i--) {
		for (j = 0 ; j < 3 ; j++) {
			row_comp[j] += i * data->htd_fcc_scratch[j][i];
			data->htd_fcc_scratch[j][i] += data->htd_fcc_scratch[j][i+1];
		}
	}

	/* Set up pointers to previous rows. Or if we're on the first
	   three rows, take special action. */
	if (data->cur_row > 2) {
		prev1 = row_comp - 3;
		prev2 = row_comp - 6;
		prev3 = row_comp - 9;
	} else if (data->cur_row == 2) {
		prev1 = row_comp - 3;
		prev2 = row_comp - 6;
		prev3 = row_comp - 6;
	} else if (data->cur_row == 1) {
		prev1 = row_comp - 3;
		prev2 = row_comp - 3;
		prev3 = row_comp - 3;
	} else {
		prev1 = row_comp;
		prev2 = row_comp;
		prev3 = row_comp;
	}

	/* For each plane in the row, work out the global scaling factor */
	for (i = 0 ; i < 3 ; i++) {
		double v5;
		/* Average it out over the number of columns */
		row_comp[i] /= data->columns;

		v5 = data->fh_cur * row_comp[i]
			+ data->fh_prev1 * prev1[i]
			+ data->fh_prev2 * prev2[i]
			- data->fh_prev3 * prev3[i];
		/* Different factors for scaling up vs down */
		if (v5 > 0.0) {
			data->fcc_ymc_scale[i] = v5 / data->fhdiv_up + 1.0;
		} else {
			data->fcc_ymc_scale[i] = v5 / data->fhdiv_dn + 1.0;
		}
	}

	/* Update the output buckets based on the input buckets plus
	   the FM correction factor */
	memset(s, 0, sizeof(s));
	for (i = 0 ; i < 128 ; i++) {
		for (j = 0 ; j < 3 ; j++) {
			int val = 255 * data->htd_fcc_scratch[j][i] / 1864;
			if (val > 255)
				val = 255;
			s[j] += data->cpc->FM[val];
			data->fcc_ymc_scratch[j][i] = s[j] / (i + 1);
		}
	}
}

/* Heat Transfer compensation (I think)

   Take the raw data
   add in the scaling factor from the adoining rows, using HK[]
   add in the fixed overhead from LINEy/m/c[]
   cap at 0-65535.

   Also populates htd_ttdnext, which informs the NEXT CalcTTD run what to do.
*/
   
static void CImageEffect70_CalcHTD(struct CImageEffect70 *data, const double *in, double *out)
{
	int cur_row, offset;
	double *hk;
	double *last, *first;
	unsigned int i, k;
	uint32_t line_comp[3];

	hk = data->cpc->HK;
	first = data->ttd_htd_first;

	/* Clean out correction buckets */
	memset(data->htd_fcc_scratch, 0, sizeof(data->htd_fcc_scratch));

	cur_row = data->cur_row;
	if (cur_row > 2729)
		cur_row = 2729;

	/* Fixed compensation per-line */
	line_comp[0] = data->cpc->LINEy[cur_row];
	line_comp[1] = data->cpc->LINEm[cur_row];
	line_comp[2] = data->cpc->LINEc[cur_row];

	/* Fill in shoulders of the row */
	last = data->ttd_htd_last;
	memcpy(first - 9, first, 0x18);   // Copy first pixel to pre-buffer
	memcpy(first - 6, first, 0x18);
	memcpy(first - 3, first, 0x18);
	memcpy(last + 3, last, 0x18);     // Copy last pixel to post-buffer
	memcpy(last + 6, last, 0x18);
	memcpy(last + 9, last, 0x18);

	/* Work out the compensation factors for each pixel in the row */
	offset = 0;
	for (i = 0; i < data->columns; i++) {
		for (k = 0; k < 3 ; k++) {
			int v11;

			/* Compute starting point for next TTD row, weighing the adjacent pixels based on the HK factor.. */
			data->htd_ttdnext[offset] = hk[0] * (first[offset] + first[offset]) +
				hk[1] * (first[offset - 3] + first[offset + 3]) +
				hk[2] * (first[offset - 6] + first[offset + 6]) +
				hk[3] * (first[offset - 9] + first[offset + 9]);

			/* Add in fixed per-line compensation */
			out[offset] = in[offset] + line_comp[k];

			/* Cap and Scale */
			v11 = out[offset];
			if ( out[offset] > 65535.0 ) {
				out[offset] = 65535.0;
				v11 = 127;
			} else if (out[offset] < 0.0) {
				out[offset] = 0.0;
				v11 = 0;
			} else {
				v11 >>= 9;
			}

			/* Increment buckets */
			data->htd_fcc_scratch[k][v11]++;
			offset++;
		}
	}
}

static void CImageEffect70_CalcTTD(struct CImageEffect70 *data,
				   const uint16_t *in, double *out)
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

	/* If we have sharpening turned on, set up the state */
	if (data->sharpen >= 0)
		sharp = &data->cpc->SHK[8 * data->sharpen];

	/* For each pixel in the row... */
	for (i = 0 ; i < data->band_pixels ; i++) {
		double v4, v6, v7, v8;
		int v29;
		int v25;
		int v17;
		double ks_comp_f, k_comp, ks_comp, os_comp, sharp_comp;
		int j, k;

		/* Starting point is the carry-over from the last row plus
		   the new pixel */
		v8 = in[i];
		v7 = data->htd_ttdnext[i] - v8;
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
			int v5;
			if (j == 5)
				continue;

			v5 = in[i] - data->linebuf_row[j][i];
			if (v5 >= 0)
				k_comp += kp[j] * v5;
			else
				k_comp += km[j] * v5;
		}

		sharp_comp = 0.0;
		if (sharp) {
			for (k = 0 ; k < 8 ; k++) {
				sharp_comp += sharp[k] * (in[i] - data->linebuf_shrp[k][i]);
			}
		}
		/* Update output state based on input plus the
		   various correction factors */
		out[i] = v8 - v6 * os_comp + k_comp + sharp_comp;

		/* Work out the state for HTD operation */
		v4 = data->htd_ttdnext[i] - out[i];
		v17 = v4;
		if ( v17 >= 0 )
		{
			int v19 = 127;
			if ( v17 <= 65535 )
				v19 = v17 >> 9;
			ks_comp_f = ksp[v19];
		} else {
			int v18 = 127;
			if ( -v17 <= 65535 )
				v18 = -v17 >> 9;
			ks_comp_f = ksm[v18];
		}
		data->ttd_htd_first[i] = out[i] + v4 * ks_comp_f;
	}
}

/* Work out the number of times the density of a given color in 
   a given area exceeds a threshold */
static void CImageEffect70_CalcSA(struct BandImage *img,
				  int invert, int32_t *in,
				  int32_t revX, int32_t *out)
{
	int cols, rows;
	int16_t *buf, *ptr;
	int stride;
	int start_row, row, start_col, col;

	cols = img->cols - img->origin_cols;
	rows = img->rows - img->origin_rows;

	if ( img->bytes_per_row >= 0 ) {
		if ( invert ) {
			stride = img->bytes_per_row >> 1;
			buf = (int16_t*)img->imgbuf + stride * (rows - 1);
		} else {
			stride = -img->bytes_per_row >> 1;
			buf = img->imgbuf;
		}
	} else {
		if ( invert ) {
			stride = img->bytes_per_row >> 1;
			buf = img->imgbuf;
		} else {
			stride = -img->bytes_per_row >> 1;
			buf = (int16_t*)img->imgbuf + stride * (rows - 1);
		}
	}

	start_col = in[0];
	start_row = in[1];
	if ( cols > in[2] )
		cols = in[2];
	if ( rows > in[3] )
		rows = in[3];
	if ( start_row < 0 )
		start_row = 0;
	if ( start_col < 0 )
		start_col = 0;

	out[2] = 0;
	out[1] = 0;
	out[0] = 0;
  
	row = start_row;

	ptr = buf - start_row * stride;
	for ( row = start_row ; row < rows ; row++ ) {
		int16_t *v18 = ptr + 3 * start_col;
		col = start_col;
		for ( col = start_col ; col < cols ; col++) {
			out[0] += (revX <= v18[0]);
			out[1] += (revX <= v18[1]);
			out[2] += (revX <= v18[2]);
			v18 += 3;
		}
		ptr -= stride;
	}
}

static int CImageEffect70_JudgeReverseSkipRibbon_int(struct BandImage *img,
						     int32_t *REV,
						     int invert)
{
	int32_t rows, cols;

	int j;

	rows = img->rows - img->origin_rows;
	cols = img->cols - img->origin_cols;

	int32_t v16[4] = { REV[0], REV[2], REV[1], rows };
	int32_t v20[4] = { REV[1], 0, cols, rows };
	int32_t v24[4] = { 0, 0, REV[0], rows };
	int32_t v28[4] = { REV[0], 0, REV[1], REV[2] };

	/* Output buffers */
	int32_t v32[3] = { 0, 0, 0 };
	int32_t v35[3] = { 0, 0, 0 };
	int32_t v38[3] = { 0, 0, 0 };
	int32_t v41[3] = { 0, 0, 0 };

	/* Work out the density inherent in these areas */
	CImageEffect70_CalcSA(img, invert, v24, REV[3], v32);
	CImageEffect70_CalcSA(img, invert, v20, REV[7], v41);
	CImageEffect70_CalcSA(img, invert, v16, REV[11], v38);
	CImageEffect70_CalcSA(img, invert, v28, REV[15], v35);

	for (j = 0 ; j < 3 ; j++) {
		if ( v32[j] >= REV[4] &&
		     (v32[j] >= REV[5] || v38[j] >= REV[14] || v35[j] >= REV[18]) ) {
			return 0;
		}

		if ( v41[j] >= REV[8] &&
		     (v41[j] >= REV[9] || v38[j] >= REV[14] || v35[j] >= REV[18]) ) {
			return 0;
		}

		if ( v38[j] >= REV[12] &&
		     (v38[j] >= REV[13] || v32[j] >= REV[6] || v41[j] >= REV[10] || v35[j] >= REV[18]) ) {
			return 0;
		}

		if ( v35[j] >= REV[16] &&
		     (v35[j] >= REV[17] || v32[j] >= REV[6] || v41[j] >= REV[10] || v38[j] >= REV[14]) ) {
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
			offset = 0; // REV[0][0]
		} else {
			offset = 19; // REV[1][0]
		}
	} else if (param1 == 2) {
		if (is_6inch) {
			offset = 38; // REV[2][0]
		} else {
			offset = 57; // REV[3][0]
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
	double maxval[3];
	double *v9 = NULL;
	double *v10 = NULL;

	uint32_t i, j;
	int offset;
	int outstride;
	uint16_t *outptr;
	uint16_t *inptr;
	
	CImageEffect70_InitMidData(data);

	if (sharpen > 8)
		sharpen = 8;
	data->sharpen = sharpen;
	
	data->fhdiv_up = cpc->FH[0];
	data->fhdiv_dn = cpc->FH[1];
	data->fh_cur   = cpc->FH[2];
	data->fh_prev1 = cpc->FH[3] - cpc->FH[2];
	data->fh_prev2 = cpc->FH[4] - cpc->FH[3];
	data->fh_prev3 = cpc->FH[4];

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
	maxval[0] = cpc->GNMby[255];
	maxval[1] = cpc->GNMgm[255];
	maxval[2] = cpc->GNMrc[255];

	/* Initialize ttd_htd structures */
	offset = 0;
	for(j = 0; j < data->columns ; j++) {
		for (i = 0 ; i < 3 ; i++) {
			data->ttd_htd_scratch[offset++] = maxval[i];
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

static void CImageEffect70_DoGamma(struct CImageEffect70 *data, struct BandImage *input, struct BandImage *out, int reverse)
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

	/* HACK:  Reverse the row data when we perform gamma correction,
	          because Old Gutenprint sends it in the wrong order. */
	for (i = 0; i < rows; i++) {
		uint8_t *v10 = inptr;
		uint16_t *v9 = (uint16_t*)outptr;
		if (reverse)
			v9 += (cols - 1) * 3;
		for (j = 0 ; j < cols ; j++) {
			v9[0] = cpc->GNMby[v10[0]];
			v9[1] = cpc->GNMgm[v10[1]];
			v9[2] = cpc->GNMrc[v10[2]];
			v10 += 3;
			if (reverse)
				v9 -= 3;
			else
				v9 += 3;
		}
		inptr += in_stride;
		outptr += out_stride;
	}
}

static void dump_announce(void)
{
	fprintf(stderr, "INFO: libMitsuD70ImageReProcess version '%s' API %d\n", LIB_VERSION, LIB_APIVERSION);
	fprintf(stderr, "INFO: Copyright (c) 2016-2017 Solomon Peachy\n");
	fprintf(stderr, "INFO: This free software comes with ABSOLUTELY NO WARRANTY!\n");
	fprintf(stderr, "INFO: Licensed under the GNU GPL.\n");
	fprintf(stderr, "INFO: *** This code is NOT supported or endorsed by Mitsubishi! ***\n");
}

int do_image_effect80(struct CPCData *cpc, struct CPCData *ecpc, struct BandImage *input, struct BandImage *output, int sharpen, int reverse, uint8_t rew[2])
{
	struct CImageEffect70 *data;

	dump_announce();

	data = CImageEffect70_Create(cpc);
	if (!data)
		return -1;

	CImageEffect70_DoGamma(data, input, output, reverse);

	/* Figure out if we can get away with rewinding, or not... */
	if (cpc->REV[0]) {
		int is_6 = -1;

		/* Only allow rewinds for 4x6 and 5x3.5" prints */
		if (input->cols == 0x0620 && input->rows == 0x0434)
			is_6 = 0;
		else if (input->cols == 0x0748 && input->rows == 0x04c2)
			is_6 = 1;

		rew[1] = 1;
		if (ecpc == NULL)  /* IOW, only do the rewind check for SuperFine */
			rew[0] = 1;
		else if (is_6 != -1) {
			rew[0] = CImageEffect70_JudgeReverseSkipRibbon(cpc, output, is_6, 1);
		} else {
			rew[0] = 1;
		}
	}

	/* If we're rewinding, we have to switch to the other CPC file and restart the process */
	if (! rew[0] ) {
		CImageEffect70_Destroy(data);
		data = CImageEffect70_Create(ecpc);
		if (!data)
			return -1;

		CImageEffect70_DoGamma(data, input, output, reverse);
	}

	CImageEffect70_DoConv(data, cpc, output, output, sharpen);

	CImageEffect70_Destroy(data);

	return 0;
}

int do_image_effect60(struct CPCData *cpc, struct CPCData *ecpc, struct BandImage *input, struct BandImage *output, int sharpen, int reverse, uint8_t rew[2])
{
	struct CImageEffect70 *data;

	UNUSED(ecpc);

	dump_announce();

	data = CImageEffect70_Create(cpc);
	if (!data)
		return -1;

	CImageEffect70_DoGamma(data, input, output, reverse);
	CImageEffect70_DoConv(data, cpc, output, output, sharpen);

	/* Figure out if we can get away with rewinding, or not... */
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

int do_image_effect70(struct CPCData *cpc, struct CPCData *ecpc, struct BandImage *input, struct BandImage *output, int sharpen, int reverse, uint8_t rew[2])
{
	struct CImageEffect70 *data;

	UNUSED(ecpc);
	UNUSED(rew);

	dump_announce();

	data = CImageEffect70_Create(cpc);
	if (!data)
		return -1;

	CImageEffect70_DoGamma(data, input, output, reverse);
	CImageEffect70_DoConv(data, cpc, output, output, sharpen);
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
