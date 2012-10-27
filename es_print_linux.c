/*
 *   Canon SELPHY ES series print assister -- Native Linux version
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

#include "es_print_common.h"

#define BUF_LEN 4096

int dump_data_new(int remaining, int present, int data_fd, 
		  int dev_fd, uint8_t *buf, uint16_t buflen)
{
  int cnt;
  int i;
  int wrote;

  while (remaining > 0) {
    cnt = read(data_fd, buf, (remaining < buflen) ? remaining : buflen);

    if (cnt < 0)
      return -1;

    if (present) {
	    cnt += present;
	    present = 0;
    }

    i = write(dev_fd, buf, cnt);
    if (i < 0)
      return wrote;
    if (i != cnt) {
	    /* Realign buffer.. */
	    present = cnt - i;
	    memmove(buf, buf + i, present);
    }
    wrote += i;
    remaining -= cnt;
  }

  fprintf(stderr, "Wrote %d bytes (%d)\n", wrote, buflen);

  return wrote;
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
  data_fd = fileno(stdin);
  if (strcmp("-", argv[1])) {
    data_fd = open(argv[1], O_RDONLY);
    if (data_fd < 0) {
      perror("Can't open input file");
      exit(1);
    }
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
  read(data_fd, buffer, MAX_HEADER);

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
  return -1;

found:
  if (printer_type != printer_type2) {
    fprintf(stderr, "File intended for a %s printer, aborting!\n", models[printer_type]);
    return (-1);
  } else {
    fprintf(stderr, "Printing a %s file\n", models[printer_type]);
  }

  plane_len += 12; /* Add in plane header */
  paper_code_offset = paper_code_offsets[printer_type];
  if (paper_code_offset != -1)
    paper_code = paper_codes[printer_type][paper_code_offset];

  /* Rewind stream to start of first plane. */
  lseek(data_fd, init_lengths[printer_type], SEEK_SET);

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
    /* Realign plane data to start of buffer.. */
    memmove(buffer, buffer+init_lengths[printer_type],
	    MAX_HEADER-init_lengths[printer_type]);

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
    dump_data_new(plane_len, MAX_HEADER-init_lengths[printer_type], data_fd, dev_fd, buffer, BUF_LEN);
    state = S_PRINTER_Y_SENT;
    break;
  case S_PRINTER_Y_SENT:
    if (!fancy_memcmp(rdbuf, ready_m_readbacks[printer_type], RDBUF_LEN, paper_code_offset, paper_code)) {
      if (bw_mode)
	state = S_PRINTER_DONE;
      else
	state = S_PRINTER_READY_M;
    }
    break;
  case S_PRINTER_READY_M:
    fprintf(stderr, "Sending MAGENTA plane\n");
    dump_data_new(plane_len, 0, data_fd, dev_fd, buffer, BUF_LEN);
    state = S_PRINTER_M_SENT;
    break;
  case S_PRINTER_M_SENT:
    if (!fancy_memcmp(rdbuf, ready_c_readbacks[printer_type], RDBUF_LEN, paper_code_offset, paper_code)) {
      state = S_PRINTER_READY_C;
    }
    break;
  case S_PRINTER_READY_C:
    fprintf(stderr, "Sending CYAN plane\n");
    dump_data_new(plane_len, 0, data_fd, dev_fd, buffer, BUF_LEN);
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
      dump_data_new(foot_lengths[printer_type], 0, data_fd, dev_fd, buffer, BUF_LEN);
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




