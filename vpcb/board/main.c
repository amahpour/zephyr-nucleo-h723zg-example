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
 *   know what its pins are wired to, so the board records what each chip is
 *   driving on each of its own pins, and works out a net's voltage only when
 *   asked, by following the netlist from the net back to whatever pin feeds
 *   it. A net with no connected, driving pin is floating.
 *
 *   Because nets are derived rather than stored, the netlist is live: send
 *   SIGHUP and the board re-reads it. Cutting or restoring a trace takes
 *   effect on the next sample, with nothing restarted - which is what lets a
 *   fault matrix rewire the board dozens of times in a few seconds.
 */

#include <signal.h>
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
#define MAX_PINS  16  /* output pins per IC we keep state for */

struct peer {
	int      fd;
	uint16_t role;
	uint16_t addr;
	char     name[VPCB_NAME_LEN];
	int32_t  pin_uv[MAX_PINS];     /* what this IC drives on each output pin */
	uint8_t  pin_driven[MAX_PINS]; /* ...and whether it has driven it at all */
};

struct link { uint16_t addr; uint16_t chan; uint32_t net; };

static struct peer peers[MAX_PEERS];
static int         n_peers;
static struct link links[MAX_LINKS];
static int         n_links;
static int         verbose = 1;
/* Register read-backs change nothing and the firmware checks each DAC every
 * 100 ms, so they are only logged with -v. Writes and failures always are. */
static int         trace_reads;
static const char *netlist_path;
static volatile sig_atomic_t reload_requested;

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

static void on_sighup(int sig)
{
	(void)sig;
	reload_requested = 1;
}

static void reload_netlist(void)
{
	reload_requested = 0;
	if (!netlist_path) {
		logd("SIGHUP ignored: started without --netlist");
		return;
	}
	if (load_netlist(netlist_path) != 0) {
		logd("SIGHUP: cannot read %s, keeping the old wiring", netlist_path);
		return;
	}
	logd("netlist reloaded from %s (%d links)", netlist_path, n_links);
}

/* The voltage on a net, found by following the netlist back to whichever
 * connected IC pin feeds it. Returns 0 and fills *uv, or -1 if nothing
 * connected is driving the net. */
static int net_value(uint32_t net, int32_t *uv)
{
	for (int i = 0; i < n_links; i++) {
		if (links[i].net != net || links[i].chan >= MAX_PINS) { continue; }
		struct peer *ic = find_ic(links[i].addr);
		if (ic && ic->pin_driven[links[i].chan]) {
			*uv = ic->pin_uv[links[i].chan];
			return 0;
		}
	}
	return -1;
}

static void handle_net_set(struct peer *p, uint8_t *pl)
{
	struct vpcb_net_set *s = (struct vpcb_net_set *)pl;
	uint32_t net;

	/* Record the pin whatever it is wired to: a later rewire may connect it. */
	if (s->net < MAX_PINS) {
		p->pin_uv[s->net] = s->microvolts;
		p->pin_driven[s->net] = 1;
	}
	if (resolve_net(p->addr, (uint16_t)s->net, &net) != 0) {
		logd("NET_SET %s ch%u -> not in netlist, dropped", p->name, s->net);
		return;
	}
	logd("NET_SET %s ch%u -> net%u = %d uV", p->name, s->net, net, s->microvolts);
}

/* A chip that is gone - unpowered, unplugged, its process killed - drives
 * nothing. Its nets go back to floating rather than holding the last voltage
 * it set, which no real trace would do once its driver disappeared. */
static void release_nets(struct peer *p)
{
	for (int i = 0; i < n_links; i++) {
		if (links[i].addr == p->addr && links[i].chan < MAX_PINS &&
		    p->pin_driven[links[i].chan]) {
			logd("  net%u released - %s no longer drives it", links[i].net, p->name);
		}
	}
	memset(p->pin_driven, 0, sizeof(p->pin_driven));
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
	const int quiet = x->wlen <= 1 && x->rlen > 0 && !trace_reads;
	if (!ic) {
		struct vpcb_i2c_reply r = { .status = VPCB_NAK, .rlen = 0 };
		if (!quiet) {
			logd("I2C addr=0x%02X wlen=%u rlen=%u -> NAK (no IC at this address)",
			     x->addr, x->wlen, x->rlen);
		}
		vpcb_send(mcu->fd, VPCB_MSG_I2C_REPLY, seq, &r, sizeof(r));
		return;
	}

	if (!quiet) {
		logd("I2C addr=0x%02X wlen=%u rlen=%u -> %s", x->addr, x->wlen, x->rlen, ic->name);
		hexdump("W", x->wdata, x->wlen);
	}

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
			if (rh.seq == seq) { break; }
			/* A late reply to a transfer we already timed out on - the IC
			 * was stopped and has since resumed. Relaying it would hand the
			 * MCU an answer to the wrong question. */
			logd("  dropping stale reply seq=%u from %s (want %u)", rh.seq, ic->name, seq);
			continue;
		}
		logd("  ignoring msg type %u from %s while awaiting reply", rh.type, ic->name);
	}
	struct vpcb_i2c_reply *rep = (struct vpcb_i2c_reply *)rbuf;
	if (!quiet) {
		logd("  reply status=%s rlen=%u", vpcb_status_str(rep->status), rep->rlen);
		if (rep->rlen) { hexdump("R", rep->rdata, rep->rlen); }
	}
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
		else if (!strcmp(argv[i], "-v"))                        { trace_reads = 1; }
	}

	netlist_path = nl;
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

	/* No SA_RESTART: SIGHUP must interrupt poll() so the reload happens now,
	 * not whenever the next message arrives. */
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_sighup;
	sigaction(SIGHUP, &sa, NULL);

	for (;;) {
		struct pollfd pfds[MAX_PEERS + 1];
		int map[MAX_PEERS + 1], np = 0;

		pfds[np].fd = lfd; pfds[np].events = POLLIN; map[np] = -1; np++;
		for (int i = 0; i < n_peers; i++) {
			if (peers[i].fd < 0) { continue; }
			pfds[np].fd = peers[i].fd; pfds[np].events = POLLIN; map[np] = i; np++;
		}

		if (reload_requested) { reload_netlist(); }
		if (poll(pfds, np, -1) < 0) { if (errno == EINTR) continue; break; }

		for (int k = 0; k < np; k++) {
			if (!(pfds[k].revents & (POLLIN | POLLHUP))) { continue; }

			if (map[k] < 0) { /* new connection */
				int cfd = vpcb_accept(lfd);
				if (cfd < 0) { continue; }
				/* Reuse a slot freed by a disconnect before growing the table.
				 * Peers come and go - an IC cycled for an intermittent fault
				 * reconnects every time - so never reusing slots would run out
				 * of MAX_PEERS after a handful of cycles and start refusing
				 * chips that are perfectly healthy. */
				int slot = -1;
				for (int i = 0; i < n_peers; i++) {
					if (peers[i].fd < 0) { slot = i; break; }
				}
				if (slot < 0 && n_peers < MAX_PEERS) { slot = n_peers++; }
				if (slot < 0) {
					logd("peer table full (%d), refusing connection", MAX_PEERS);
					close(cfd);
					continue;
				}
				peers[slot].fd = cfd;
				peers[slot].role = 0;
				peers[slot].addr = 0;
				memset(peers[slot].pin_driven, 0, sizeof(peers[slot].pin_driven));
				snprintf(peers[slot].name, VPCB_NAME_LEN, "peer%d", slot);
				logd("peer connected (fd=%d)", cfd);
				continue;
			}

			struct peer *p = &peers[map[k]];
			struct vpcb_hdr h;
			uint8_t pl[VPCB_MAX_PAYLOAD];
			int r = vpcb_recv(p->fd, &h, pl, sizeof(pl), -1);
			if (r != 0) {
				logd("peer %s disconnected%s", p->name,
				     p->role == VPCB_ROLE_IC ? " (its address now NAKs)" : "");
				if (p->role == VPCB_ROLE_IC) { release_nets(p); }
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
				if (g->net < MAX_NETS && net_value(g->net, &v.microvolts) == 0) {
					v.status = VPCB_OK;
				} else if (g->net < MAX_NETS) {
					/* Undriven net: floating. There is no voltage to
					 * report, so say that instead of inventing 0 V. */
					v.status = VPCB_UNDRIVEN;
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
