/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Virtual PCB wire protocol - THE shared contract.
 *
 * This header is compiled into BOTH the Zephyr-side bottom layer and the
 * standalone host processes. It must therefore depend on nothing but
 * <stdint.h>: no Zephyr headers, no host headers. If that ever stops being
 * true, the process boundary has leaked.
 */

#ifndef VPCB_PROTO_H_
#define VPCB_PROTO_H_

#include <stdint.h>

#define VPCB_PROTO_VERSION 1u
#define VPCB_MAX_PAYLOAD   1024u
#define VPCB_NAME_LEN      24u

/* Message types */
enum vpcb_msg_type {
	VPCB_MSG_HELLO     = 1, /* peer  -> board : announce role/address   */
	VPCB_MSG_HELLO_ACK = 2, /* board -> peer                            */
	VPCB_MSG_I2C_XFER  = 3, /* mcu   -> board -> ic                     */
	VPCB_MSG_I2C_REPLY = 4, /* ic    -> board -> mcu                    */
	VPCB_MSG_NET_SET   = 5, /* ic    -> board : drive a net             */
	VPCB_MSG_NET_GET   = 6, /* mcu   -> board : sample a net            */
	VPCB_MSG_NET_VALUE = 7, /* board -> mcu                             */
};

enum vpcb_role {
	VPCB_ROLE_MCU = 1,
	VPCB_ROLE_IC  = 2,
};

/* Transaction status. NAK is what a real bus reports for an absent chip. */
enum vpcb_status {
	VPCB_OK       = 0,
	VPCB_NAK      = 1, /* no device acknowledged this address */
	VPCB_ETIMEOUT = 2,
	VPCB_EPROTO   = 3,
	VPCB_ENONET   = 4, /* net not present in the netlist */
};

/* Every message is this header followed by `len` bytes of payload. */
struct vpcb_hdr {
	uint32_t len;
	uint16_t type;
	uint16_t seq;
};

struct vpcb_hello {
	uint32_t version;
	uint16_t role;
	uint16_t i2c_addr; /* 7-bit, meaningful only for VPCB_ROLE_IC */
	char     name[VPCB_NAME_LEN];
};

/* I2C is synchronous at the transaction level: one request, one reply. */
struct vpcb_i2c_xfer {
	uint16_t addr;
	uint16_t rlen;   /* bytes expected back; 0 for a pure write */
	uint16_t wlen;
	uint16_t _pad;
	uint8_t  wdata[];
};

struct vpcb_i2c_reply {
	uint16_t status;
	uint16_t rlen;
	uint8_t  rdata[];
};

struct vpcb_net_set {
	uint32_t net;
	int32_t  microvolts;
};

struct vpcb_net_get {
	uint32_t net;
};

struct vpcb_net_value {
	uint32_t net;
	int32_t  microvolts;
	uint16_t status;
	uint16_t _pad;
};

static inline const char *vpcb_status_str(uint16_t s)
{
	switch (s) {
	case VPCB_OK:       return "OK";
	case VPCB_NAK:      return "NAK";
	case VPCB_ETIMEOUT: return "TIMEOUT";
	case VPCB_EPROTO:   return "EPROTO";
	case VPCB_ENONET:   return "ENONET";
	default:            return "?";
	}
}

#endif /* VPCB_PROTO_H_ */
