/*
 * Copyright (c) 2012, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __MSM_THERMAL_H
#define __MSM_THERMAL_H

struct msm_thermal_data {
	uint32_t sensor_id;
	uint32_t poll_ms;
	int32_t limit_temp_degC;
	int32_t temp_hysteresis_degC;
	uint32_t freq_step;
	int32_t core_limit_temp_degC;
	int32_t core_temp_hysteresis_degC;
	uint32_t core_control_mask;
#ifdef CONFIG_MAKO_THERMAL
	uint32_t shutdown_temp;
	uint32_t allowed_max_high;
	uint32_t allowed_max_low;
	uint32_t allowed_max_freq;
	uint32_t allowed_mid_high;
	uint32_t allowed_mid_low;
	uint32_t allowed_mid_freq;
	uint32_t allowed_low_high;
	uint32_t allowed_low_low;
	uint32_t allowed_low_freq;
#endif
};

#ifdef CONFIG_INTELLI_THERMAL
struct msm_thermal_data_intelli {
	uint32_t sensor_id;
	uint32_t poll_ms;
	int32_t limit_temp_degC;
	int32_t temp_hysteresis_degC;
	uint32_t freq_step;
	uint32_t freq_control_mask;
	int32_t core_limit_temp_degC;
	int32_t core_temp_hysteresis_degC;
	uint32_t core_control_mask;
};
#endif

#ifdef CONFIG_MAKO_THERMAL
struct msm_thermal_stat {
    cputime64_t time_low_start;
    cputime64_t time_mid_start;
    cputime64_t time_max_start;
    cputime64_t time_low;
    cputime64_t time_mid;
    cputime64_t time_max;
};
#endif

#if defined(CONFIG_THERMAL_MONITOR) || defined(CONFIG_INTELLI_THERMAL) || defined(CONFIG_MAKO_THERMAL)
extern int msm_thermal_init(struct msm_thermal_data *pdata);
extern int msm_thermal_device_init(void);
#else
static inline int msm_thermal_init(struct msm_thermal_data *pdata)
{
	return -ENOSYS;
}
static inline int msm_thermal_device_init(void)
{
	return -ENOSYS;
}
#endif

#endif /*__MSM_THERMAL_H*/
