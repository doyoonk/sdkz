/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 * Copyright (c) 2025 HU Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app, CONFIG_APP_LOG_LEVEL);

#include "main.h"
#include <app_version.h>
#include <huerrno.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/version.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/bbram.h>

#include <zephyr/net/net_if.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/shell/shell.h>

#include <zephyr/sys/reboot.h>

#include <app/usb.h>
#include <app/app_api.h>

#include <hu/hupacket.h>
#include <hu/palloc.h>

#include <net_sample_common.h>

#define MY_PORT     				4241
#define HUP_UDP_THREAD_STACK_SIZE	(1024 - 128)
#define HUP_UART_THREAD_STACK_SIZE	(1024 - 512)

struct app_data app =
{
	.connected = false,

	.hup_uart = NULL,
	.hup_udp = NULL,
};

K_THREAD_STACK_DEFINE(hup_udp_stack_area, HUP_UDP_THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(hup_uart_stack_area, HUP_UART_THREAD_STACK_SIZE);

#if !CONFIG_LED_PWM
#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

#if CONFIG_LED_TIMER
static struct k_timer blink_timer;
static void _timer_handler(struct k_timer *timer)
{
	gpio_pin_toggle_dt(&led);
}

static int init_blink_led()
{
	int ret;
	if (!gpio_is_ready_dt(&led))
	{
		LOG_ERR("blink led gpio not ready");
		return -ENXIO;
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	if (ret != 0)
	{
		LOG_ERR("blink led gpio not ready, error %d", ret);
		return ret;
	}
	k_timer_init(&blink_timer, _timer_handler, NULL);
	k_timer_start(&blink_timer, K_MSEC(1000), K_MSEC(1000));
	return 0;
}
#endif
#endif

#if CONFIG_NET_CONNECTION_MANAGER
#define EVENT_MASK (NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED)

static void event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event, struct net_if *iface)
{
	ARG_UNUSED(iface);
	ARG_UNUSED(cb);

	if ((mgmt_event & EVENT_MASK) != mgmt_event)
	{
		return;
	}

	if (mgmt_event == NET_EVENT_L4_CONNECTED)
	{
		if (!app.connected)
		{
			LOG_INF("app event l4 connected");
			app.connected = true;
		}
		return;
	}

	if (mgmt_event == NET_EVENT_L4_DISCONNECTED)
	{
		if (app.connected)
		{
			LOG_INF("app event l4 disconnected");
			app.connected = false;
		}
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
		net_mgmt_init_event_callback(&app.mgmt_cb, event_handler, EVENT_MASK);
		net_mgmt_add_event_callback(&app.mgmt_cb);
		conn_mgr_mon_resend_status();
#endif
		init_vlan();
		init_tunnel();
	}
#endif
	init_bbram();

	return init_app_usb();
}

static void deinit_app()
{
	deinit_hup_server(app.hup_uart, &uart_interrupt);
	deinit_hup_server(app.hup_udp, &udp_server);

#if CONFIG_NET_CONNECTION_MANAGER
	net_mgmt_del_event_callback(&app.mgmt_cb);
#endif
}

int main(void)
{
	LOG_INF("Zephyr Example Application %s/0x%08x, %s", APP_VERSION_STRING, APPVERSION, __DATE__ " " __TIME__);

#if !CONFIG_LED_PWM && CONFIG_LED_TIMER
	init_blink_led();
#endif

 #if DT_NODE_EXISTS(DT_CHOSEN(zephyr_dtcm))
 #if 0
 	LOG_INF("The total used CCM area   : [%p, %p)", &__dtcm_start, &__dtcm_end);
	LOG_INF("Zero initialized BSS area : [%p, %p)", &__dtcm_bss_start, &__dtcm_bss_end);
	LOG_INF("Uninitialized NOINIT area : [%p, %p)", &__dtcm_noinit_start, &__dtcm_noinit_end);
	LOG_INF("Initialised DATA area     : [%p, %p)", &__dtcm_data_start, &__dtcm_data_end);
	LOG_INF("Start of DATA in FLASH    : %p", &__dtcm_data_load_start);
#endif
	LOG_INF("ccm size %d: start %p/%p, data end %p"
		, DT_REG_SIZE(DT_CHOSEN(zephyr_dtcm))
		, &__dtcm_start, &__dtcm_end, &__dtcm_data_end);
	LOG_INF("palloc start %p end 0x%08x, "
		, &__dtcm_end
		, (size_t)&__dtcm_end + (DT_REG_SIZE(DT_CHOSEN(zephyr_dtcm)) - ((size_t)&__dtcm_end - (size_t)&__dtcm_start)) );
	palloc_init(__dtcm_start, __dtcm_start + DT_REG_SIZE(DT_CHOSEN(zephyr_dtcm)));
 #endif

	init_app();

	LOG_INF("hu packet server start for UDP port %d", MY_PORT);
	app.hup_udp = init_hup_server(&udp_server, "hup_udp"
		, hup_udp_stack_area, K_THREAD_STACK_SIZEOF(hup_udp_stack_area)
		, (void*)MY_PORT, NULL, NULL);

	LOG_INF("hu packet server start for USB cdc acm uart");
	app.hup_uart = init_hup_server(&uart_interrupt, "hup_acm"
		, hup_uart_stack_area, K_THREAD_STACK_SIZEOF(hup_uart_stack_area)
		, (void*)DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart), (void*)115200, "n81");


	while (1)
	{
#if !CONFIG_LED_PWM && !CONFIG_LED_TIMER		
		gpio_pin_toggle_dt(&led);
#endif
		k_sleep(K_MSEC(1000));
	}

	deinit_app();
	return 0;
}

static void _ver(void* h, int argc, const char** argv)
{
	hupacket_ack_response(h, NULL);
	hupacket_record_str(h, NULL, APP_VERSION_EXTENDED_STRING);
	hupacket_record_str(h, NULL, KERNEL_VERSION_STRING);
	hupacket_record_str(h, NULL, __DATE__ " " __TIME__);
	hupacket_send_buffer(h, NULL);
}
DEFINE_HUP_CMD(hup_cmd_ver, "ver", _ver);

static struct k_timer _reboot_timer;
static void _reboot_timer_handler(struct k_timer *timer)
{
	sys_reboot(SYS_REBOOT_COLD);
}
static void _reboot(void*h, int argc, const char** argv)
{
	k_timer_init(&_reboot_timer, _reboot_timer_handler, NULL);
	k_timer_start(&_reboot_timer, K_MSEC(1500), K_NO_WAIT);
	hupacket_ack_response(h, NULL);
	hupacket_send_buffer(h, NULL);
}
DEFINE_HUP_CMD(hup_cmd_reboot, "reboot", _reboot);
