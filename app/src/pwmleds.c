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

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/led.h>

#include <stdint.h>
#include <stdbool.h>

#if CONFIG_LED_PWM

#define MIN_PERIOD PWM_SEC(2U) / 200U
#define MAX_PERIOD PWM_SEC(2U)

static const struct pwm_dt_spec pwm_leds[] = {
	PWM_DT_SPEC_GET(DT_ALIAS(pwm_led0)),
};

static void start_pwmleds(void*, void*, void*)
{
	uint64_t cycle_per_sec;
	uint32_t max_period;
	uint32_t period;
	uint8_t dir = 0U;
	int ret;

	LOG_INF("PWM-based blinky");

	for (size_t i = 0; i < ARRAY_SIZE(pwm_leds); i ++) {
		if (!pwm_is_ready_dt(&pwm_leds[i])) {
			LOG_ERR("Error: PWM device %s is not ready", pwm_leds[i].dev->name);
			return;
		}
	}

	/*
	 * In case the default MAX_PERIOD value cannot be set for
	 * some PWM hardware, decrease its value until it can.
	 *
	 * Keep its value at least MIN_PERIOD * 4 to make sure
	 * the sample changes frequency at least once.
	 */
	for (size_t i = 0; i < ARRAY_SIZE(pwm_leds); i ++) {
		pwm_get_cycles_per_sec(pwm_leds[i].dev, 1, &cycle_per_sec);

		LOG_INF("%s Cycles per sec %lld, Calibrating for channel %d...", pwm_leds[i].dev->name, cycle_per_sec, pwm_leds[i].channel);
	}

	max_period = MAX_PERIOD;
	while (pwm_set_dt(&pwm_leds[0], max_period, max_period / 2U)) {
		max_period /= 2U;
		if (max_period < (4U * MIN_PERIOD)) {
			LOG_ERR("Error: PWM device does not support a period at least %lu", 4U * MIN_PERIOD);
			return;
		}
	}
	LOG_INF("Done calibrating; maximum/minimum periods %u/%lu nsec", max_period, MIN_PERIOD);

	period = max_period;
	while (1) {
		ret = pwm_set_dt(&pwm_leds[0], period, period / 2U);
		if (ret) {
			LOG_ERR("Error %d: failed to set pulse width", ret);
			return;
		}
		LOG_DBG("Using period %d", period);

		period = dir ? (period * 2U) : (period / 2U);
		if (period > max_period) {
			period = max_period / 2U;
			dir = 0U;
		} else if (period < MIN_PERIOD) {
			period = MIN_PERIOD * 2U;
			dir = 1U;
		}

		k_sleep(K_SECONDS(5U));
	}
}
K_THREAD_DEFINE(pwmleds, 384, start_pwmleds, NULL, NULL, NULL, 5, 0, 0);

#endif
