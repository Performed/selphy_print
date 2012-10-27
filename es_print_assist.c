/*
 *   Canon SELPHY ES1 series print assister 
 *
 *   (c) 2007-2011 Solomon Peachy <pizza@shaftnet.org>
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

// Compile with:  gcc -o es_print -Wall es_print_assist.c

#define VERSION "0.14"

#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

#define BUF_LEN 4096
#define RDBUF_LEN 12

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

int dump_data_new(int remaining, int data_fd, int dev_fd, uint8_t *buf, uint16_t buflen)
{
  int cnt;
  int i;
  int wrote;

  while (remaining) {
    cnt = read(data_fd, buf, (remaining < buflen) ? remaining : buflen);

    if (cnt < 0)
      return -1;

    i = write(dev_fd, buf, cnt);
    if (i < 0)
      return wrote;
    if (i != cnt) {  // XXX worth handling this?  retry write?
      fprintf(stderr, "write mismatch R/W %d/%d ...\n", cnt, i);
      return wrote;
    }
    wrote += i;
    remaining -= cnt;
  }

  fprintf(stderr, "Wrote %d bytes (%d)\n", wrote, buflen);

  return wrote;
}

#define INCORRECT_PAPER -999

int fancy_memcmp(const uint8_t *buf_a, const int16_t *buf_b, uint len, int16_t papercode_offset, int16_t papercode_val) 
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

int main(int argc, char **argv) 
{
  int dev_fd, data_fd;

  uint8_t buffer[BUF_LEN];
  uint8_t rdbuf[RDBUF_LEN];
  uint8_t rdbuf2[RDBUF_LEN];

  int last_state, state = S_IDLE;
  int printer_type = P_END;
  int printer_type2 = P_END;
  int plane_len = 0;
  int bw_mode = 0;
  int extra_offset = 0;
  int16_t paper_code_offset = -1;
  int16_t paper_code = -1;

  /* Static initialization */
  setup_paper_codes();

  /* Cmdline help */
  if (argc < 2) {
    fprintf(stderr, "SELPHY ES Print Assist version %s\n\nUsage:\n\t%s infile outdev\n",
	    VERSION,
	    argv[0]);
    fprintf(stderr, "\n");
    exit(1);
  }

  /* Open input file */
  data_fd = open(argv[1], O_RDONLY);
  if (data_fd < 0) {
    perror("Can't open input file");
    exit(1);
  }

  /* Open output device */
  dev_fd = open(argv[2], O_RDWR);
  if (dev_fd < 0) {
    perror("Can't open output device");
    exit(1);
  }

  /* Figure out the printer type based on the readback */
  read(dev_fd, rdbuf, RDBUF_LEN);
  for (printer_type2 = 0; printer_type2 < P_END ; printer_type2++) {
    if (!fancy_memcmp(rdbuf, init_readbacks[printer_type2], RDBUF_LEN, -1, -1))
      break;
  }
  if (printer_type2 == P_END) {
    fprintf(stderr, "Unrecognized printer!\n");
    fprintf(stderr, "readback:  %02x %02x %02x %02x  %02x %02x %02x %02x  %02x %02x %02x %02x\n",
	    rdbuf[0], rdbuf[1], rdbuf[2], rdbuf[3],
	    rdbuf[4], rdbuf[5], rdbuf[6], rdbuf[7],
	    rdbuf[8], rdbuf[9], rdbuf[10], rdbuf[11]);
  } 
  
  /* Figure out printer this file is intended for */
  read(data_fd, buffer, MAX_HEADER + 1024);  /* Read in header */

  if (buffer[0] != 0x40 &&
      buffer[1] != 0x00) {
    fprintf(stderr, "Unrecognized file format!\n");
    return(-1);
  }

  if (buffer[12] == 0x40 &&
      buffer[13] == 0x01) {
    if (buffer[2] == 0x00) {
      printer_type = P_CP_XXX;  /* Unpadded */
    } else {
      printer_type = P_ES1;
      bw_mode = (buffer[2] == 0x20);
    }

    plane_len = *(uint32_t*)(&buffer[16]);
    plane_len = cpu_to_le32(plane_len);
    goto found;
  }

  plane_len = cpu_to_le32(plane_len);
  plane_len = *(uint32_t*)(&buffer[12]);

  if (plane_len == 0) {
    printer_type = P_CP_XXX;
    extra_offset = 1012; /* Work around gutenprint's added padding for some CP-XXX models. */
    goto found;
  }

  if (buffer[16] == 0x40 &&
      buffer[17] == 0x01) {

    if (buffer[4] == 0x02) {
      printer_type = P_ES2_20;
      bw_mode = (buffer[7] == 0x01);
      goto found;
    }
    
    if (es40_plane_lengths[buffer[2]] == plane_len) {
      printer_type = P_ES40; 
      bw_mode = (buffer[3] == 0x01);
      goto found;
    } else {
      printer_type = P_ES3_30; 
      bw_mode = (buffer[3] == 0x01);
      goto found;
    }
  }

  fprintf(stderr, "Unrecognized file format!\n");

found:
  if (printer_type != printer_type2) {
    fprintf(stderr, "File intended for a %s printer, aborting!\n", models[printer_type]);
    return (-1);
  } else {
    fprintf(stderr, "Printing a %s file %s\n", models[printer_type], extra_offset? "(padded)" : "");
  }

  plane_len += 12; /* Add in plane header */
  paper_code_offset = paper_code_offsets[printer_type];
  if (paper_code_offset != -1)
    paper_code = paper_codes[printer_type][paper_code_offset];

  /* Rewind stream to start of first plane. */
  lseek(data_fd, init_lengths[printer_type] + extra_offset, SEEK_SET);

 top:

  read(dev_fd, rdbuf, RDBUF_LEN);  /* Read the status from printer */
  if (memcmp(rdbuf, rdbuf2, RDBUF_LEN)) {
    fprintf(stderr, "readback:  %02x %02x %02x %02x  %02x %02x %02x %02x  %02x %02x %02x %02x\n",
	    rdbuf[0], rdbuf[1], rdbuf[2], rdbuf[3],
	    rdbuf[4], rdbuf[5], rdbuf[6], rdbuf[7],
	    rdbuf[8], rdbuf[9], rdbuf[10], rdbuf[11]);
    memcpy(rdbuf2, rdbuf, RDBUF_LEN);
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
    if (!fancy_memcmp(rdbuf, init_readbacks[printer_type], RDBUF_LEN, paper_code_offset, paper_code)) {
      state = S_PRINTER_READY;
      break;
    }
    break;
  case S_PRINTER_READY:
    fprintf(stderr, "Sending init sequence (%d bytes)\n", init_lengths[printer_type]);
    write(dev_fd, buffer, init_lengths[printer_type]); /* Send printer_init */
    state = S_PRINTER_INIT_SENT;
    break;
  case S_PRINTER_INIT_SENT:
    if (!fancy_memcmp(rdbuf, ready_y_readbacks[printer_type], RDBUF_LEN, paper_code_offset, paper_code)) {
      state = S_PRINTER_READY_Y;
    }
    break;
  case S_PRINTER_READY_Y:
    if (bw_mode)
      fprintf(stderr, "Sending BLACK plane\n");
    else
      fprintf(stderr, "Sending YELLOW plane\n");
    dump_data_new(plane_len, data_fd, dev_fd, buffer, BUF_LEN);
    state = S_PRINTER_Y_SENT;
    break;
  case S_PRINTER_Y_SENT:
    // handle bw_mode?  transition to S_PRINTER_DONE?
    if (!fancy_memcmp(rdbuf, ready_m_readbacks[printer_type], RDBUF_LEN, paper_code_offset, paper_code)) {
      state = S_PRINTER_READY_M;
    }
    break;
  case S_PRINTER_READY_M:
    fprintf(stderr, "Sending MAGENTA plane\n");
    dump_data_new(plane_len, data_fd, dev_fd, buffer, BUF_LEN);
    state = S_PRINTER_M_SENT;
    break;
  case S_PRINTER_M_SENT:
    if (!fancy_memcmp(rdbuf, ready_c_readbacks[printer_type], RDBUF_LEN, paper_code_offset, paper_code)) {
      state = S_PRINTER_READY_C;
    }
    break;
  case S_PRINTER_READY_C:
    fprintf(stderr, "Sending CYAN plane\n");
    dump_data_new(plane_len, data_fd, dev_fd, buffer, BUF_LEN);
    state = S_PRINTER_C_SENT;
    break;
  case S_PRINTER_C_SENT:
    if (!fancy_memcmp(rdbuf, done_c_readbacks[printer_type], RDBUF_LEN, paper_code_offset, paper_code)) {
      state = S_PRINTER_DONE;
    }
    break;
  case S_PRINTER_DONE:
    if (foot_lengths[printer_type]) {
      fprintf(stderr, "Sending cleanup sequence\n");
      dump_data_new(foot_lengths[printer_type], data_fd, dev_fd, buffer, BUF_LEN);
    }
    state = S_FINISHED;
    break;
  case S_FINISHED:
    fprintf(stderr, "All data sent to printer!\n");
    break;
  }
  if (state != S_FINISHED)
    goto top;

  return 0;
}


/* 

 Selphy ES1:

   Init func:   40 00 [typeA] [pgcode] 00 00 00 00 00 00 00 00
   Plane func:  40 01 [typeB] [plane] [length, 32-bit LE] 00 00 00 00 

   TypeA codes are 0x10 for Color papers, 0x20 for B&W papers.
   TypeB codes are 0x01 for Color papers, 0x02 for B&W papers.

   Plane codes are 0x01, 0x03, 0x07 for Y, M, and C, respectively.
   B&W Jobs have a single plane code of 0x01.

   'P' papers pgcode of 0x11 and a plane length of 2227456 bytes
   'CP_L'     pgcode of 0x12 and a plane length of 1601600 bytes.
   'Card'     pgcode of 0x13 and a plane length of  698880 bytes.

   Readback values seen:

   02 00 00 00 02 01 [pg] 01 00 00 00 00   [idle, waiting for init seq]
   04 00 00 00 02 01 [pg] 01 00 00 00 00   [init received, not ready..]
   04 00 01 00 02 01 [pg] 01 00 00 00 00   [waiting for Y data]
   04 00 03 00 02 01 [pg] 01 00 00 00 00   [waiting for M data]
   04 00 07 00 02 01 [pg] 01 00 00 00 00   [waiting for C data]
   04 00 00 00 02 01 [pg] 01 00 00 00 00   [all data sent; not ready..]
   05 00 00 00 02 01 [pg] 01 00 00 00 00   [?? transitions to this]
   06 00 00 00 02 01 [pg] 01 00 00 00 00   [?? transitions to this]
   02 00 00 00 02 01 [pg] 01 00 00 00 00   [..transitions back to idle]

   Readbacks for other paper types are currently unknown.

   Known paper types for all ES printers:  P, Pbw, C, Cl
   Additional types for ES3/30/40:         Pg, Ps

   Known pg codes:   0x01   -- P-size
                     0x03   -- C-size

   P*   sizes are 100x148mm "postcards"
   CP_L sizes are 89x119mm "labels"
   Card sizes are 54x86mm "cards"

*/

/* 

 Selphy ES2/20:

   Init func:   40 00 [pgcode] 00  02 00 00 [type]  00 00 00 [pg2] [length, 32-bit LE]
   Plane func:  40 01 [plane] 00  00 00 00 00  00 00 00 00 

   Type codes are 0x00 for Color papers, 0x01 for B&W papers.

   Plane codes are 0x01, 0x02, 0x03 for Y, M, and C, respectively.
   B&W Jobs have a single plane code of 0x01.

   'P' papers pgcode of 0x01 and a plane length of 2227456 bytes
   'CP_L'     pgcode of 0x02 and a plane length of 1601600 bytes.
   'Card'     pgcode of 0x03 and a plane length of  698880 bytes.

   pg2 is 0x00 for all media types except for 'Card', which is 0x01.

   Readback values seen:

   02 00 00 00 [pg] 00 [pg2] [xx] 00 00 00 00   [idle, waiting for init seq]
   03 00 01 00 [pg] 00 [pg2] [xx] 00 00 00 00   [init complete, ready for Y]
   04 00 01 00 [pg] 00 [pg2] [xx] 00 00 00 00   [? paper loaded]
   05 00 01 00 [pg] 00 [pg2] [xx] 00 00 00 00   [? transitions to this]
   06 00 03 00 [pg] 00 [pg2] [xx] 00 00 00 00   [ready for M]
   08 00 03 00 [pg] 00 [pg2] [xx] 00 00 00 00   [? transitions to this]
   09 00 07 00 [pg] 00 [pg2] [xx] 00 00 00 00   [ready for C]
   09 00 00 00 [pg] 00 [pg2] 00 00 00 00 00   [? transitions to this]
   0b 00 00 00 [pg] 00 [pg2] 00 00 00 00 00   [? transisions to this]
   0c 00 00 00 [pg] 00 [pg2] 00 00 00 00 00   [? transitions to this]
   0f 00 00 00 [pg] 00 [pg2] 00 00 00 00 00   [? transitions to this]
   13 00 00 00 [pg] 00 [pg2] 00 00 00 00 00   [? transitions to this]
   02 00 00 00 [pg] 00 [pg2] 00 00 00 00 00   [back to idle, waiting for init seq]

   [xx] can be 0x00 or 0xff, depending on if a print job has completed or not.

   14 00 00 00 [pg] 00 [pg2] 00 00 00 00 00   [out of paper/ink]
   14 00 01 00 [pg] 00 [pg2] 00 01 00 00 00   [out of paper/ink]

   [pg] is:  0x01 for P-paper
             0x03 for C-paper

   [pg2] is: 0x00 for P-paper
             0x01 for C-paper (label)
*/

/* 

 Selphy ES3/30:

   Init func:   40 00 [pgcode] [type]  00 00 00 00  00 00 00 00 [length, 32-bit LE]
   Plane func:  40 01 [plane] 00  00 00 00 00  00 00 00 00 

   End func:    40 20 00 00  00 00 00 00  00 00 00 00

   Type codes are 0x00 for Color papers, 0x01 for B&W papers.

   Plane codes are 0x01, 0x02, 0x03 for Y, M, and C, respectively.
   B&W Jobs have a single plane code of 0x01.

   'P' papers pgcode of 0x01 and a plane length of 2227456 bytes.
   'CP_L'     pgcode of 0x02 and a plane length of 1601600 bytes.
   'Card'     pgcode of 0x03 and a plane length of  698880 bytes.

   Readback values seen with standard 'P-Color' and 'C-Label' cartridges:

   00 ff 00 00 ff ff ff ff 00 00 00 00   [idle, waiting for init seq]
   01 ff 01 00 ff ff ff ff 00 00 00 00   [init complete, ready for Y]
   03 ff 01 00 ff ff ff ff 00 00 00 00   [?]
   03 ff 02 00 ff ff ff ff 00 00 00 00   [ready for M]
   05 ff 02 00 ff ff ff ff 00 00 00 00   [?]
   05 ff 03 00 ff ff ff ff 00 00 00 00   [ready for C]
   07 ff 03 00 ff ff ff ff 00 00 00 00   [?]
   0b ff 03 00 ff ff ff ff 00 00 00 00   [?]
   13 ff 03 00 ff ff ff ff 00 00 00 00   [?]
   00 ff 10 00 ff ff ff ff 00 00 00 00   [done, ready for footer]

*/

/* 

 Selphy ES40:   (May be supported if necessary)

   Init func:   40 00 [pgcode] [type]  00 00 00 00  00 00 00 00 [length, 32-bit LE]
   Plane func:  40 01 [plane] 00  00 00 00 00  00 00 00 00 

   End func:    40 20 00 00  00 00 00 00  00 00 00 00

   Type codes are 0x00 for Color papers, 0x01 for B&W papers.

   Plane codes are 0x01, 0x02, 0x03 for Y, M, and C, respectively.
   B&W Jobs have a single plane code of 0x01.

   'P' papers pgcode of 0x00 and a plane length of 2227456 bytes.
   'CP_L'     pgcode of 0x01 and a plane length of 1601600 bytes.
   'Card'     pgcode of 0x02 and a plane length of  698880 bytes.

   Readback codes are unknown.

*/

/* 

 Selphy CP790:   (May be supported if necessary)

   Init func:   40 00 [pgcode] 00  00 00 00 00  00 00 00 00 [length, 32-bit LE]
   Plane func:  40 01 [plane] 00  00 00 00 00  00 00 00 00 

   End func:    40 20 00 00  00 00 00 00  00 00 00 00

   Plane codes are 0x01, 0x02, 0x03 for Y, M, and C, respectively.
   B&W Jobs have a single plane code of 0x01.

   'P' papers pgcode of 0x00 and a plane length of 2227456 bytes.
   'CP_L'     pgcode of 0x01 and a plane length of 1601600 bytes.
   'Card'     pgcode of 0x02 and a plane length of  698880 bytes.
   'Wide'     pgcode of 0x03 and a plane length of 2976512 bytes.

   Readback codes are unknown.

*/

/*

 Selphy CP-XXX (except for CP-790):
 
   Init func:   40 00 00 [pgcode] 00 00 00 00 00 00 00 00
   Plane func:  40 01 00 [plane] [length, 32-bit LE] 00 00 00 00 

   plane codes are 0x00, 0x01, 0x02 for Y, M, and C, respectively.

   'P' paper     pgcode 0x01   plane length 2227456 bytes.
   'CP_L'        pgcode 0x02   plane length 1601600 bytes.
   'Card' paper  pgcode 0x03   plane length  698880 bytes.
   'Wide' paper  pgcode 0x04   plane length 2976512 bytes.

   Known readback values from a SELPHY CP-760:

   01 00 00 00 00 00 [pg] 00 00 00 00 00   [idle, waiting for init]
   02 00 00 00 70 00 [pg] 00 00 00 00 00   [waiting for Y data]
   04 00 00 00 00 00 [pg] 00 00 00 00 00   [waiting for M data]
   08 00 00 00 00 00 [pg] 00 00 00 00 00   [waiting for C data]
   10 00 00 00 00 00 [pg] 00 00 00 00 00   [?]
   20 00 00 00 00 00 [pg] 00 00 00 00 00   [?]

   'P'    paper has a code of 0x11
   'CP_L' paper has a code of 0x22

   Readback codes for other paper types are unknown.

*/



