/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * vpcb-dac7578 - behavioural model of a TI DACx578 (8-channel I2C DAC).
 *
 * Separate OS process. Links no Zephyr and no firmware; it only speaks the
 * vPCB wire protocol. Register map ported from the hardware-validated
 * DAC5578.h in kb2040-dac7578-controller, cross-checked against the TI
 * datasheet tables (command/access byte = C3:C0 | A3:A0, data left-justified).
 *
 * Transfer function follows the datasheet output stage: a resistor-string DAC
 * feeding a gain-of-two output buffer, so full scale = 2 x VREF.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include "vpcb/vpcb_io.h"

#define NCH 8

/* Command codes C3:C0 */
#define CMD_WRITE_INPUT      0x0
#define CMD_UPDATE_DAC       0x1
#define CMD_WRITE_UPDATE_ALL 0x2
#define CMD_WRITE_UPDATE_CH  0x3
#define CMD_POWER_DOWN       0x4
#define CMD_CLEAR_CODE       0x5
#define CMD_LDAC             0x6
#define CMD_RESET            0x7

#define ACCESS_ALL 0x0F

static uint16_t input_reg[NCH];
static uint16_t dac_reg[NCH];
static uint8_t  ldac_mask = 0xFF;  /* 1 = ignore LDAC pin, update immediately */
static uint8_t  pd_mode[NCH];      /* PD1:PD0, 0 = normal */
static int      resolution = 12;
static int      vref_uv    = 1650000; /* x2 buffer -> 3.3 V full scale */
static int      offset_uv;            /* datasheet: 0.5 mV typ, +/-4 mV max */
static int      gain_ppm;             /* datasheet: +/-0.15 % FSR max       */
static int      verbose = 1;
static uint16_t my_addr = 0x48;
static char     my_name[VPCB_NAME_LEN] = "dac7578@48";

static double now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static void logd(const char *fmt, ...)
{
	if (!verbose) { return; }
	va_list ap;
	fprintf(stderr, "[%-11s %10.3f] ", my_name, now_ms());
	va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
	fputc('\n', stderr); fflush(stderr);
}

/* code -> output microvolts, including the gain-of-2 buffer and error terms. */
static int32_t code_to_uv(uint16_t code, int ch)
{
	long long full = 2LL * vref_uv;               /* gain-of-two buffer */
	long long maxc = (1LL << resolution) - 1;
	long long uv   = (long long)code * full / maxc;

	if (pd_mode[ch] != 0) {
		/* 1k/100k to GND both pull to 0 V; Hi-Z is modelled as 0 V here
		 * because the board has no floating-net model yet. */
		return 0;
	}
	uv += offset_uv;
	uv += uv * gain_ppm / 1000000LL;
	if (uv < 0) { uv = 0; }
	if (uv > full) { uv = full; }
	return (int32_t)uv;
}

static void push_net(int fd, int ch)
{
	struct vpcb_net_set s = { .net = (uint32_t)ch, .microvolts = code_to_uv(dac_reg[ch], ch) };
	vpcb_send(fd, VPCB_MSG_NET_SET, 0, &s, sizeof(s));
	logd("  ch%d code=%u -> %d uV (net push)", ch, dac_reg[ch], s.microvolts);
}

static void do_reset(int fd)
{
	memset(input_reg, 0, sizeof(input_reg));
	memset(dac_reg, 0, sizeof(dac_reg));
	memset(pd_mode, 0, sizeof(pd_mode));
	ldac_mask = 0xFF;
	logd("RESET");
	for (int c = 0; c < NCH; c++) { push_net(fd, c); }
}

/* Returns a vpcb_status. */
static uint16_t handle_write(int fd, const uint8_t *w, uint16_t wlen)
{
	if (wlen < 1) { return VPCB_EPROTO; }

	uint8_t ca     = w[0];
	uint8_t cmd    = (ca >> 4) & 0x0F;
	uint8_t access = ca & 0x0F;
	uint16_t data  = 0;

	if (wlen >= 3) { data = ((uint16_t)w[1] << 8) | w[2]; }
	uint16_t code = data >> (16 - resolution);   /* left-justified */

	logd("XFER ca=0x%02X (cmd=0x%X access=0x%X) data=0x%04X code=%u",
	     ca, cmd, access, data, code);

	switch (cmd) {
	case CMD_WRITE_INPUT:
		if (access == ACCESS_ALL) {
			for (int c = 0; c < NCH; c++) { input_reg[c] = code; }
		} else if (access < NCH) {
			input_reg[access] = code;
		} else { return VPCB_EPROTO; }
		logd("  WRITE_INPUT (no output change)");
		break;

	case CMD_UPDATE_DAC:
		if (access == ACCESS_ALL) {
			for (int c = 0; c < NCH; c++) { dac_reg[c] = input_reg[c]; push_net(fd, c); }
		} else if (access < NCH) {
			dac_reg[access] = input_reg[access]; push_net(fd, access);
		} else { return VPCB_EPROTO; }
		break;

	case CMD_WRITE_UPDATE_ALL:
		if (access < NCH) { input_reg[access] = code; }
		for (int c = 0; c < NCH; c++) { dac_reg[c] = input_reg[c]; push_net(fd, c); }
		break;

	case CMD_WRITE_UPDATE_CH:
		if (access == ACCESS_ALL) {
			for (int c = 0; c < NCH; c++) {
				input_reg[c] = code; dac_reg[c] = code; push_net(fd, c);
			}
		} else if (access < NCH) {
			input_reg[access] = code;
			dac_reg[access]   = code;
			push_net(fd, access);
		} else { return VPCB_EPROTO; }
		break;

	case CMD_POWER_DOWN: {
		/* MSDB layout: X | PD1 | PD0 | H G F E D ; LSDB: C B A x... */
		uint8_t pd  = (w[1] >> 4) & 0x03;
		uint16_t msk = (uint16_t)(((w[1] & 0x1F) << 3) | ((w[2] >> 5) & 0x07));
		for (int c = 0; c < NCH; c++) {
			if (msk & (1u << c)) { pd_mode[c] = pd; push_net(fd, c); }
		}
		logd("  POWER_DOWN mode=%u mask=0x%03X", pd, msk);
		break;
	}
	case CMD_LDAC:
		ldac_mask = w[1];
		logd("  LDAC mask=0x%02X", ldac_mask);
		break;

	case CMD_CLEAR_CODE:
		logd("  CLEAR_CODE (modelled as no-op)");
		break;

	case CMD_RESET:
		do_reset(fd);
		break;

	default:
		return VPCB_EPROTO;
	}
	return VPCB_OK;
}

int main(int argc, char **argv)
{
	const char *sock = "/tmp/vpcb.sock";

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--sock") && i + 1 < argc)      { sock = argv[++i]; }
		else if (!strcmp(argv[i], "--addr") && i + 1 < argc) { my_addr = (uint16_t)strtol(argv[++i], NULL, 0); }
		else if (!strcmp(argv[i], "--vref-uv") && i + 1 < argc) { vref_uv = atoi(argv[++i]); }
		else if (!strcmp(argv[i], "--offset-uv") && i + 1 < argc) { offset_uv = atoi(argv[++i]); }
		else if (!strcmp(argv[i], "--gain-ppm") && i + 1 < argc)  { gain_ppm = atoi(argv[++i]); }
		else if (!strcmp(argv[i], "--res") && i + 1 < argc)  { resolution = atoi(argv[++i]); }
		else if (!strcmp(argv[i], "-q"))                     { verbose = 0; }
	}
	snprintf(my_name, sizeof(my_name), "dac7578@%02X", my_addr);

	int fd = vpcb_connect(sock);
	if (fd < 0) { fprintf(stderr, "%s: connect(%s): %s\n", my_name, sock, strerror(errno)); return 1; }

	struct vpcb_hello he = { .version = VPCB_PROTO_VERSION, .role = VPCB_ROLE_IC,
				 .i2c_addr = my_addr };
	snprintf(he.name, VPCB_NAME_LEN, "%s", my_name);
	vpcb_send(fd, VPCB_MSG_HELLO, 0, &he, sizeof(he));

	struct vpcb_hdr h;
	uint8_t pl[VPCB_MAX_PAYLOAD];
	if (vpcb_recv(fd, &h, pl, sizeof(pl), 2000) != 0 || h.type != VPCB_MSG_HELLO_ACK) {
		fprintf(stderr, "%s: no HELLO_ACK\n", my_name); return 1;
	}
	logd("attached: %d-bit, VREF=%d uV, full-scale=%d uV (gain-of-2 buffer)%s",
	     resolution, vref_uv, 2 * vref_uv,
	     (offset_uv || gain_ppm) ? ", error injection ON" : ", ideal");

	do_reset(fd);

	for (;;) {
		int r = vpcb_recv(fd, &h, pl, sizeof(pl), -1);
		if (r != 0) { logd("board closed the connection, exiting"); break; }
		if (h.type != VPCB_MSG_I2C_XFER) { continue; }

		struct vpcb_i2c_xfer *x = (struct vpcb_i2c_xfer *)pl;
		uint8_t  out[VPCB_MAX_PAYLOAD];
		struct vpcb_i2c_reply *rep = (struct vpcb_i2c_reply *)out;

		rep->status = handle_write(fd, x->wdata, x->wlen);
		rep->rlen   = 0;

		if (rep->status == VPCB_OK && x->rlen) {
			/* Read-back: return the addressed DAC register, left-justified. */
			uint8_t ch = x->wdata[0] & 0x0F;
			uint16_t v = (ch < NCH) ? (uint16_t)(dac_reg[ch] << (16 - resolution)) : 0;
			rep->rdata[0] = v >> 8;
			rep->rdata[1] = v & 0xFF;
			rep->rlen = (x->rlen < 2) ? x->rlen : 2;
			logd("  READBACK ch%u -> 0x%04X", ch, v);
		}
		vpcb_send(fd, VPCB_MSG_I2C_REPLY, h.seq, out,
			  (uint32_t)(sizeof(*rep) + rep->rlen));
	}
	close(fd);
	return 0;
}
