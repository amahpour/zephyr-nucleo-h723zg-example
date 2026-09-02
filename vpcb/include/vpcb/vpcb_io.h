/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Framed message I/O over a SOCK_STREAM AF_UNIX socket.
 *
 * SOCK_STREAM + explicit length prefix rather than SOCK_SEQPACKET: macOS does
 * not support AF_UNIX/SOCK_SEQPACKET, and the framing costs ~20 lines.
 *
 * HOST CONTEXT ONLY. Included by the standalone processes and by the Zephyr
 * *_bottom.c file (which is compiled against host headers). Never include this
 * from Zephyr "top" code.
 */

#ifndef VPCB_IO_H_
#define VPCB_IO_H_

#include <errno.h>
#include <poll.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "vpcb/proto.h"

static inline int vpcb_write_all(int fd, const void *buf, size_t len)
{
	const uint8_t *p = (const uint8_t *)buf;
	while (len) {
		ssize_t n = write(fd, p, len);
		if (n < 0) {
			if (errno == EINTR) { continue; }
			return -1;
		}
		if (n == 0) { return -1; }
		p += n; len -= (size_t)n;
	}
	return 0;
}

/* Returns 0 on success, -1 on error, 1 on clean peer shutdown (EOF). */
static inline int vpcb_read_all(int fd, void *buf, size_t len)
{
	uint8_t *p = (uint8_t *)buf;
	while (len) {
		ssize_t n = read(fd, p, len);
		if (n < 0) {
			if (errno == EINTR) { continue; }
			return -1;
		}
		if (n == 0) { return 1; }
		p += n; len -= (size_t)n;
	}
	return 0;
}

static inline int vpcb_send(int fd, uint16_t type, uint16_t seq,
			    const void *payload, uint32_t len)
{
	struct vpcb_hdr h;
	h.len = len; h.type = type; h.seq = seq;
	if (vpcb_write_all(fd, &h, sizeof(h)) != 0) { return -1; }
	if (len && vpcb_write_all(fd, payload, len) != 0) { return -1; }
	return 0;
}

/* Wall-clock timeout. Simulated time is frozen while the MCU blocks here, so a
 * Zephyr-side timeout could never fire - the watchdog has to live at this level.
 * timeout_ms < 0 blocks indefinitely. Returns 0 ok, -1 error, 1 EOF, 2 timeout.
 */
static inline int vpcb_recv(int fd, struct vpcb_hdr *h, void *payload,
			    uint32_t maxlen, int timeout_ms)
{
	if (timeout_ms >= 0) {
		struct pollfd pfd = { .fd = fd, .events = POLLIN };
		int pr;
		do { pr = poll(&pfd, 1, timeout_ms); } while (pr < 0 && errno == EINTR);
		if (pr < 0)  { return -1; }
		if (pr == 0) { return 2; }
	}
	int r = vpcb_read_all(fd, h, sizeof(*h));
	if (r != 0) { return r; }
	if (h->len > maxlen) { return -1; }
	if (h->len) { return vpcb_read_all(fd, payload, h->len); }
	return 0;
}

static inline int vpcb_connect(const char *path)
{
	struct sockaddr_un sa;
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) { return -1; }
	memset(&sa, 0, sizeof(sa));
	sa.sun_family = AF_UNIX;
	strncpy(sa.sun_path, path, sizeof(sa.sun_path) - 1);
	if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static inline int vpcb_listen(const char *path, int backlog)
{
	struct sockaddr_un sa;
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) { return -1; }
	unlink(path);
	memset(&sa, 0, sizeof(sa));
	sa.sun_family = AF_UNIX;
	strncpy(sa.sun_path, path, sizeof(sa.sun_path) - 1);
	if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) { close(fd); return -1; }
	if (listen(fd, backlog) != 0) { close(fd); return -1; }
	return fd;
}

#endif /* VPCB_IO_H_ */
