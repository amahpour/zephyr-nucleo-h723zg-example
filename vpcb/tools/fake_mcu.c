/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * vpcb-fake-mcu - stands in for the firmware so the host side of the virtual
 * PCB can be exercised without building Zephyr. Same wire protocol the real
 * i2c_vpcb driver speaks.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "vpcb/vpcb_io.h"

static int fd;
static uint16_t seq;

static uint16_t i2c_write(uint16_t addr, const uint8_t *w, uint16_t wlen)
{
	uint8_t buf[VPCB_MAX_PAYLOAD];
	struct vpcb_i2c_xfer *x = (struct vpcb_i2c_xfer *)buf;
	struct vpcb_hdr h;
	uint8_t rp[VPCB_MAX_PAYLOAD];

	x->addr = addr; x->rlen = 0; x->wlen = wlen; x->_pad = 0;
	memcpy(x->wdata, w, wlen);
	vpcb_send(fd, VPCB_MSG_I2C_XFER, ++seq, buf, sizeof(*x) + wlen);

	int r = vpcb_recv(fd, &h, rp, sizeof(rp), 2000);
	if (r != 0) { printf("  [mcu] no reply (r=%d)\n", r); return VPCB_ETIMEOUT; }
	return ((struct vpcb_i2c_reply *)rp)->status;
}

static int net_get(uint32_t net, int32_t *uv)
{
	struct vpcb_net_get g = { .net = net };
	struct vpcb_hdr h;
	uint8_t rp[VPCB_MAX_PAYLOAD];
	vpcb_send(fd, VPCB_MSG_NET_GET, ++seq, &g, sizeof(g));
	if (vpcb_recv(fd, &h, rp, sizeof(rp), 2000) != 0) { return -1; }
	*uv = ((struct vpcb_net_value *)rp)->microvolts;
	return 0;
}

int main(int argc, char **argv)
{
	const char *sock = "/tmp/vpcb.sock";
	uint16_t addr = 0x48;
	int ch = 0, mv = 2000;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--sock") && i + 1 < argc) { sock = argv[++i]; }
		else if (!strcmp(argv[i], "--addr") && i + 1 < argc) { addr = strtol(argv[++i], NULL, 0); }
		else if (!strcmp(argv[i], "--ch") && i + 1 < argc) { ch = atoi(argv[++i]); }
		else if (!strcmp(argv[i], "--mv") && i + 1 < argc) { mv = atoi(argv[++i]); }
	}

	fd = vpcb_connect(sock);
	if (fd < 0) { fprintf(stderr, "fake-mcu: connect: %s\n", strerror(errno)); return 1; }

	struct vpcb_hello he = { .version = VPCB_PROTO_VERSION, .role = VPCB_ROLE_MCU };
	snprintf(he.name, VPCB_NAME_LEN, "fake-mcu");
	vpcb_send(fd, VPCB_MSG_HELLO, ++seq, &he, sizeof(he));
	struct vpcb_hdr h; uint8_t pl[VPCB_MAX_PAYLOAD];
	vpcb_recv(fd, &h, pl, sizeof(pl), 2000);

	/* 12-bit code, left-justified into MSDB/LSDB, per datasheet table_0022. */
	uint16_t code = (uint16_t)((long)mv * 4095 / 3300);
	uint16_t data = (uint16_t)(code << 4);
	uint8_t w[3] = { (uint8_t)((0x3 << 4) | (ch & 0x0F)),   /* WRITE_UPDATE_CH */
			 (uint8_t)(data >> 8), (uint8_t)(data & 0xFF) };

	printf("  [mcu] write addr=0x%02X ch%d %d mV (code=%u)\n", addr, ch, mv, code);
	uint16_t st = i2c_write(addr, w, sizeof(w));
	printf("  [mcu] i2c status = %s\n", vpcb_status_str(st));

	int32_t uv = -1;
	if (net_get((uint32_t)ch, &uv) == 0) {
		printf("  [mcu] net%d reads %d uV (%.1f mV)\n", ch, uv, uv / 1000.0);
	}
	printf("RESULT status=%s net_uv=%d\n", vpcb_status_str(st), uv);
	close(fd);
	return st == VPCB_OK ? 0 : 2;
}
