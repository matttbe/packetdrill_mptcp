/*
 * Copyright 2013 Google Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */
/*
 * Author: ncardwell@google.com (Neal Cardwell)
 *
 * Interfaces for reading and writing TCP options in their wire format.
 */

#ifndef __TCP_OPTIONS_H__
#define __TCP_OPTIONS_H__

#include <asm/byteorder.h>
#include "types.h"
#include "packet.h"

#define MAX_TCP_OPTION_BYTES (MAX_TCP_HEADER_BYTES - (int)sizeof(struct tcp))

/* TCP Fast Open uses the following magic number to be after the
 * option value for sharing TCP experimental options.
 *
 * For a description of experimental options, see:
 *   http://tools.ietf.org/html/draft-ietf-tcpm-experimental-options-00
 *
 * For a description of TFO, see:
 *   http://tools.ietf.org/html/draft-cheng-tcpm-fastopen-02
 */
#define TCPOPT_FASTOPEN_MAGIC	0xF989

/* TFO option must have: 1-byte kind, 1-byte length, and 2-byte magic: */
#define TCPOLEN_EXP_FASTOPEN_BASE 4	/* smallest legal TFO option size */

/* The TFO option base prefix leaves this amount of space: */
#define MAX_TCP_FAST_OPEN_COOKIE_BYTES				\
	(MAX_TCP_OPTION_BYTES - TCPOLEN_EXP_FASTOPEN_BASE)

/* Represents a list of TCP options in their wire format. */
struct tcp_options {
	u8 data[MAX_TCP_OPTION_BYTES];	/* The options data, in wire format */
	u8 length;		/* The length, in bytes, of the data */
};

/* Specification of a TCP SACK block (RFC 2018) */
struct sack_block {
	u32 left;   /* left edge: 1st sequence number in block */
	u32 right;  /* right edge: 1st sequence number just past block */
};

/* part of mptcp dss option structures */
struct dack {
	union {
		u32 dack4;
		u64 dack8;
	};
};

struct dsn {
	union {
		u32 dsn4;
		u64 dsn8;
	};
	union {
		struct {
			u32 ssn; //subflow sequence number
			u16 dll; //data level length
		} wo_cs;
		struct {
			u32 ssn; //subflow sequence number
			u16 dll; //data level length, TODO endianness
			u16 checksum;
		} w_cs;
	};
};


/* Represents a single TCP option in its wire format. Note that for
 * EOL and NOP options the length and data field are not included in
 * the on-the-wire data. For other options, the length field describes
 * the number of bytes of the struct that go on the wire. */
struct tcp_option {
	u8 kind;
	u8 length;  /* bytes on the wire; includes kind and length byte */
	union {
		struct {
			u16 bytes;	/* in network order */
		} mss;
		struct {
			u32 val;	/* in network order */
			u32 ecr;	/* in network order */
		} time_stamp;
		struct {
			u8 shift_count;
		} window_scale;
		struct {
			/* actual number of blocks will be 1..4 */
			struct sack_block block[4];
		} sack;
		struct {
			u16 magic;	/* must be TCPOPT_FASTOPEN_MAGIC */
			/* The fast open chookie should be 4-16 bytes
			 * of cookie, multiple of 2 bytes, but we
			 * allow for larger sizes, so we can test what
			 * stacks do with illegal options.
			 */
			u8 cookie[MAX_TCP_FAST_OPEN_COOKIE_BYTES];
		} fast_open;

		/*******MPTCP options*********/

		struct {
			#if defined(__LITTLE_ENDIAN_BITFIELD)
			u8 version:4;
			u8 subtype:4;
			u8 flags;
			#elif defined(__BIG_ENDIAN_BITFIELD)
			u8 subtype:4;
			u8 version:4;
			u8 flags;
			#else
			#error "Adjust your <asm/byteorder.h> defines"
			#endif
			union {
				struct {
					u64 key;
				} syn;
				struct {
					u64 sender_key;
					u64 receiver_key;
				} no_syn;
			};
		} __packed mp_capable;

		struct {
			union {
				struct {
					#if defined(__LITTLE_ENDIAN_BITFIELD)
					__u8    flags:4,
					subtype:4;
					#elif defined(__BIG_ENDIAN_BITFIELD)
					__u8    subtype:4,
					flags:4;
					#else
					#error "Adjust your <asm/byteorder.h> defines"
					#endif
					u8 address_id;
					union {
						struct {
							u64 sender_hmac;
							u32 sender_random_number;
						} ack;
						struct {
							u32 receiver_token;
							u32 sender_random_number;
						} no_ack;
					};
				} __packed syn;
				struct {
					#if defined(__LITTLE_ENDIAN_BITFIELD)
					__u16    reserved:12, subtype:4;
					#elif defined(__BIG_ENDIAN_BITFIELD)
					__u16    subtype:4,	 reserved:12;
					#else
					#error "Adjust your <asm/byteorder.h> defines"
					#endif
			//		u8 reserved_last_bits; //TODO find better solution
					u32 sender_hmac[5];
				} __packed no_syn;
			}; 
		} __packed mp_join;

		struct {

			#if defined(__LITTLE_ENDIAN_BITFIELD)
			__u8    reserved_first_bits:4, subtype:4;
			__u8 	flag_A:1, // Data ACK present
					flag_a:1, // Data ACK is 8 octets (if not set,4 octets)
					flag_M:1, // DSN, SSN, DLL, CHCK is set
					flag_m:1, // Data sequence number is 8 octets (4 otherwie)
					flag_F:1, // Data Fin is present
					reserved_last_bits:3;
			#elif defined(__BIG_ENDIAN_BITFIELD)
			__u8    subtype:4, reserved_first_bits:4;
			__u8	reserved_last_bits:3,
			//flags: 5;
					flag_F:1, // Data Fin is present
					flag_m:1, // Data sequence number is 8 octets (4 otherwie)
					flag_M:1, // DSN, SSN, DLL, CHCK is set
					flag_a:1, // Data ACK is 8 octets (if not set,4 octets)
					flag_A:1; // Data ACK present
			#else
			#error "Adjust your <asm/byteorder.h> defines"
			#endif

			/*		Data Sequence Signal (DSS) Option
							1               2                3
			0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
			+---------------+---------------+-------+----------------------+
			|     Kind      |    Length     |Subtype| (reserved) |F|m|M|a|A|
			+---------------+---------------+-------+----------------------+
			|           Data ACK (4 or 8 octets, depending on flags)       |
			+--------------------------------------------------------------+
			|   Data sequence number (4 or 8 octets, depending on flags)   |
			+--------------------------------------------------------------+
			|              Subflow Sequence Number (4 octets)              |
			+-------------------------------+------------------------------+
			|  Data-Level Length (2 octets) |      Checksum (2 octets)     |
			+-------------------------------+------------------------------+
			*/
			union {
				struct dack dack;
				struct dsn dsn;
				struct {
					struct dack dack;
					struct dsn dsn;
				} __packed dack_dsn;
			};
		} __packed dss;
		/*******END MPTCP options*********/
	} __packed data;
} __packed tcp_option;

/* Allocate a new options list. */
extern struct tcp_options *tcp_options_new(void);

/* Allocate a new option and initialize its kind and length fields. */
extern struct tcp_option *tcp_option_new(u8 kind, u8 length);

/* Appends the given option to the given list of options. Returns
 * STATUS_OK on success; on failure returns STATUS_ERR and sets
 * error message.
 */
extern int tcp_options_append(struct tcp_options *options,
			      struct tcp_option *option);

/* Calculate the number of SACK blocks in a SACK option of the given
 * length and store it in *num_blocks. Returns STATUS_OK on success;
 * on failure returns STATUS_ERR and sets error message.
 */
extern int num_sack_blocks(u8 opt_len, int *num_blocks, char **error);

#endif /* __TCP_OPTIONS_H__ */
