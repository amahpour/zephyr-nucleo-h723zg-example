/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host-side half of the virtual-PCB I2C driver.
 *
 * Compiled into the native_simulator (host) context, NOT the Zephyr context,
 * so it may use host sockets. It must not include any Zephyr header.
 *
 * Note the timeouts here are WALL CLOCK. While the MCU blocks on this socket,
 * native_sim's simulated time is frozen, so a Zephyr-side timeout could never
 * fire. The watchdog has to live at this level or a dead IC hangs the sim.
 */

#include <stdio.h>
#include <string.h>

#include "vpcb/vpcb_io.h"
#include "i2c_vpcb_bottom.h"

static int      sock_fd = -1;
static uint16_t tx_seq;

int vpcb_bottom_connect(const char *path, const char *name)
{
	struct vpcb_hello he;
	struct vpcb_hdr h;
	uint8_t pl[VPCB_MAX_PAYLOAD];

	sock_fd = vpcb_connect(path);
	if (sock_fd < 0) {
		fprintf(stderr, "i2c_vpcb: cannot connect to board at %s\n", path);
		return -1;
	}

	memset(&he, 0, sizeof(he));
	he.version = VPCB_PROTO_VERSION;
	he.role    = VPCB_ROLE_MCU;
	snprintf(he.name, VPCB_NAME_LEN, "%s", name);

	if (vpcb_send(sock_fd, VPCB_MSG_HELLO, ++tx_seq, &he, sizeof(he)) != 0) { return -1; }
	if (vpcb_recv(sock_fd, &h, pl, sizeof(pl), 2000) != 0 || h.type != VPCB_MSG_HELLO_ACK) {
		fprintf(stderr, "i2c_vpcb: no HELLO_ACK from board\n");
		return -1;
	}
	fprintf(stderr, "i2c_vpcb: attached to virtual PCB at %s\n", path);
	return 0;
}

int vpcb_bottom_i2c(uint16_t addr, const uint8_t *w, uint16_t wlen,
		    uint8_t *r, uint16_t rlen, int timeout_ms)
{
	uint8_t buf[VPCB_MAX_PAYLOAD];
	uint8_t rp[VPCB_MAX_PAYLOAD];
	struct vpcb_i2c_xfer *x = (struct vpcb_i2c_xfer *)buf;
	struct vpcb_hdr h;

	if (sock_fd < 0) { return -1; }
	if (wlen > VPCB_MAX_PAYLOAD - sizeof(*x)) { return -1; }

	x->addr = addr; x->rlen = rlen; x->wlen = wlen; x->_pad = 0;
	if (wlen) { memcpy(x->wdata, w, wlen); }

	if (vpcb_send(sock_fd, VPCB_MSG_I2C_XFER, ++tx_seq, buf,
		      (uint32_t)(sizeof(*x) + wlen)) != 0) {
		return -1;
	}

	int rc = vpcb_recv(sock_fd, &h, rp, sizeof(rp), timeout_ms);
	if (rc == 2) { return VPCB_ETIMEOUT; }
	if (rc != 0 || h.type != VPCB_MSG_I2C_REPLY) { return -1; }

	struct vpcb_i2c_reply *rep = (struct vpcb_i2c_reply *)rp;
	if (rep->status == VPCB_OK && rlen && r) {
		uint16_t n = rep->rlen < rlen ? rep->rlen : rlen;
		memcpy(r, rep->rdata, n);
	}
	return (int)rep->status;
}

int vpcb_bottom_net_get(uint32_t net, int32_t *microvolts, int timeout_ms)
{
	struct vpcb_net_get g = { .net = net };
	struct vpcb_hdr h;
	uint8_t rp[VPCB_MAX_PAYLOAD];

	if (sock_fd < 0) { return -1; }
	if (vpcb_send(sock_fd, VPCB_MSG_NET_GET, ++tx_seq, &g, sizeof(g)) != 0) { return -1; }

	int rc = vpcb_recv(sock_fd, &h, rp, sizeof(rp), timeout_ms);
	if (rc == 2) { return VPCB_ETIMEOUT; }
	if (rc != 0 || h.type != VPCB_MSG_NET_VALUE) { return -1; }

	struct vpcb_net_value *v = (struct vpcb_net_value *)rp;
	*microvolts = v->microvolts;
	return (int)v->status;
}

void vpcb_bottom_close(void)
{
	if (sock_fd >= 0) { close(sock_fd); sock_fd = -1; }
}
