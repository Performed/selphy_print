/*
 *   CUPS Backend common code
 *
 *   (c) 2013-2019 Solomon Peachy <pizza@shaftnet.org>
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
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *          [http://www.gnu.org/licenses/gpl-3.0.html]
 *
 *   SPDX-License-Identifier: GPL-3.0+
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <libusb.h>
#include <arpa/inet.h>

#ifndef __BACKEND_COMMON_H
#define __BACKEND_COMMON_H

#define STR_LEN_MAX 64
#define STATE( ... ) do { if (!quiet) fprintf(stderr, "STATE: " __VA_ARGS__ ); } while(0)
#define ATTR( ... ) do { if (!quiet) fprintf(stderr, "ATTR: " __VA_ARGS__ ); } while(0)
#define PAGE( ... ) do { if (!quiet) fprintf(stderr, "PAGE: " __VA_ARGS__ ); } while(0)
#define DEBUG( ... ) do { if (!quiet) fprintf(stderr, "DEBUG: " __VA_ARGS__ ); } while(0)
#define DEBUG2( ... ) do { if (!quiet) fprintf(stderr, __VA_ARGS__ ); } while(0)
#define INFO( ... )  do { if (!quiet) fprintf(stderr, "INFO: " __VA_ARGS__ ); } while(0)
#define WARNING( ... )  do { fprintf(stderr, "WARNING: " __VA_ARGS__ ); } while(0)
#define ERROR( ... ) do { fprintf(stderr, "ERROR: " __VA_ARGS__ ); sleep(1); } while (0)
#define PPD( ... ) do { fprintf(stderr, "PPD: " __VA_ARGS__ ); sleep(1); } while (0)

#if (__BYTE_ORDER == __LITTLE_ENDIAN)
#define le64_to_cpu(__x) __x
#define le32_to_cpu(__x) __x
#define le16_to_cpu(__x) __x
#define be16_to_cpu(__x) ntohs(__x)
#define be32_to_cpu(__x) ntohl(__x)
#define be64_to_cpu(__x) ((__uint64_t)(                         \
        (((__uint64_t)(__x) & (__uint64_t)0x00000000000000ffULL) << 56) |   \
        (((__uint64_t)(__x) & (__uint64_t)0x000000000000ff00ULL) << 40) |   \
        (((__uint64_t)(__x) & (__uint64_t)0x0000000000ff0000ULL) << 24) |   \
        (((__uint64_t)(__x) & (__uint64_t)0x00000000ff000000ULL) <<  8) |   \
        (((__uint64_t)(__x) & (__uint64_t)0x000000ff00000000ULL) >>  8) |   \
        (((__uint64_t)(__x) & (__uint64_t)0x0000ff0000000000ULL) >> 24) |   \
        (((__uint64_t)(__x) & (__uint64_t)0x00ff000000000000ULL) >> 40) |   \
        (((__uint64_t)(__x) & (__uint64_t)0xff00000000000000ULL) >> 56)))
#else
#define le64_to_cpu(__x) ((__uint64_t)(                         \
        (((__uint64_t)(__x) & (__uint64_t)0x00000000000000ffULL) << 56) |   \
        (((__uint64_t)(__x) & (__uint64_t)0x000000000000ff00ULL) << 40) |   \
        (((__uint64_t)(__x) & (__uint64_t)0x0000000000ff0000ULL) << 24) |   \
        (((__uint64_t)(__x) & (__uint64_t)0x00000000ff000000ULL) <<  8) |   \
        (((__uint64_t)(__x) & (__uint64_t)0x000000ff00000000ULL) >>  8) |   \
        (((__uint64_t)(__x) & (__uint64_t)0x0000ff0000000000ULL) >> 24) |   \
        (((__uint64_t)(__x) & (__uint64_t)0x00ff000000000000ULL) >> 40) |   \
        (((__uint64_t)(__x) & (__uint64_t)0xff00000000000000ULL) >> 56)))
#define le32_to_cpu(x)							\
	({								\
		uint32_t __x = (x);					\
		((uint32_t)(						\
			(((uint32_t)(__x) & (uint32_t)0x000000ffUL) << 24) | \
			(((uint32_t)(__x) & (uint32_t)0x0000ff00UL) <<  8) | \
			(((uint32_t)(__x) & (uint32_t)0x00ff0000UL) >>  8) | \
			(((uint32_t)(__x) & (uint32_t)0xff000000UL) >> 24) )); \
	})
#define le16_to_cpu(x)							\
	({								\
		uint16_t __x = (x);					\
		((uint16_t)(						\
			(((uint16_t)(__x) & (uint16_t)0x00ff) <<  8) | \
			(((uint16_t)(__x) & (uint16_t)0xff00) >>  8))); \
	})
#define be64_to_cpu(__x) __x
#define be32_to_cpu(__x) __x
#define be16_to_cpu(__x) __x
#endif

#define cpu_to_le16 le16_to_cpu
#define cpu_to_le32 le32_to_cpu
#define cpu_to_le64 le64_to_cpu
#define cpu_to_be16 be16_to_cpu
#define cpu_to_be32 be32_to_cpu
#define cpu_to_be64 be64_to_cpu

/* To cheat the compiler */
#define UNUSED(expr) do { (void)(expr); } while (0)

/* To enumerate supported devices */
enum {
	P_UNKNOWN = 0,
	P_CITIZEN_CW01,
	P_CITIZEN_OP900II,
	P_CP_XXX,
	P_CP10,
	P_CP790,
	P_CP900,
	P_CP910,
	P_DNP_DS40,
	P_DNP_DS80,
	P_DNP_DS80D,
	P_DNP_DS620,
	P_DNP_DS820,
	P_DNP_DSRX1,
	P_ES1,
	P_ES2_20,
	P_ES3_30,
	P_ES40,
	P_FUJI_ASK300,
	P_HITI_51X,
	P_HITI_52X,
	P_HITI_720,
	P_HITI_750,
	P_KODAK_1400_805,
	P_KODAK_305,
	P_KODAK_605,
	P_KODAK_6800,
	P_KODAK_6850,
	P_KODAK_6900,
	P_KODAK_7000,
	P_KODAK_701X,
	P_KODAK_8810,
	P_MAGICARD,
	P_MITSU_9550,
	P_MITSU_9550S,
	P_MITSU_9600,
	P_MITSU_9800,
	P_MITSU_9800S,
	P_MITSU_9810,
	P_MITSU_D70X,
	P_MITSU_D80,
	P_MITSU_D90,
	P_MITSU_K60,
	P_MITSU_P93D,
	P_MITSU_P95D,
	P_SHINKO_S1245,
	P_SHINKO_S2145,
	P_SHINKO_S2245,
	P_SHINKO_S6145,
	P_SHINKO_S6145D,
	P_SHINKO_S6245,
	P_SONY_UPCR10,
	P_SONY_UPCR20L,
	P_SONY_UPD895,
	P_SONY_UPD897,
	P_SONY_UPD898,
	P_SONY_UPDR150,
	P_SONY_UPDR80,
	P_END,
};

struct device_id {
	uint16_t vid;
	uint16_t pid;
	int type;  /* P_** */
	const char *manuf_str;
	const char *prefix;
};

struct marker {
	const char *color;  /* Eg "#00FFFF" */
	const char *name;   /* Eg "CK9015 (4x6)" */
	int levelmax; /* Max media count, eg '600', or '-1' */
	int levelnow; /* Remaining media, -3, -2, -1, 0..N.  See CUPS. */
	int numtype; /* Numerical type, (-1 for unknown) */
};

#define BACKEND_FLAG_JOBLIST 0x00000001

/* Backend Functions */
struct dyesub_backend {
	const char *name;
	const char *version;
	const char **uri_prefixes;
	const uint32_t flags;
	void (*cmdline_usage)(void);  /* Optional */
	void *(*init)(void);
	int  (*attach)(void *ctx, struct libusb_device_handle *dev, int type,
		       uint8_t endp_up, uint8_t endp_down, uint8_t jobid);
	void (*teardown)(void *ctx);
	int  (*cmdline_arg)(void *ctx, int argc, char **argv);
	int  (*read_parse)(void *ctx, const void **job, int data_fd, int copies);
	void (*cleanup_job)(const void *job);
	int  (*main_loop)(void *ctx, const void *job);
	int  (*query_serno)(struct libusb_device_handle *dev, uint8_t endp_up, uint8_t endp_down, char *buf, int buf_len); /* Optional */
	int  (*query_markers)(void *ctx, struct marker **markers, int *count);
	const struct device_id devices[];
};

#define DYESUB_MAX_JOB_ENTRIES 2

struct dyesub_joblist {
	// TODO: mutex/lock
	const struct dyesub_backend *backend;
	void *ctx;
	int num_entries;
	int copies;
	const void *entries[DYESUB_MAX_JOB_ENTRIES];
};

/* Exported functions */
int send_data(struct libusb_device_handle *dev, uint8_t endp,
	      const uint8_t *buf, int len);
int read_data(struct libusb_device_handle *dev, uint8_t endp,
	      uint8_t *buf, int buflen, int *readlen);

void dump_markers(const struct marker *markers, int marker_count, int full);

void print_license_blurb(void);
void print_help(const char *argv0, const struct dyesub_backend *backend);

int dyesub_read_file(const char *filename, void *databuf, int datalen,
		     int *actual_len);

uint16_t uint16_to_packed_bcd(uint16_t val);
uint32_t packed_bcd_to_uint32(const char *in, int len);

void generic_teardown(void *vctx);

/* USB enumeration and attachment */
#define NUM_CLAIM_ATTEMPTS 10
int backend_claim_interface(struct libusb_device_handle *dev, int iface,
			    int num_claim_attempts);

/* Job list manipulation */
struct dyesub_joblist *dyesub_joblist_create(const struct dyesub_backend *backend, void *ctx);
int dyesub_joblist_addjob(struct dyesub_joblist *list, const void *job);
void dyesub_joblist_cleanup(const struct dyesub_joblist *list);
int dyesub_joblist_print(const struct dyesub_joblist *list);

/* Global data */
extern int terminate;
extern int dyesub_debug;
extern int fast_return;
extern int extra_vid;
extern int extra_pid;
extern int extra_type;
extern int ncopies;
extern int collate;
extern int test_mode;
extern int quiet;

enum {
	TEST_MODE_NONE = 0,
	TEST_MODE_NOPRINT,
	TEST_MODE_NOATTACH,
	TEST_MODE_MAX,
};

#if defined(BACKEND)
extern struct dyesub_backend BACKEND;
#endif

/* CUPS compatibility */
#define CUPS_BACKEND_OK            0 /* Success */
#define CUPS_BACKEND_FAILED        1 /* Failed to print use CUPS policy */
#define CUPS_BACKEND_AUTH_REQUIRED 2 /* Auth required */
#define CUPS_BACKEND_HOLD          3 /* Hold this job only */
#define CUPS_BACKEND_STOP          4 /* Stop the entire queue */
#define CUPS_BACKEND_CANCEL        5 /* Cancel print job */
#define CUPS_BACKEND_RETRY         6 /* Retry later */
#define CUPS_BACKEND_RETRY_CURRENT 7 /* Retry immediately */

/* Argument processing */
#define GETOPT_LIST_GLOBAL "d:DfGhv"
#define GETOPT_PROCESS_GLOBAL \
			case 'd': \
				ncopies = atoi(optarg); \
				break; \
			case 'D': \
				dyesub_debug++; \
				break; \
			case 'f': \
				fast_return++; \
				break; \
			case 'G': \
				print_license_blurb(); \
				exit(0); \
			case 'h': \
				print_help(argv[0], &BACKEND); \
				exit(0); \
			case 'v': \
				quiet++; \
				break;

#endif /* __BACKEND_COMMON_H */
