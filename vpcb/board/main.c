/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * vpcb-board - the virtual PCB.
 *
 * Owns the netlist and routes between peers. Every peer is a separate OS
 * process; this program never links any IC model or firmware.
 *
 * Routing rules:
 *   I2C_XFER from the MCU is forwarded to whichever IC claimed that 7-bit
 *   address at HELLO time. If no IC claimed it, the board answers NAK - which
 *   is exactly what a real bus does when a chip is absent, unpowered or
 *   mis-strapped.
 *
 *   NET_SET from an IC carries an IC-LOCAL channel index. A real chip does not
 *   know what its pins are wired to, so the board resolves (addr, channel)
 *   through the netlist to a board net.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include "vpcb/vpcb_io.h"

#define MAX_PEERS 16
#define MAX_NETS  64
#define MAX_LINKS 64

struct peer {
	int      fd;
	uint16_t role;
	uint16_t addr;
	char     name[VPCB_NAME_LEN];
};

struct link { uint16_t addr; uint16_t chan; uint32_t net; };

static struct peer peers[MAX_PEERS];
static int         n_peers;
static struct link links[MAX_LINKS];
static int         n_links;
static int32_t     nets[MAX_NETS];
static int         net_driven[MAX_NETS];
static int         verbose = 1;

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
	fprintf(stderr, "[board %10.3f] ", now_ms());
	va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
	fputc('\n', stderr);
	fflush(stderr);
}

static void hexdump(const char *tag, const uint8_t *d, uint16_t n)
{
	if (!verbose) { return; }
	fprintf(stderr, "[board %10.3f]   %s [%u]:", now_ms(), tag, n);
	for (uint16_t i = 0; i < n; i++) { fprintf(stderr, " %02X", d[i]); }
	fputc('\n', stderr); fflush(stderr);
}

static struct peer *find_ic(uint16_t addr)
{
	for (int i = 0; i < n_peers; i++) {
		if (peers[i].fd >= 0 && peers[i].role == VPCB_ROLE_IC &&
		    peers[i].addr == addr) {
			return &peers[i];
		}
	}
	return NULL;
}

static int resolve_net(uint16_t addr, uint16_t chan, uint32_t *net)
{
	for (int i = 0; i < n_links; i++) {
		if (links[i].addr == addr && links[i].chan == chan) {
			*net = links[i].net;
			return 0;
		}
	}
	return -1;
}

static void default_netlist(void)
{
	/* dac@0x48 ch0-7 -> nets 0-7 ; dac@0x4c ch0-7 -> nets 8-15 */
	for (uint16_t c = 0; c < 8 && n_links < MAX_LINKS; c++) {
		links[n_links++] = (struct link){ 0x48, c, c };
	}
	for (uint16_t c = 0; c < 8 && n_links < MAX_LINKS; c++) {
		links[n_links++] = (struct link){ 0x4c, c, (uint32_t)(c + 8) };
	}
}

static int load_netlist(const char *path)
{
	FILE *f = fopen(path, "r");
	char line[256];
	if (!f) { return -1; }
	n_links = 0;
	while (fgets(line, sizeof(line), f)) {
		unsigned a, c, n;
		char *h = strchr(line, '#');
		if (h) { *h = '\0'; }
		if (sscanf(line, "%i %u %u", (int *)&a, &c, &n) == 3 && n_links < MAX_LINKS) {
			links[n_links++] = (struct link){ (uint16_t)a, (uint16_t)c, n };
		}
	}
	fclose(f);
	return 0;
}

static void handle_net_set(struct peer *p, uint8_t *pl)
{
	struct vpcb_net_set *s = (struct vpcb_net_set *)pl;
	uint32_t net;

	if (resolve_net(p->addr, (uint16_t)s->net, &net) != 0) {
		logd("NET_SET %s ch%u -> not in netlist, dropped", p->name, s->net);
		return;
	}
	if (net < MAX_NETS) {
		nets[net] = s->microvolts;
		net_driven[net] = 1;
		logd("NET_SET %s ch%u -> net%u = %d uV", p->name, s->net, net, s->microvolts);
	}
}

/* Forward an MCU transfer to the owning IC and relay its reply.
 *
 * An IC drives its nets whenever it likes - the analog domain is genuinely
 * asynchronous - so NET_SET messages legitimately arrive interleaved with the
 * reply we are waiting for. Service them inline rather than mistaking the
 * first arrival for the reply.
 */
static void route_i2c(struct peer *mcu, uint16_t seq, uint8_t *pl, uint32_t len)
{
	struct vpcb_i2c_xfer *x = (struct vpcb_i2c_xfer *)pl;
	uint8_t  rbuf[VPCB_MAX_PAYLOAD];
	struct vpcb_hdr rh;

	if (len < sizeof(*x)) { return; }

	struct peer *ic = find_ic(x->addr);
	if (!ic) {
		struct vpcb_i2c_reply r = { .status = VPCB_NAK, .rlen = 0 };
		logd("I2C addr=0x%02X wlen=%u rlen=%u -> NAK (no IC at this address)",
		     x->addr, x->wlen, x->rlen);
		vpcb_send(mcu->fd, VPCB_MSG_I2C_REPLY, seq, &r, sizeof(r));
		return;
	}

	logd("I2C addr=0x%02X wlen=%u rlen=%u -> %s", x->addr, x->wlen, x->rlen, ic->name);
	hexdump("W", x->wdata, x->wlen);

	if (vpcb_send(ic->fd, VPCB_MSG_I2C_XFER, seq, pl, len) != 0) {
		struct vpcb_i2c_reply r = { .status = VPCB_NAK, .rlen = 0 };
		logd("  forward failed -> NAK");
		vpcb_send(mcu->fd, VPCB_MSG_I2C_REPLY, seq, &r, sizeof(r));
		return;
	}
	/* Drain until the reply appears; 1s is a generous host-side bound. */
	for (;;) {
		int r = vpcb_recv(ic->fd, &rh, rbuf, sizeof(rbuf), 1000);
		if (r != 0) {
			struct vpcb_i2c_reply rep = { .status = VPCB_ETIMEOUT, .rlen = 0 };
			logd("  no reply from %s (r=%d) -> TIMEOUT", ic->name, r);
			vpcb_send(mcu->fd, VPCB_MSG_I2C_REPLY, seq, &rep, sizeof(rep));
			return;
		}
		if (rh.type == VPCB_MSG_NET_SET) {
			handle_net_set(ic, rbuf);
			continue;
		}
		if (rh.type == VPCB_MSG_I2C_REPLY) {
			break;
		}
		logd("  ignoring msg type %u from %s while awaiting reply", rh.type, ic->name);
	}
	struct vpcb_i2c_reply *rep = (struct vpcb_i2c_reply *)rbuf;
	logd("  reply status=%s rlen=%u", vpcb_status_str(rep->status), rep->rlen);
	if (rep->rlen) { hexdump("R", rep->rdata, rep->rlen); }
	vpcb_send(mcu->fd, VPCB_MSG_I2C_REPLY, seq, rbuf, rh.len);
}

int main(int argc, char **argv)
{
	const char *sock = "/tmp/vpcb.sock";
	const char *nl   = NULL;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--sock") && i + 1 < argc)         { sock = argv[++i]; }
		else if (!strcmp(argv[i], "--netlist") && i + 1 < argc) { nl = argv[++i]; }
		else if (!strcmp(argv[i], "-q"))                        { verbose = 0; }
	}

	if (nl && load_netlist(nl) == 0) {
		logd("netlist loaded from %s (%d links)", nl, n_links);
	} else {
		default_netlist();
		logd("using default netlist (%d links)", n_links);
	}

	int lfd = vpcb_listen(sock, 8);
	if (lfd < 0) { fprintf(stderr, "board: listen(%s): %s\n", sock, strerror(errno)); return 1; }
	logd("listening on %s", sock);

	for (int i = 0; i < MAX_PEERS; i++) { peers[i].fd = -1; }

	for (;;) {
		struct pollfd pfds[MAX_PEERS + 1];
		int map[MAX_PEERS + 1], np = 0;

		pfds[np].fd = lfd; pfds[np].events = POLLIN; map[np] = -1; np++;
		for (int i = 0; i < n_peers; i++) {
			if (peers[i].fd < 0) { continue; }
			pfds[np].fd = peers[i].fd; pfds[np].events = POLLIN; map[np] = i; np++;
		}

		if (poll(pfds, np, -1) < 0) { if (errno == EINTR) continue; break; }

		for (int k = 0; k < np; k++) {
			if (!(pfds[k].revents & (POLLIN | POLLHUP))) { continue; }

			if (map[k] < 0) { /* new connection */
				int cfd = accept(lfd, NULL, NULL);
				if (cfd < 0) { continue; }
				if (n_peers < MAX_PEERS) {
					peers[n_peers].fd = cfd;
					peers[n_peers].role = 0;
					snprintf(peers[n_peers].name, VPCB_NAME_LEN, "peer%d", n_peers);
					n_peers++;
					logd("peer connected (fd=%d)", cfd);
				} else { close(cfd); }
				continue;
			}

			struct peer *p = &peers[map[k]];
			struct vpcb_hdr h;
			uint8_t pl[VPCB_MAX_PAYLOAD];
			int r = vpcb_recv(p->fd, &h, pl, sizeof(pl), -1);
			if (r != 0) {
				logd("peer %s disconnected%s", p->name,
				     p->role == VPCB_ROLE_IC ? " (its address now NAKs)" : "");
				close(p->fd); p->fd = -1;
				continue;
			}

			switch (h.type) {
			case VPCB_MSG_HELLO: {
				struct vpcb_hello *he = (struct vpcb_hello *)pl;
				p->role = he->role; p->addr = he->i2c_addr;
				/* name arrives from the wire; do not trust termination */
				memcpy(p->name, he->name, VPCB_NAME_LEN - 1);
				p->name[VPCB_NAME_LEN - 1] = '\0';
				logd("HELLO role=%s addr=0x%02X name=%s v%u",
				     he->role == VPCB_ROLE_MCU ? "MCU" : "IC",
				     he->i2c_addr, p->name, he->version);
				vpcb_send(p->fd, VPCB_MSG_HELLO_ACK, h.seq, NULL, 0);
				break;
			}
			case VPCB_MSG_I2C_XFER:
				route_i2c(p, h.seq, pl, h.len);
				break;
			case VPCB_MSG_NET_SET:
				handle_net_set(p, pl);
				break;
			case VPCB_MSG_NET_GET: {
				struct vpcb_net_get *g = (struct vpcb_net_get *)pl;
				struct vpcb_net_value v = { .net = g->net, .microvolts = 0,
							    .status = VPCB_ENONET };
				if (g->net < MAX_NETS && net_driven[g->net]) {
					v.microvolts = nets[g->net];
					v.status = VPCB_OK;
				} else if (g->net < MAX_NETS) {
					/* Undriven net: floating. Model as 0 V for now. */
					v.microvolts = 0;
					v.status = VPCB_OK;
				}
				vpcb_send(p->fd, VPCB_MSG_NET_VALUE, h.seq, &v, sizeof(v));
				break;
			}
			default:
				logd("unexpected msg type %u from %s", h.type, p->name);
				break;
			}
		}
	}
	return 0;
}
