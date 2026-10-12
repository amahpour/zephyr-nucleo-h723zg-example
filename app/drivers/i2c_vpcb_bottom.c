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
 *
 * Because it is wall clock, expiry says nothing about the modelled bus. It is
 * reported as VPCB_BOTTOM_NO_REPLY so the Zephyr half can tell a wedged harness
 * apart from a bus event rather than blaming the firmware for our crash.
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "vpcb/vpcb_io.h"
#include "i2c_vpcb_bottom.h"

static int      sock_fd = -1;
static uint16_t tx_seq;
static char     sock_path[108];
static char     my_name[VPCB_NAME_LEN];
static double   next_attach_ms;
static int      was_attached;

static double host_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

/* Open the socket and say HELLO. Returns 0 or -1, leaving sock_fd valid only
 * on success. */
static int attach(void)
{
	struct vpcb_hello he;
	struct vpcb_hdr h;
	uint8_t pl[VPCB_MAX_PAYLOAD];

	int fd = vpcb_connect(sock_path);
	if (fd < 0) { return -1; }

	memset(&he, 0, sizeof(he));
	he.version = VPCB_PROTO_VERSION;
	he.role    = VPCB_ROLE_MCU;
	snprintf(he.name, VPCB_NAME_LEN, "%s", my_name);

	if (vpcb_send(fd, VPCB_MSG_HELLO, ++tx_seq, &he, sizeof(he)) != 0 ||
	    vpcb_recv(fd, &h, pl, sizeof(pl), 2000) != 0 || h.type != VPCB_MSG_HELLO_ACK) {
		close(fd);
		return -1;
	}
	sock_fd = fd;
	return 0;
}

/* The board is gone. Drop the socket so the next call tries a fresh attach. */
static int lose_board(void)
{
	if (sock_fd >= 0) { close(sock_fd); sock_fd = -1; }
	if (was_attached) {
		fprintf(stderr, "i2c_vpcb: lost the virtual PCB at %s - board process gone, "
			"will reattach when it returns\n", sock_path);
		was_attached = 0;
	}
	next_attach_ms = host_ms() + VPCB_BOTTOM_REATTACH_MS;
	return VPCB_BOTTOM_DETACHED;
}

/* 0 when there is a live connection, VPCB_BOTTOM_DETACHED otherwise. Retries
 * are rate limited so a sampling thread polling every channel does not hammer
 * connect() hundreds of times a second while the board is down. */
static int ensure_attached(void)
{
	if (sock_fd >= 0) { return 0; }
	if (sock_path[0] == '\0' || host_ms() < next_attach_ms) { return VPCB_BOTTOM_DETACHED; }
	if (attach() != 0) {
		next_attach_ms = host_ms() + VPCB_BOTTOM_REATTACH_MS;
		return VPCB_BOTTOM_DETACHED;
	}
	fprintf(stderr, "i2c_vpcb: reattached to virtual PCB at %s\n", sock_path);
	was_attached = 1;
	return 0;
}

/* Wait for the reply to request `seq`. A request that timed out earlier - a
 * board that was stopped, then continued - still gets answered eventually,
 * and that late reply sits in the socket ahead of ours. Taking the first
 * message off the socket would hand every later caller the previous caller's
 * answer, one behind, for the rest of the run. Discard anything that is not
 * ours, within the same overall deadline.
 * Returns 0, VPCB_BOTTOM_NO_REPLY, or VPCB_BOTTOM_DETACHED. */
static int recv_reply(uint16_t type, uint16_t seq, struct vpcb_hdr *h,
		      uint8_t *pl, uint32_t maxlen, int timeout_ms)
{
	double deadline = host_ms() + timeout_ms;

	for (;;) {
		int left = (int)(deadline - host_ms());
		if (left < 0) { left = 0; }
		int rc = vpcb_recv(sock_fd, h, pl, maxlen, left);
		if (rc == 2) { return VPCB_BOTTOM_NO_REPLY; }
		if (rc != 0) { return lose_board(); }
		if (h->type == type && h->seq == seq) { return 0; }
		/* stale reply to an abandoned request - drop it and keep waiting */
	}
}

int vpcb_bottom_connect(const char *path, const char *name)
{
	snprintf(sock_path, sizeof(sock_path), "%s", path);
	snprintf(my_name, sizeof(my_name), "%s", name);

	if (attach() != 0) {
		fprintf(stderr, "i2c_vpcb: cannot attach to board at %s\n", path);
		next_attach_ms = host_ms() + VPCB_BOTTOM_REATTACH_MS;
		return -1;
	}
	was_attached = 1;
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

	if (wlen > VPCB_MAX_PAYLOAD - sizeof(*x)) { return -1; }
	if (ensure_attached() != 0) { return VPCB_BOTTOM_DETACHED; }

	x->addr = addr; x->rlen = rlen; x->wlen = wlen; x->_pad = 0;
	if (wlen) { memcpy(x->wdata, w, wlen); }

	uint16_t seq = ++tx_seq;
	if (vpcb_send(sock_fd, VPCB_MSG_I2C_XFER, seq, buf,
		      (uint32_t)(sizeof(*x) + wlen)) != 0) {
		return lose_board();
	}

	int rc = recv_reply(VPCB_MSG_I2C_REPLY, seq, &h, rp, sizeof(rp), timeout_ms);
	if (rc != 0) { return rc; }

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

	if (ensure_attached() != 0) { return VPCB_BOTTOM_DETACHED; }
	uint16_t seq = ++tx_seq;
	if (vpcb_send(sock_fd, VPCB_MSG_NET_GET, seq, &g, sizeof(g)) != 0) {
		return lose_board();
	}

	int rc = recv_reply(VPCB_MSG_NET_VALUE, seq, &h, rp, sizeof(rp), timeout_ms);
	if (rc != 0) { return rc; }

	struct vpcb_net_value *v = (struct vpcb_net_value *)rp;
	*microvolts = v->microvolts;
	return (int)v->status;
}

void vpcb_bottom_close(void)
{
	if (sock_fd >= 0) { close(sock_fd); sock_fd = -1; }
}
