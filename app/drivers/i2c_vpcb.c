/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Virtual-PCB I2C controller driver (native_sim).
 *
 * Zephyr "top" half: implements the standard I2C controller API so the rest of
 * the system is unaware the bus leaves the process. All host I/O is delegated
 * to i2c_vpcb_bottom.c, which is compiled in the host context.
 */

#define DT_DRV_COMPAT zephyr_i2c_vpcb

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <zephyr/init.h>
#include <string.h>

#include <cmdline.h>           /* native_sim command line options */
#include <posix_native_task.h>

#include "i2c_vpcb_bottom.h"
#include "i2c_vpcb.h"

LOG_MODULE_REGISTER(i2c_vpcb, CONFIG_I2C_LOG_LEVEL);

/* Mirrors enum vpcb_status from the wire protocol. */
#define VPCB_ST_OK       0
#define VPCB_ST_NAK      1
#define VPCB_ST_TIMEOUT  2

#define VPCB_XFER_TIMEOUT_MS 1000

#define VPCB_DEFAULT_SOCK "/tmp/vpcb.sock"

static char vpcb_sock_path[128] = VPCB_DEFAULT_SOCK;
static bool vpcb_ready;

/* Set by --vpcb-sock. Points into argv, so it is only read during init. */
static char *vpcb_sock_opt;

static void vpcb_add_options(void)
{
	static struct args_struct_t vpcb_options[] = {
		{
			.is_mandatory = false,
			.option = "vpcb-sock",
			.name = "path",
			.type = 's',
			.dest = (void *)&vpcb_sock_opt,
			.descript = "Unix socket of the virtual PCB board process "
				    "(default: " VPCB_DEFAULT_SOCK "). Give each "
				    "concurrent run its own path.",
		},
		ARG_TABLE_ENDMARKER,
	};

	native_add_command_line_opts(vpcb_options);
}

/* PRE_BOOT_1 runs before device init, so the path is settled before the
 * driver tries to connect.
 */
NATIVE_TASK(vpcb_add_options, PRE_BOOT_1, 11);

static int i2c_vpcb_configure(const struct device *dev, uint32_t cfg)
{
	ARG_UNUSED(dev); ARG_UNUSED(cfg);
	return 0;
}

static int i2c_vpcb_transfer(const struct device *dev, struct i2c_msg *msgs,
			     uint8_t num_msgs, uint16_t addr)
{
	uint8_t wbuf[256];
	uint16_t wlen = 0;
	uint8_t *rptr = NULL;
	uint16_t rlen = 0;

	ARG_UNUSED(dev);

	if (!vpcb_ready) {
		LOG_ERR("virtual PCB not attached");
		return -EIO;
	}

	/* Flatten: concatenate writes, allow a single trailing read. */
	for (uint8_t i = 0; i < num_msgs; i++) {
		if (msgs[i].flags & I2C_MSG_READ) {
			if (rptr) {
				LOG_ERR("only one read message supported");
				return -ENOTSUP;
			}
			rptr = msgs[i].buf;
			rlen = msgs[i].len;
		} else {
			if (rptr) {
				LOG_ERR("write after read not supported");
				return -ENOTSUP;
			}
			if (wlen + msgs[i].len > sizeof(wbuf)) { return -ENOMEM; }
			memcpy(&wbuf[wlen], msgs[i].buf, msgs[i].len);
			wlen += msgs[i].len;
		}
	}

	int st = vpcb_bottom_i2c(addr, wbuf, wlen, rptr, rlen, VPCB_XFER_TIMEOUT_MS);

	if (st == VPCB_BOTTOM_NO_REPLY) {
		/* Not a bus event. Nobody answered within the host-side deadline,
		 * which means the board or an IC process is wedged or gone. Say so
		 * plainly: -ETIMEDOUT here would let a broken harness masquerade as
		 * a chip that stretched the clock.
		 */
		LOG_ERR("no reply from the virtual PCB within %d ms of host time "
			"(addr 0x%02x) - board or IC process not responding",
			VPCB_XFER_TIMEOUT_MS, addr);
		return -EIO;
	}
	if (st < 0) {
		LOG_ERR("transport failure to virtual PCB");
		return -EIO;
	}
	switch (st) {
	case VPCB_ST_OK:
		LOG_DBG("xfer addr=0x%02x wlen=%u rlen=%u OK", addr, wlen, rlen);
		return 0;
	case VPCB_ST_NAK:
		/* No device acknowledged: exactly what a real bus reports when the
		 * chip is absent, unpowered or mis-strapped.
		 */
		LOG_ERR("addr 0x%02x NAK - no device on the virtual bus", addr);
		return -ENODEV;
	case VPCB_ST_TIMEOUT:
		/* Reported by the board as a modelled bus event, not inferred from
		 * our own silence. Unreachable from a host stall now.
		 */
		LOG_ERR("addr 0x%02x timed out on the bus", addr);
		return -ETIMEDOUT;
	default:
		LOG_ERR("addr 0x%02x bus error (status %d)", addr, st);
		return -EIO;
	}
}

int vpcb_net_read_mv(uint32_t net, int32_t *millivolts)
{
	int32_t uv;
	int st;

	if (!vpcb_ready) { return -EIO; }

	st = vpcb_bottom_net_get(net, &uv, VPCB_XFER_TIMEOUT_MS);
	if (st == VPCB_BOTTOM_NO_REPLY) {
		/* Without this the sampling thread reports a wedged board as
		 * -ENODEV, i.e. "net not in the netlist", which is a wiring fault.
		 */
		LOG_ERR("no reply from the virtual PCB within %d ms of host time "
			"(net %u) - board process not responding",
			VPCB_XFER_TIMEOUT_MS, net);
		return -EIO;
	}
	if (st < 0)  { return -EIO; }
	if (st != VPCB_ST_OK) { return -ENODEV; }

	*millivolts = uv / 1000;
	return 0;
}

const char *vpcb_sock(void) { return vpcb_sock_path; }
void vpcb_set_sock(const char *p)
{
	strncpy(vpcb_sock_path, p, sizeof(vpcb_sock_path) - 1);
	vpcb_sock_path[sizeof(vpcb_sock_path) - 1] = '\0';
}

static int i2c_vpcb_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	if (vpcb_sock_opt != NULL) {
		vpcb_set_sock(vpcb_sock_opt);
	}

	if (vpcb_bottom_connect(vpcb_sock_path, "zephyr-mcu") != 0) {
		LOG_ERR("could not attach to virtual PCB at %s", vpcb_sock_path);
		vpcb_ready = false;
		/* Not fatal: a board that is not running should look like a bus
		 * with nothing on it, not a boot failure.
		 */
		return 0;
	}
	vpcb_ready = true;
	LOG_INF("attached to virtual PCB at %s", vpcb_sock_path);
	return 0;
}

static DEVICE_API(i2c, i2c_vpcb_api) = {
	.configure = i2c_vpcb_configure,
	.transfer  = i2c_vpcb_transfer,
};

#define I2C_VPCB_INIT(n)						\
	I2C_DEVICE_DT_INST_DEFINE(n, i2c_vpcb_init, NULL, NULL, NULL,	\
				  POST_KERNEL, CONFIG_I2C_INIT_PRIORITY,\
				  &i2c_vpcb_api);

DT_INST_FOREACH_STATUS_OKAY(I2C_VPCB_INIT)
