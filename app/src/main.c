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
#include <zephyr/drivers/rtc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log_ctrl.h>

#include <zephyr/net/net_if.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/conn_mgr_monitor.h>

#include <zephyr/shell/shell.h>

#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>

#include <app/usb.h>
#include <app/app_api.h>

#include <hu/hupacket.h>
#include <hu/palloc.h>

#include <net_sample_common.h>
#include <soc.h>

#include <time.h>

#define MY_PORT     				4241
#define HUP_UDP_THREAD_STACK_SIZE	(1024 - 128)
#define HUP_UART_THREAD_STACK_SIZE	(1024 - 512)

struct app_data app =
{
	.connected = false,

	.hup_uart = NULL,
	.hup_udp = NULL,
};

struct z_thread_stack_element app_stack_sect
	__aligned(Z_KERNEL_STACK_OBJ_ALIGN)
	hup_udp_stack_area[K_KERNEL_STACK_LEN(HUP_UDP_THREAD_STACK_SIZE)];
struct z_thread_stack_element app_stack_sect
	__aligned(Z_KERNEL_STACK_OBJ_ALIGN)
	hup_uart_stack_area[K_KERNEL_STACK_LEN(HUP_UART_THREAD_STACK_SIZE)];

#if CONFIG_SOC_FAMILY_STM32
#define MAGIC_VALUE 0xA500FF5A
#if CONFIG_SOC_SERIES_STM32N6X && DT_NODE_HAS_STATUS(DT_NODELABEL(backup_sram), okay)
/** Value stored in backup SRAM. */
struct backup_store __attribute__((section(STRINGIFY(BKPSRAM)))) backup;
#elif DT_HAS_COMPAT_STATUS_OKAY(st_stm32_backup_sram)
#define BACKUP_DEV_COMPAT st_stm32_backup_sram
/** Value stored in backup SRAM. */
struct backup_store __stm32_backup_sram_section backup;
#else
#undef MAGIC_VALUE
#endif
#endif

#if CONFIG_NET_CONNECTION_MANAGER
#define EVENT_MASK (NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED)

const struct device *const rtc = DEVICE_DT_GET(DT_ALIAS(rtc));

static void event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event, struct net_if *iface)
{
	ARG_UNUSED(iface);
	ARG_UNUSED(cb);

	if ((mgmt_event & EVENT_MASK) != mgmt_event)
		return;

	if (mgmt_event == NET_EVENT_L4_CONNECTED) {
		if (!app.connected) {
			LOG_INF("app event l4 connected");
			app.connected = true;
		}
		return;
	}

	if (mgmt_event == NET_EVENT_L4_DISCONNECTED) {
		if (app.connected) {
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
	if (net_if_flag_is_set(iface, NET_IF_IPV4)) {
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

#if !defined(CONFIG_BOARD_HAS_VBAT_BATTERY)
static int set_date_time(const struct device *rtc)
{
	struct tm time;
	int ret = 0;
	struct rtc_time tm = {
		.tm_year = 2026 - 1900,
		.tm_mon = 0,
		.tm_mday = 1,
		.tm_hour = 0,
		.tm_min = 0,
		.tm_sec = 0,
	};

    memset(&time, 0, sizeof(time));
    time.tm_mday = tm.tm_mday;
    time.tm_year = tm.tm_year;
    mktime(&time);

	tm.tm_wday = time.tm_wday;
	tm.tm_isdst = time.tm_isdst;
	ret = rtc_set_time(rtc, &tm);
	if (ret < 0) {
		LOG_ERR("Cannot write date time: %d", ret);
		return ret;
	}
	return ret;
}
#endif

static int get_date_time(const struct device *rtc)
{
	int ret = 0;
	struct rtc_time tm;

	ret = rtc_get_time(rtc, &tm);
	if (ret < 0) {
		LOG_ERR("Cannot read date time: %d", ret);
		return ret;
	}

	LOG_INF("RTC date and time: %04d-%02d-%02d %02d:%02d:%02d", tm.tm_year + 1900,
	       tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

	return ret;
}

int main(void)
{
	LOG_INF("Zephyr Example Application %s/0x%08x, %s, %s", APP_VERSION_STRING, APPVERSION, __DATE__ " " __TIME__, KERNEL_VERSION_EXTENDED_STRING);

#if DT_NODE_EXISTS(DT_CHOSEN(zephyr_itcm))
	LOG_INF("itcm size %d(0x%08x)"
		, DT_REG_SIZE(DT_CHOSEN(zephyr_itcm)), DT_REG_SIZE(DT_CHOSEN(zephyr_itcm)));
	LOG_INF("  start %p end %p size %d(0x%08x)"
		, &__itcm_start, &__itcm_end, (size_t)&__itcm_size, (size_t)&__itcm_size);
	LOG_INF("  data load start %p", &__itcm_load_start);
#endif
#if DT_NODE_EXISTS(DT_CHOSEN(zephyr_dtcm))
	LOG_INF("dtcm size %d(0x%08x)"
		, DT_REG_SIZE(DT_CHOSEN(zephyr_dtcm)), DT_REG_SIZE(DT_CHOSEN(zephyr_dtcm)));
	LOG_INF("  start %p end %p", &__dtcm_start, &__dtcm_end);
	LOG_INF("  bss start %p end %p", &__dtcm_bss_start, &__dtcm_bss_end);
	LOG_INF("  noinit start %p end %p", &__dtcm_noinit_start, &__dtcm_noinit_end);
	LOG_INF("  data start %p end %p", &__dtcm_data_start, &__dtcm_data_end);
	LOG_INF("  data load start %p", &__dtcm_data_load_start);
#if CONFIG_HU_PALLOC
	LOG_INF("palloc start %p end 0x%08x, "
		, &__dtcm_end
		, (size_t)&__dtcm_end + (DT_REG_SIZE(DT_CHOSEN(zephyr_dtcm))
		- ((size_t)&__dtcm_end - (size_t)&__dtcm_start)));
#endif
	palloc_init(&__dtcm_end, (void*)((size_t)&__dtcm_end + DT_REG_SIZE(DT_CHOSEN(zephyr_dtcm))
		- ((size_t)&__dtcm_end - (size_t)&__dtcm_start)));
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

#if !defined(CONFIG_BOARD_HAS_VBAT_BATTERY)
	set_date_time(rtc);
#endif
	get_date_time(rtc);

#ifdef BACKUP_DEV_COMPAT
	const struct device *const dev = DEVICE_DT_GET_ONE(BACKUP_DEV_COMPAT);

	if (!device_is_ready(dev)) {
		LOG_ERR("ERROR: BackUp SRAM device is not ready");
	} else
#endif
#ifdef MAGIC_VALUE
	{
		if (backup.magic == MAGIC_VALUE) {
			LOG_INF("Backup SRAM read: magic=0x%08x", backup.magic);
		} else {
			LOG_INF("Backup SRAM uninitialized, initializing...");
			backup.magic = MAGIC_VALUE;
		}
	}
#endif
	while (1) {
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

static void _reboot_timer_handler(struct k_timer *timer)
{
	sys_reboot(SYS_REBOOT_COLD);
}
static void _reboot(void*h, int argc, const char** argv)
{
	struct k_timer* reboot_timer;
	hupacket_ack_response(h, NULL);
	hupacket_send_buffer(h, NULL);
	log_flush();

	reboot_timer = palloc(sizeof(struct k_timer));
	if (!reboot_timer) {
		sys_reboot(SYS_REBOOT_COLD);
	} else {
		k_timer_init(reboot_timer, _reboot_timer_handler, NULL);
		k_timer_start(reboot_timer, K_MSEC(1500), K_NO_WAIT);
	}
}
DEFINE_HUP_CMD(hup_cmd_reboot, "reboot", _reboot);
