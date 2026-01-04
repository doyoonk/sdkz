/*
 * Copyright (c) 2016 Intel Corporation
 * Copyright (c) 2020 Nordic Semiconductor ASA
 * Copyright (c) 2025 HU Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file Sample app to demonstrate PWM.
 */

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(app, CONFIG_APP_LOG_LEVEL);

#include "main.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/led.h>

#include <stdint.h>
#include <stdbool.h>

#if CONFIG_LED_PWM

#define MIN_SCALE	50U

static const struct pwm_dt_spec pwm_led = PWM_DT_SPEC_GET(DT_ALIAS(pwm_led0));

static void start_pwmleds(void*, void*, void*)
{
	uint64_t cycle_per_sec;
	uint32_t max_period;
	uint32_t period;
	uint8_t dir = 0U;
	int ret;

	LOG_INF("PWM-based blinky");

	if (!pwm_is_ready_dt(&pwm_led)) {
		LOG_ERR("Error: PWM device %s is not ready", pwm_led.dev->name);
		return;
	}

	/*
	 * In case the default MAX_PERIOD value cannot be set for
	 * some PWM hardware, decrease its value until it can.
	 *
	 * Keep its value at least pwm_led.period / MIN_SCALE * 4 to make sure
	 * the sample changes frequency at least once.
	 */
	pwm_get_cycles_per_sec(pwm_led.dev, 1, &cycle_per_sec);

	LOG_INF("%s Cycles per sec %lld, Calibrating for channel %d period %d..."
		, pwm_led.dev->name, cycle_per_sec, pwm_led.channel, pwm_led.period);

	max_period = pwm_led.period;
	while (pwm_set_dt(&pwm_led, max_period, max_period / 2U)) {
		max_period /= 2U;
		if (max_period < (4U * (pwm_led.period / MIN_SCALE))) {
			LOG_ERR("Error: PWM device does not support a period at least %u", 4U * (pwm_led.period / MIN_SCALE));
			return;
		}
	}
	LOG_INF("Done calibrating; maximum/minimum periods %u/%u nsec", max_period, (pwm_led.period / MIN_SCALE));

	period = max_period;
	while (1) {
		ret = pwm_set_dt(&pwm_led, period, period / 2U);
		if (ret) {
			LOG_ERR("Error %d: failed to set pulse width", ret);
			return;
		}
		LOG_DBG("Using period %d", period);

		period = dir ? (period * 2U) : (period / 2U);
		if (period > max_period) {
			period = max_period / 2U;
			dir = 0U;
		} else if (period < (pwm_led.period / MIN_SCALE)) {
			period = (pwm_led.period / MIN_SCALE) * 2U;
			dir = 1U;
		}

		k_sleep(K_SECONDS(4U));
	}
}

static struct z_thread_stack_element app_stack_sect
	__aligned(Z_KERNEL_STACK_OBJ_ALIGN)
	_k_thread_stack_pwmleds[K_KERNEL_STACK_LEN(768)];
Z_THREAD_COMMON_DEFINE(pwmleds, 768, start_pwmleds, NULL, NULL, NULL, 7, 0, 0);
#endif
