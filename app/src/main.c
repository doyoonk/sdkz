/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 * Copyright (c) 2025 HU Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app, CONFIG_APP_LOG_LEVEL);

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/shell/shell.h>

#include <zephyr/net/net_if.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/conn_mgr_monitor.h>

#include <zephyr/version.h>

#include <app/drivers/blink.h>

#include <app_version.h>
#include <app/usb.h>

#include <huerrno.h>
#include <hu/hupacket.h>
#include <hu/palloc.h>

#include <net_sample_common.h>
#include <zephyr/drivers/bbram.h>

#define BLINK_PERIOD_MS_STEP 100U
#define BLINK_PERIOD_MS_MAX  1000U

extern int init_bbram();
static bool connected;

#if CONFIG_NET_CONNECTION_MANAGER
static struct net_mgmt_event_callback mgmt_cb = { 0 };
#define EVENT_MASK (NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED)

static void event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event, struct net_if *iface)
{
	ARG_UNUSED(iface);
	ARG_UNUSED(cb);

	if ((mgmt_event & EVENT_MASK) != mgmt_event) {
		return;
	}

	if (mgmt_event == NET_EVENT_L4_CONNECTED) {
		connected = true;
		return;
	}

	if (mgmt_event == NET_EVENT_L4_DISCONNECTED) {
		connected = false;
		return;
	}
}
#endif

static int init_app(void)
{
#if CONFIG_NET_IPV4
	struct net_if* iface = net_if_get_default();

	if (net_if_flag_is_set(iface, NET_IF_IPV4))
	{
#if CONFIG_NET_CONNECTION_MANAGER
		net_mgmt_init_event_callback(&mgmt_cb, event_handler, EVENT_MASK);
		net_mgmt_add_event_callback(&mgmt_cb);
		conn_mgr_mon_resend_status();
#endif
		init_vlan();
		init_tunnel();
	}
#endif

	init_bbram();

	return init_usb();
}

static void deinit_app()
{
	net_mgmt_del_event_callback(&mgmt_cb);
}

int main(void)
{
	int ret;
	unsigned int period_ms = BLINK_PERIOD_MS_MAX;
	const struct device *sensor, *blink;
	struct sensor_value last_val = { 0 }, val;

	LOG_INF("Zephyr Example Application %s/0x%08x", APP_VERSION_STRING, APPVERSION);

 #if DT_NODE_EXISTS(DT_CHOSEN(zephyr_ccm))
	LOG_INF("ccm size %d: start 0x%08x, data end 0x%08x"
		, DT_REG_SIZE(DT_CHOSEN(zephyr_ccm))
		, (size_t)__ccm_start, (size_t)__ccm_data_end);
	LOG_INF("palloc start 0x%08x end 0x%08x"
		, (size_t)__ccm_start
		, (size_t)__ccm_start + DT_REG_SIZE(DT_CHOSEN(zephyr_ccm)));
	palloc_init(__ccm_start, __ccm_start + DT_REG_SIZE(DT_CHOSEN(zephyr_ccm)));
 #endif

	init_app();

	sensor = DEVICE_DT_GET(DT_NODELABEL(example_sensor));
	if (!device_is_ready(sensor)) {
		LOG_ERR("Sensor not ready");
		goto exit_main;
	}

	blink = DEVICE_DT_GET(DT_NODELABEL(blink_led));
	if (!device_is_ready(blink)) {
		LOG_ERR("Blink LED not ready");
		goto exit_main;
	}

	LOG_INF("Use the sensor to change LED blinking period");
	blink_set_period_ms(blink, period_ms);

	while (1) {
		ret = sensor_sample_fetch(sensor);
		if (ret < 0) {
			LOG_ERR("Could not fetch sample (%d)", ret);
			goto exit_main;
		}

		ret = sensor_channel_get(sensor, SENSOR_CHAN_PROX, &val);
		if (ret < 0) {
			LOG_ERR("Could not get sample (%d)", ret);
			goto exit_main;
		}

		if ((last_val.val1 == 0) && (val.val1 == 1)) {
			if (period_ms == 0U) {
				period_ms = BLINK_PERIOD_MS_MAX;
			} else {
				period_ms -= BLINK_PERIOD_MS_STEP;
			}

			LOG_INF("Proximity detected, setting LED period to %u ms", period_ms);
			blink_set_period_ms(blink, period_ms);
		}

		last_val = val;

		k_sleep(K_MSEC(100));
	}

exit_main:
	deinit_app();
	return 0;
}

static void _ver(void* h, int argc, const char** argv)
{
	hupacket_ack_response(h, NULL);
	hupacket_record_int(h, NULL, NOERROR);
	hupacket_record_str(h, NULL, APP_VERSION_EXTENDED_STRING);
	hupacket_record_str(h, NULL, KERNEL_VERSION_STRING);
	hupacket_record_str(h, NULL, __DATE__ " " __TIME__);
	hupacket_send_buffer(h, NULL);
}
DEFINE_HUP_CMD(hup_cmd_ver, "ver", _ver);
