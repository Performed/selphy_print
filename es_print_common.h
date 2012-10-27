/*
 *   Canon SELPHY ES series print assister -- Common Code
 *
 *   (c) 2007-2012 Solomon Peachy <pizza@shaftnet.org>
 *
 *   The latest version of this program can be found at
 *  
 *   http://git.shaftnet.org/git/gitweb.cgi?p=selphy_print.git
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

#define VERSION "0.20"

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

#define INCORRECT_PAPER -999

static int fancy_memcmp(const uint8_t *buf_a, const int16_t *buf_b, uint len, int16_t papercode_offset, int16_t papercode_val) 
{
  int i;

  for (i = 0 ; i < len ; i++) {
    if (papercode_offset != -1 && i == papercode_offset) {
      if (papercode_val == -1)
	continue;
      else if (buf_a[i] != papercode_val)
	return INCORRECT_PAPER;
    } else if (buf_b[i] == -1)
      continue;
    else if (buf_a[i] > buf_b[i])
      return 1;
    else if (buf_a[i] < buf_b[i])
      return -1;
  }
  return 0;
}

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
