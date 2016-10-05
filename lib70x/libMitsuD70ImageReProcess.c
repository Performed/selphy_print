/* LibMitsuD70ImageReProcess -- Re-implemented image processing library for
                                the Mitsubishi CP-D70 family of printers

   Copyright (c) 2016 Solomon Peachy <pizza@shaftnet.org>

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

#define LIB_VERSION "0.1"

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

struct CColorConv3D {
	uint8_t lut[17][17][17][3];
};

/* Defines an image.  Note that origin_cols/origin_rows should always = 0 */
struct BandImage {
	   void  *imgbuf;      //  @0
	 int32_t bytes_per_row;//  @4  bytes per row (respect 8bpp and 16bpp!)
	uint16_t origin_cols;  // @8  origin_cols
	uint16_t origin_rows;  // @12  origin_rows
	uint16_t cols;         // @16  cols
	uint16_t rows;         // @20  rows
	                       // @24
};

/* State for image processing algorithm */
struct CImageEffect70 {
	uint32_t pad;            // @0
	  double *unk_0001;      // @4/1      
	  double *unk_0002;      // @8/2      
	  double *unk_0003;      // @12/3     
	  double *unk_0004;      // @16/4     
	  double unk_0005[3];    // @20/5     
	uint32_t unk_0011[384];  // @44/11    
	  double unk_0395[384];  // @1580/395
	  double *unk_1163;      // @4652/1163
	uint16_t *unk_1164;      // @4656/1164
	uint16_t *unk_1165[11];  // @4660/1165
	uint16_t *unk_1176[11];  // @4704/1176
	uint16_t *unk_1187[8];   // @4748/1187   // sharpening state?
	struct CPCData *cpc;     // @4780/1195
	 int32_t sharpen;        // @4784/1196   // -1 off, max 8.
	uint32_t columns;        // @4788/1197
	uint32_t rows;           // @4792/1198
	uint32_t pixel_count;    // @4796/1199
	uint32_t row_count;      // @4800/1200
	uint32_t band_pixels;    // @4804/1201
	uint32_t unk_1202;       // @4808/1202   // band_pixels + 6
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
	double   KM[11];         // @40536
	/* Used for HTD */
	double   HK[4];          // @40616
	
	uint32_t Speed[3];       // @40648
	/* Used for FCC */
	double   FH[5];          // @40660
	/* Used for sharpening */
	double   SHK[72];        // @40700
	/* Used for YMC6 */
	double   UH[101];        // @41276
	                         // @42084
	/* Unused */
	uint32_t ROLK[13];       // NOT USED by D70 lib!
	uint32_t REV[76];        // NOT USED by D70 lib (and missing in D70/ASK300 CPC)
};

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
		if (line < 13) { // ROLK, optional?
			ptr = strtok(NULL, delim);
			if (!ptr)
				continue;
			data->ROLK[line] = strtol(ptr, NULL, 10);

		}
		if (line < 76) { // REV, optional?
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
	data->unk_1164 = malloc(22 * data->unk_1202);
	memset(data->unk_1164, 0, (22 * data->unk_1202));
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
				   int a2, uint16_t *row, int a4)
{
	uint16_t *src, *v5;

	src = data->unk_1165[a2 + 5];
	v5 = src + 3 * (data->columns - 1);

	memcpy(src, row -(a4 * data->pixel_count), 2 * data->band_pixels);
	
	memcpy(src -3, src, 6);
	memcpy(v5 + 3, v5, 6);	
	
}

static void CImageEffect70_Sharp_PrepareLine(struct CImageEffect70 *data,
					     uint16_t *row)
{
	int n;
	uint32_t i;
	
	CImageEffect70_Sharp_CopyLine(data, 0, row, 0);
	n = 2 * data->unk_1202;
	for (i = 0 ; i < 5 ; i++) {
		memcpy(data->unk_1176[i], data->unk_1176[5], n);
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

static void CImageEffect70_CalcYMC6(struct CImageEffect70 *data, double *a2, uint16_t *imgdata)
{
	uint16_t i, j;
	uint32_t offset;	
	uint16_t uh_offset;
	
	double uh_val;

	offset = 0;
	uh_offset = data->rows - 1 - data->row_count;
	if ( uh_offset > 100 )
		uh_offset = 100;

	uh_val = data->cpc->UH[uh_offset];

	for ( i = 0; i < data->columns; i++ ) {
		for ( j = 0; j < 3; j++ ) {
			double v4 = data->unk_0005[j] * data->unk_0395[j*128 + ((int)a2[offset] >> 9)] * a2[offset] * uh_val;
			if ( v4 <= 65535.0 ) {
				if ( v4 >= 0.0 )
					imgdata[offset] = (int)v4;
				else
					imgdata[offset] = 0;
			} else {
				imgdata[offset] = -1;
			}
			++offset;
		}
	}

}

static void CImageEffect70_CalcFCC(struct CImageEffect70 *data)
{
	double s[3];
	double *v6;
	double v5;
	int v3;
	int v7;
	int i, j;
	double *v12, *v11, *v10;
	
	v7 = data->row_count;
	v6 = &data->unk_1163[3*v7];

	for (i = 0 ; i < 3 ; i++) {
		v6[i] = 127 * data->unk_0011[127 + 128*i];
	}
	for (i = 126 ; i >= 0 ; i--) {
		for (j = 0 ; j < 3 ; j++) {
			v6[j] += i * data->unk_0011[j*128 + i];
			data->unk_0011[j*16+i] = data->unk_0011[j*128+i+1];
		}
	}
	if (v7 > 2) {
		v12 = v6 - 3;
		v11 = v6 - 6;
		v10 = v6 - 9;
	} else if (v7 == 2) {
		v12 = v6 - 3;
		v11 = v6 - 6;
		v10 = v6 - 6;;		
	} else if (v7 == 1) {
		v12 = v6 - 3;
		v11 = v6 - 3;
		v10 = v6 - 3;;
	} else {
		v12 = v6;
		v11 = v6;
		v10 = v6;
	}

	for (i = 0 ; i < 3 ; i++) {
		v6[i] /= data->columns;

		v5 = data->unk_1207 * v6[i] *
			data->unk_1209 * v12[i] *
			data->unk_1211 * v11[i] *
			data->unk_1213 * v10[i];
		if (v5 > 0.0) {
			data->unk_0005[i] = v5 / data->unk_1203 + 1.0;
		} else {
			data->unk_0005[i] = v5 / data->unk_1205 + 1.0;
		}
	}

	memset(s, 0, sizeof(s));
	
	for (i = 0 ; i < 128 ; i++) {
		for (j = 0 ; j < 3 ; j++) {
			v3 = 255 * data->unk_0011[j*128 + i] / 1864;
			if (v3 > 255)
				v3 = 255;
			s[j] += data->cpc->FM[v3];
			data->unk_0395[i + j*128] = s[j] / (i + 1);
		}
	}
}

static void CImageEffect70_CalcHTD(struct CImageEffect70 *data, double *a2, double *a3)
{
	int v10, v16;
	double *v9, *v7;
	double *v5;
	double *src;
	int v13;
	int i, k;
	int v4[3];
	int v11;

	v10 = data->columns;
	v9 = data->cpc->HK;
	src = data->unk_0002;
	v7 = data->unk_0004;
	memset(data->unk_0011, 0, 1536);
	v16 = data->row_count;
	if (v16 > 2729)
		v16 = 2729;
	for (i = 0 ; i < 3 ; i++) {
		if (i == 0)
			v4[i] = data->cpc->LINEy[v16];
		else if (i == 1)
			v4[i] = data->cpc->LINEm[v16];
		else if (i == 2)
			v4[i] = data->cpc->LINEc[v16];
	}

	v5 = data->unk_0003;
	memcpy(src - 9, src, 0x18);
	memcpy(src - 6, src, 0x18);
	memcpy(src - 3, src, 0x18);
	memcpy(v5 + 3, v5, 0x18);
	memcpy(v5 + 6, v5, 0x18);
	memcpy(v5 + 9, v5, 0x18);
	v13 = 0;
	for (i = 0; i < v10; i++) {
		for (k = 0; k < 3 ; k++) {
			v7[13] = v9[0] * (src[v13] + src[v13]) +
				v9[1] * (src[v13 - 3] + src[v13 + 3]) +
				v9[2] * (src[v13 - 6] + src[v13 + 6]) +
				v9[3] * (src[v13 - 9] + src[v13 + 9]);

			a3[v13] = a2[v13] + v4[k];
			v11 = a3[v13];
			if ( a3[v13] < 65535.0 ) {
				if (a3[v13] >= 0.0) {
					v11 >>= 9;
				} else {
					a3[v13] = 0.0;
					v11 = 0;
				}
			} else {
				a3[v13] = 65535.0;
				v11 = 127;
			}

			data->unk_0011[k*128 + v11] ++;  // ???
			v13++;
		}
	}
}

static void CImageEffect70_CalcTTD(struct CImageEffect70 *data,
			    uint16_t *a2, double *a3)
{
	double *v11, *v12, *v13, *v14, *v15, *v16;
	double *v34;
	uint32_t i;
	v16 = data->cpc->KSP;
	v15 = data->cpc->KSM;
	v14 = data->cpc->OSP;
	v13 = data->cpc->OSM;
	v12 = data->cpc->KP;
	v11 = data->cpc->KM;

	v34 = NULL;

	if (data->sharpen >= 0) {
		v34 = &data->cpc->SHK[8 * data->sharpen];
	}
	for (i = 0 ; i < data->band_pixels ; i++) {
		int v5;
		double v4, v6, v7, v8;
		int v29;
		int v25;
		int v17;
		double v20, v24, v32, v28, v22;
		int j, k;

		v8 = a2[i];
		v7 = data->unk_0004[i] - v8;
		v29 = v7;
		if (v29 >= 0) {
			int v31;
			if (v29 <= 65535)
				v31 = -v29 >> 9;
			else
				v31 = 127;
			v32 = v16[v31];
		} else {
			int v30;
			if (-v29 <= 65535)
				v30 = -v29 >> 9;
			else
				v30 = 127;
			v32 = v15[v30];
		}
		v6 = v7 * v32 + v8 - v8;  // WTF?
		v25 = v6;
		if (v25 >= 0) {
			int v27;
			if (v25 <= 65535)
				v27 = v25 >> 9;
			else
				v27 = 127;
			v28 = v14[v27];
		} else {
			int v26;
			if (-v25 <= 65535)
				v26 = -v25 >> 9;
			else
				v26 = 127;
			v28 = v13[v26];
		}
		v24 = 0.0;
		for ( j = 0 ; j < 11 ; j++) {
			if (j == 5)
				continue;

			v5 = a2[i] - data->unk_1165[j][i];
			if (v5 >= 0)
				v24 += v11[j] * v5;
			else
				v24 += v12[j] * v5;
		}
		v22 = 0.0;
		if (v34) {
			for (k = 0 ; k < 8 ; k++) {
				v22 += v34[k] * (a2[i] - data->unk_1187[k][i]);
			}
		}
		a3[i] = v8 - v6 * v28 + v24 + v22;
		v4 = data->unk_0004[i] - a3[i];
		v17 = v4;

		if ( v17 >= 0 )
		{
			int v19;
			if ( v17 <= 65535 )
				v19 = v17 >> 9;
			else
				v19 = 127;
			v20 = v16[v19];
		}
		else
		{
			int v18;
			if ( -v17 <= 65535 )
				v18 = -v17 >> 9;
			else
				v18 = 127;
			v20 = v15[v18];
		}
		data->unk_0002[i] = a3[i] + v4 * v20;
	}
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
	int v14;
	uint16_t *v15;
	uint16_t *v16;
	
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

		v14 = out->bytes_per_row / 2; // pixels per dest band (negative!)
		v16 = (uint16_t*) in->imgbuf + data->pixel_count * (data->rows - 1); // ie last row of input buffer
		v15 = (uint16_t*) out->imgbuf + v14 * (data->rows - 1); // end of output buffer?
	} else {
		data->pixel_count = in->bytes_per_row / 2;
		v14 = out->bytes_per_row / 2;
		v16 = in->imgbuf;
		v15 = out->imgbuf;
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
	
	CImageEffect70_Sharp_PrepareLine(data, v16);

	if (data->sharpen >= 0)
		CImageEffect70_Sharp_SetRefPtr(data);

	for (data->row_count = 0 ; data->row_count < data->rows ; data->row_count++) {
		if (data->row_count + 5 < data->rows)
			CImageEffect70_Sharp_CopyLine(data, 5, v16, 5);
		CImageEffect70_CalcTTD(data, v16, v10);
		CImageEffect70_CalcHTD(data, v10, v9);
		CImageEffect70_CalcFCC(data);
		CImageEffect70_CalcYMC6(data, v9, v15);
		v16 -= data->pixel_count; // work backwards one input row
		v15 -= v14;               // work backwards one output row
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

	uint16_t *outptr;	
	uint8_t *inptr;

	struct CPCData *cpc = data->cpc;
	
	cols = input->cols - input->origin_cols;
	rows = input->rows - input->origin_rows;

	if (cols <= 0 || rows <= 0)
	    return;

	inptr = (uint8_t*) input->imgbuf;
	outptr = out->imgbuf;

	for (i = 0; i < rows; i++) {
		uint8_t *v10 = inptr;
		uint16_t *v9 = outptr;
		for (j = 0 ; j < cols ; j++) {
			v9[0] = cpc->GNMby[v10[0]];
			v9[1] = cpc->GNMgm[v10[1]];
			v9[2] = cpc->GNMrc[v10[2]];
			v10 += 3;
			v9 += 3;
		}
		inptr += input->bytes_per_row;
		outptr += out->bytes_per_row >> 1;
	}
}

int do_image_effect(struct CPCData *cpc, struct BandImage *input, struct BandImage *output,
		     int sharpen)
{
	struct CImageEffect70 *data;

	fprintf(stderr, "INFO: libMitsuD70ImageReProcess version '%s'\n", LIB_VERSION);
	fprintf(stderr, "INFO: Copyright (c) 2016 Solomon Peachy\n");  
	fprintf(stderr, "INFO: This free software comes with ABSOLUTELY NO WARRANTY!\n");
	fprintf(stderr, "INFO: Licensed under the GNU GPL.\n");
	fprintf(stderr, "INFO: *** This code is NOT supported or endorsed by Mitsubishi! ***\n");
	
	data = CImageEffect70_Create(cpc);
	if (!data)
		return -1;
	
	CImageEffect70_DoGamma(data, input, output);
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
	uint16_t *v15, *v9;
	size_t count;
	
	rows = out->cols - out->origin_cols;
	cols = out->rows - out->origin_rows;
	buf = malloc(256*1024);
	if (!buf)
		goto done;
	if (!callback_fn)
		goto done;
	if (out->bytes_per_row < 0) { // XXX note this is reversed.
		v15 = out->imgbuf + ((cols - 1) * out->bytes_per_row);
	} else {
		v15 = out->imgbuf;
	}

	for ( i = 0 ; i < 3 ; i++) {
		uint16_t *v13, *v12;

		v13 = &v15[i];
		v12 = buf;
		count = 0;
		memset(buf, 0, 256*1024);
		for (j = 0 ; j < cols ; j++) {
			v9 = v13;
			for (k = 0 ; k < rows ; k++) {
				*v12++ = cpu_to_be16(*v9);
				v9 += 3;
				count += 2;
				if ( count == 256*1024 )
				{
					if (callback_fn(context, buf, count))
						goto done;
					count = 0;
					v12 = buf;
					memset(buf, 0, 256*1024);
				}
			}
			v13 += out->bytes_per_row / 2; // XXX here too.
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
