/*
 * Copyright (c) 2025 HU Inc. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT hu_stm32n6_extra_sram

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/clock_control/stm32_clock_control.h>
#include <soc.h>

#define EXTRA_SRAM_CHILD_COUNT(inst) + DT_INST_CHILD_NUM_STATUS_OKAY(inst)

#if defined(CONFIG_STM32N6_EXTRA_SRAM) && \
	(0 DT_INST_FOREACH_STATUS_OKAY(EXTRA_SRAM_CHILD_COUNT)) > 0

struct stm32n6_extra_sram_config {
	struct stm32_pclken sram_clock;
	struct stm32_pclken ramcfg_clock;
	bool venc_shared;
};

static int stm32n6_extra_sram_init(const struct device *dev)
{
	const struct stm32n6_extra_sram_config *cfg = dev->config;
	const struct device *clock = DEVICE_DT_GET(STM32_CLOCK_CONTROL_NODE);
	int rc;

	/* Never change VENCRAM ownership while the encoder is enabled. */
	if (cfg->venc_shared && !DT_NODE_HAS_STATUS(DT_NODELABEL(venc), disabled)) {
		return -EBUSY;
	}
	if (!device_is_ready(clock)) {
		return -ENODEV;
	}

	rc = clock_control_on(clock, (clock_control_subsys_t)&cfg->ramcfg_clock);
	if (rc != 0) {
		return rc;
	}
	rc = clock_control_on(clock, (clock_control_subsys_t)&cfg->sram_clock);
	if (rc != 0) {
		return rc;
	}

#if DT_NODE_HAS_STATUS(DT_NODELABEL(venc), disabled)
	if (cfg->venc_shared) {
		__HAL_RCC_SYSCFG_CLK_ENABLE();
		HAL_SYSCFG_DisableVENCRAMReserved();
		__DSB();
		if ((SYSCFG->VENCRAMCR & SYSCFG_VENCRAMCR_VENCRAM_EN) == 0U) {
			return -EIO;
		}
	}

#endif

	/* These banks have no AXISRAM2-6 power-down control or TCM ECC setup. */
	return 0;
}

#define EXTRA_SRAM_INIT(inst)                                                   \
	static const struct stm32n6_extra_sram_config config_##inst = {          \
		.sram_clock = STM32_DT_INST_CLOCK_INFO_BY_NAME(inst, sram),       \
		.ramcfg_clock = STM32_DT_INST_CLOCK_INFO_BY_NAME(inst, ramcfg),   \
		.venc_shared = DT_INST_PROP(inst, venc_shared),                   \
	};                                                                      \
	DEVICE_DT_INST_DEFINE(inst, stm32n6_extra_sram_init, NULL, NULL,           \
			      &config_##inst, PRE_KERNEL_2, 0, NULL);

#define EXTRA_SRAM_MAYBE_INIT(inst)                                             \
	COND_CODE_0(DT_INST_CHILD_NUM_STATUS_OKAY(inst), (), (EXTRA_SRAM_INIT(inst)))

DT_INST_FOREACH_STATUS_OKAY(EXTRA_SRAM_MAYBE_INIT)

#endif /* At least one enabled SRAM child. */

#if defined(CONFIG_STM32N6_SYSTEM_SRAM)

LOG_MODULE_REGISTER(stm32n6_system_sram, CONFIG_LOG_DEFAULT_LEVEL);

BUILD_ASSERT(DT_NODE_HAS_STATUS(DT_NODELABEL(npu), disabled),
	     "System SRAM must not be allocated by the NPU");
BUILD_ASSERT(DT_NODE_HAS_STATUS(DT_NODELABEL(npu_cache), disabled),
	     "CACHEAXI cannot be both system SRAM and NPU cache");
BUILD_ASSERT(DT_NODE_HAS_STATUS(DT_NODELABEL(venc), disabled),
	     "VENCRAM cannot be both system SRAM and encoder memory");
BUILD_ASSERT(!DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(venc_ram)),
	     "VENCRAM must not also be exposed as a separate heap/linker region");

static int stm32n6_system_sram_init(void)
{
	__HAL_RCC_CACHEAXI_CLK_ENABLE();
	__HAL_RCC_SYSCFG_CLK_ENABLE();

	/* Refuse an active cache rather than discard its contents. */
	if ((CACHEAXI->CR1 & CACHEAXI_CR1_EN) != 0U) {
		LOG_ERR("CACHEAXI is active; system SRAM is unavailable");
		k_panic();
	}

	__HAL_RCC_CACHEAXIRAM_MEM_CLK_ENABLE();
	__HAL_RCC_VENCRAM_MEM_CLK_ENABLE();
	__HAL_RCC_CACHEAXIRAM_MEM_CLK_SLEEP_ENABLE();
	__HAL_RCC_VENCRAM_MEM_CLK_SLEEP_ENABLE();
	HAL_SYSCFG_DisableVENCRAMReserved();
	__DSB();
	__ISB();

	if (!__HAL_RCC_CACHEAXIRAM_MEM_IS_CLK_ENABLED() ||
	    !__HAL_RCC_VENCRAM_MEM_IS_CLK_ENABLED() ||
	    (SYSCFG->VENCRAMCR & SYSCFG_VENCRAMCR_VENCRAM_EN) == 0U) {
		LOG_ERR("Cannot enable CACHEAXI/VENCRAM system SRAM");
		k_panic();
	}

	return 0;
}

/* MCUboot prepares these banks before copying/jumping to the application. */
SYS_INIT(stm32n6_system_sram_init, PRE_KERNEL_2, 0);

#endif /* CONFIG_STM32N6_SYSTEM_SRAM */
