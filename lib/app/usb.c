/*
 * Copyright (c) 2019 Intel Corporation
 * Copyright (c) 2025 HU Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sample_usbd.h>

#include <stdio.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>

#include <zephyr/usb/usbd.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(acm, LOG_LEVEL_INF);

static inline void print_baudrate(const struct device *dev)
{
	uint32_t baudrate;
	int ret;

	ret = uart_line_ctrl_get(dev, UART_LINE_CTRL_BAUD_RATE, &baudrate);
	if (ret) {
		LOG_WRN("Failed to get baudrate, ret code %d", ret);
	} else {
		LOG_INF("Baudrate %u", baudrate);
	}
}

static struct usbd_context *sample_usbd;
#if CONFIG_UART_LINE_CTRL
static K_SEM_DEFINE(dtr_sem, 0, 1);
#endif

static void sample_msg_cb(struct usbd_context *const ctx, const struct usbd_msg *msg)
{
	LOG_INF("USBD message: %s", usbd_msg_type_string(msg->type));

	if (usbd_can_detect_vbus(ctx)) {
		if (msg->type == USBD_MSG_VBUS_READY) {
			if (usbd_enable(ctx)) {
				LOG_ERR("Failed to enable device support");
			}
		}

		if (msg->type == USBD_MSG_VBUS_REMOVED) {
			if (usbd_disable(ctx)) {
				LOG_ERR("Failed to disable device support");
			}
		}
	}

#if CONFIG_UART_LINE_CTRL
	if (msg->type == USBD_MSG_CDC_ACM_CONTROL_LINE_STATE) {
		uint32_t dtr = 0U;

		uart_line_ctrl_get(msg->dev, UART_LINE_CTRL_DTR, &dtr);
		if (dtr) {
			k_sem_give(&dtr_sem);
		}
	}
#endif

	if (msg->type == USBD_MSG_CDC_ACM_LINE_CODING) {
		print_baudrate(msg->dev);
	}
}

static int enable_usb_device_next(void)
{
	int err;

	sample_usbd = sample_usbd_init_device(sample_msg_cb);
	if (sample_usbd == NULL) {
		LOG_ERR("Failed to initialize USB device");
		return -ENODEV;
	}

	if (!usbd_can_detect_vbus(sample_usbd)) {
		err = usbd_enable(sample_usbd);
		if (err) {
			LOG_ERR("Failed to enable device support");
			return err;
		}
	}

	LOG_INF("USB device support enabled");

	return 0;
}

int init_app_usb(void)
{
	int ret;
	const struct device *const uart_dev = DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);

	if (!device_is_ready(uart_dev)) {
		LOG_ERR("CDC ACM device not ready");
		return 0;
	}

	ret = enable_usb_device_next();
	if (ret != 0) {
		LOG_ERR("Failed to enable USB device support");
		return 0;
	}
#if CONFIG_UART_LINE_CTRL
	LOG_INF("Wait for DTR");
	k_sem_take(&dtr_sem, K_FOREVER);
	LOG_INF("DTR set");

	/* They are optional, we use them to test the interrupt endpoint */
	ret = uart_line_ctrl_set(uart_dev, UART_LINE_CTRL_DCD, 1);
	if (ret) {
		LOG_WRN("Failed to set DCD, ret code %d", ret);
	}

	ret = uart_line_ctrl_set(uart_dev, UART_LINE_CTRL_DSR, 1);
	if (ret) {
		LOG_WRN("Failed to set DSR, ret code %d", ret);
	}
#endif
	/* Wait 100ms for the host to do all settings */
	k_msleep(500);

	return 0;
}
