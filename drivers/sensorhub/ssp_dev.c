/*
 *  Copyright (C) 2015, Samsung Electronics Co. Ltd. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/of_gpio.h>
#include "ssp_dev.h"
#include "ssp.h"
#include "ssp_debug.h"
#include "ssp_sysfs.h"
#include "ssp_iio.h"
#include "ssp_sensorlist.h"
#include "ssp_data.h"
#include "ssp_comm.h"
#include "ssp_scontext.h"
#include "ssp_cmd_define.h"
#include "ssp_platform.h"
#include "ssp_dump.h"
#include "ssp_injection.h"

#define NORMAL_SENSOR_STATE_K   0x3FEFF

static void init_sensorlist(struct ssp_data *data)
{
	struct sensor_info sensorinfo[SENSOR_TYPE_MAX] = {
		SENSOR_INFO_UNKNOWN,
		SENSOR_INFO_ACCELEROMETER,
		SENSOR_INFO_GEOMAGNETIC_FIELD,
		SENSOR_INFO_UNKNOWN,
		SENSOR_INFO_GYRO,
		SENSOR_INFO_LIGHT,
		SENSOR_INFO_PRESSURE,
		SENSOR_INFO_UNKNOWN,
		SENSOR_INFO_PROXIMITY,
		SENSOR_INFO_UNKNOWN,
		SENSOR_INFO_UNKNOWN,
		SENSOR_INFO_ROTATION_VECTOR,
		SENSOR_INFO_UNKNOWN,
		SENSOR_INFO_UNKNOWN,
		SENSOR_INFO_MAGNETIC_FIELD_UNCALIBRATED,
		SENSOR_INFO_UNKNOWN,
		SENSOR_INFO_GYRO_UNCALIBRATED,
		SENSOR_INFO_SIGNIFICANT_MOTION,
		SENSOR_INFO_STEP_DETECTOR,
		SENSOR_INFO_STEP_COUNTER,
		SENSOR_INFO_GEOMAGNETIC_ROTATION_VECTOR,
		SENSOR_INFO_UNKNOWN,
		SENSOR_INFO_TILT_DETECTOR,
		SENSOR_INFO_UNKNOWN,
		SENSOR_INFO_UNKNOWN,
		SENSOR_INFO_PICK_UP_GESTURE,
		SENSOR_INFO_UNKNOWN,
		SENSOR_INFO_UNKNOWN,
		SENSOR_INFO_UNKNOWN,
		SENSOR_INFO_PROXIMITY_RAW,
		SENSOR_INFO_GEOMAGNETIC_POWER,
		SENSOR_INFO_INTERRUPT_GYRO,
		SENSOR_INFO_SCONTEXT,
		SENSOR_INFO_UNKNOWN,
		SENSOR_INFO_LIGHT_CCT,
		SENSOR_INFO_CALL_GESTURE,
		SENSOR_INFO_WAKE_UP_MOTION,
		SENSOR_INFO_AUTO_BIRGHTNESS,
		SENSOR_INFO_VDIS_GYRO,
	};

	memcpy(&data->info, sensorinfo, sizeof(data->info));
}

static void initialize_variable(struct ssp_data *data)
{
	int type;
	int light_coef[7] = {-947, -425, -1777, 1754, 3588, 1112, 1370};

	ssp_infof("");

	init_sensorlist(data);

	for (type = 0; type < SENSOR_TYPE_MAX; type++) {
		data->delay[type].sampling_period = DEFUALT_POLLING_DELAY;
		data->delay[type].max_report_latency = 0;
		data->sensor_status[type] = INITIALIZATION_STATE;
		data->is_data_reported[type] = false;
	}

	data->sensor_probe_state = NORMAL_SENSOR_STATE_K;
	data->is_reset_from_kernel = false;
	data->is_reset_started = false;

	data->cnt_reset = -1;
	data->cnt_no_event_reset = 0;
	data->last_resume_status = SCONTEXT_AP_STATUS_RESUME;

	INIT_LIST_HEAD(&data->pending_list);

	data->is_refresh_done = false;
	data->dump_index = 0;
	
#ifdef CONFIG_SENSORS_SSP_LIGHT
	memcpy(data->light_coef, light_coef, sizeof(light_coef));
	data->camera_lux_en = false;
	data->brightness = -1;
#endif

#ifdef CONFIG_SENSORS_SSP_MAGNETIC
	data->geomag_cntl_regdata = 1;
	data->is_geomag_raw_enabled = false;
#endif
}


int initialize_magnetic_sensor(struct ssp_data *data)
{
	int ret = 0;

	/* STATUS AK09916C doesn't need FuseRomdata more*/
	data->uFuseRomData[0] = 0;
	data->uFuseRomData[1] = 0;
	data->uFuseRomData[2] = 0;

	ret = set_pdc_matrix(data);
	if (ret < 0)
		pr_err("[SSP] %s - set_magnetic_pdc_matrix failed %d\n",
		       __func__, ret);


	return ret < 0 ? ret : SUCCESS;
}

void initialize_mcu(struct ssp_data *data)
{
	int ret = 0;

	//ssp_dbgf();
	ssp_infof();

	clean_pending_list(data);

	ssp_infof("is_working = %d", is_sensorhub_working(data));

	ret = get_sensor_scanning_info(data);
	if (ret < 0) {
		ssp_errf("get_sensor_scanning_info failed");
	}

#ifdef CONFIG_SENSORS_SSP_ACCELOMETER
	select_accel_ops(data);
#endif
#ifdef CONFIG_SENSORS_SSP_GYROSCOPE
	select_gyro_ops(data);
#endif
#ifdef CONFIG_SENSORS_SSP_MAGNETIC
	select_magnetic_ops(data);
#endif
#ifdef CONFIG_SENSORS_SSP_BAROMETER
	select_barometer_ops(data);
#endif
#ifdef CONFIG_SENSORS_SSP_LIGHT
	select_light_ops(data);
#endif
#ifdef CONFIG_SENSORS_SSP_PROXIMITY
	select_prox_ops(data);
#endif

	ret = set_sensor_position(data);
	if (ret < 0) {
		ssp_errf("set_sensor_position failed");
	}
	
#ifdef CONFIG_SENSORS_SSP_GYROSCOPE
	gyro_open_calibration(data);
	ret = set_gyro_cal(data);
	if (ret < 0) {
		ssp_errf("set_gyro_cal failed\n");
	}
#endif

#ifdef CONFIG_SENSORS_SSP_ACCELOMETER
	ret = set_accel_cal(data);
	if (ret < 0) {
		ssp_errf("set_accel_cal failed\n");
	}
#endif

#ifdef CONFIG_SENSORS_SSP_LIGHT
	set_light_coef(data);
	set_light_brightness(data);
	set_light_ab_camera_hysteresis(data);
#endif

#ifdef CONFIG_SENSORS_SSP_PROXIMITY
#ifdef CONFIG_SENSROS_SSP_PROXIMITY_THRESH_CAL
	set_proximity_threshold_addval(data);
	if(data->cnt_reset == 0) {
		do_proximity_calibration(data);
	} else {
		set_proximity_threshold(data);
	}
#else
	set_proximity_threshold(data);
#endif
#endif

#ifdef CONFIG_SENSORS_SSP_MAGNETIC
	ret = initialize_magnetic_sensor(data);
	if (ret < 0) {
		ssp_errf("initialize magnetic sensor failed");
	}

	mag_open_calibration(data);
	ret = set_mag_cal(data);
	if (ret < 0) {
		ssp_errf("set_mag_cal failed\n");
	}

#endif

	data->curr_fw_rev = get_firmware_rev(data);
	ssp_info("MCU Firm Rev : New = %8u", data->curr_fw_rev);

	ssp_send_status(data, data->last_resume_status);
}

static void sync_sensor_state(struct ssp_data *data)
{
	u8 buf[8] = {0,};
	u32 uSensorCnt;

	udelay(10);

	for (uSensorCnt = 0; uSensorCnt < SENSOR_TYPE_MAX; uSensorCnt++) {
		mutex_lock(&data->enable_mutex);
		if (atomic64_read(&data->sensor_en_state) & (1ULL << uSensorCnt)) {
			memcpy(&buf[0], &data->delay[uSensorCnt].sampling_period, 4);
			memcpy(&buf[4], &data->delay[uSensorCnt].max_report_latency, 4);
			make_command(data, ADD_SENSOR, uSensorCnt, buf, 8);
			udelay(10);
		}
		mutex_unlock(&data->enable_mutex);
	}
}

void refresh_task(struct work_struct *work)
{
	struct ssp_data *data = container_of((struct delayed_work *)work,
	                                     struct ssp_data, work_refresh);

	ssp_dbgf("REFESH TASK");

	if (!is_sensorhub_working(data)) {
		ssp_errf("ssp is not working");
		return;
	}

	wake_lock(&data->ssp_wake_lock);
	ssp_infof();
	data->cnt_reset++;
	clean_pending_list(data);

	data->cnt_timeout = 0;
	data->cnt_com_fail = 0;
	data->is_refresh_done = true;

	initialize_mcu(data);	
	sync_sensor_state(data);
	report_scontext_notice_data(data, SCONTEXT_AP_STATUS_RESET);
	if (data->last_ap_status != 0) {
		ssp_send_status(data, data->last_ap_status);
	}
	enable_timestamp_sync_timer(data);
	data->is_reset_started = false;
	wake_unlock(&data->ssp_wake_lock);
}

int queue_refresh_task(struct ssp_data *data, int delay)
{
	cancel_delayed_work_sync(&data->work_refresh);

	ssp_dbgf();
	data->is_refresh_done = false;
	queue_delayed_work(data->debug_wq, &data->work_refresh,
	                   msecs_to_jiffies(delay));
	return SUCCESS;
}

void power_on_task(struct work_struct *work)
{
	struct ssp_data *data = container_of((struct delayed_work *)work,
	                                     struct ssp_data, work_power_on);

	ssp_errf("power_on_task is called");
	sensorhub_power_on(data);
	queue_refresh_task(data, 0);
}

static int ssp_parse_dt(struct device *dev, struct ssp_data *data)
{
	struct device_node *np = dev->of_node;

	/* sensor positions */
#ifdef CONFIG_SENSORS_SSP_ACCELOMETER
	if (of_property_read_u32(np, "ssp-acc-position", &data->accel_position)) {
		data->accel_position = 0;
	}

	ssp_info("acc-posi[%d]", data->accel_position);

#endif

#ifdef CONFIG_SENSORS_SSP_GYROSCOPE
	if (of_property_read_u32(np, "ssp-gyro-position", &data->gyro_position)) {
	
#ifdef CONFIG_SENSORS_SSP_ACCELOMETER
		data->gyro_position = data->accel_position;
#else
		data->gyro_position = 0;
#endif
	}
	ssp_info("gyro-posi[%d]", data->gyro_position);

#endif

#ifdef CONFIG_SENSORS_SSP_MAGNETIC
	if (of_property_read_u32(np, "ssp-mag-position", &data->mag_position)) {
		data->mag_position = 0;
	}

	ssp_info("mag-posi[%d]", data->mag_position);

#endif

#ifdef CONFIG_SENSORS_SSP_PROXIMITY
#if defined(CONFIG_SENSORS_SSP_PROXIMITY_AUTO_CAL_TMD3725)
	/* prox thresh */
	if (of_property_read_u8_array(np, "ssp-prox-thresh",
	                              data->prox_thresh, PROX_THRESH_SIZE)) {
		pr_err("no prox-thresh, set as 0");
	}

	ssp_info("prox-thresh - %u, %u, %u, %u", data->prox_thresh[PROX_THRESH_HIGH],
	         data->prox_thresh[PROX_THRESH_LOW], 
	         data->prox_thresh[PROX_THRESH_DETECT_HIGH], data->prox_thresh[PROX_THRESH_DETECT_LOW]);
#else
	/* prox thresh */
	if (of_property_read_u16_array(np, "ssp-prox-thresh",
	                              data->prox_thresh, PROX_THRESH_SIZE)) {
		pr_err("no prox-thresh, set as 0");
	}

	ssp_info("prox-thresh - %u, %u", data->prox_thresh[PROX_THRESH_HIGH],
	         data->prox_thresh[PROX_THRESH_LOW]);
#endif

#if defined(CONFIG_SENSROS_SSP_PROXIMITY_THRESH_CAL)
	/* prox thresh additional value */
	if (of_property_read_u16_array(np, "ssp-prox-thresh-addval", data->prox_thresh_addval, 
		sizeof(data->prox_thresh_addval)/sizeof(data->prox_thresh_addval[0]))) {
		pr_err("no prox-thresh_addval, set as 0");
	}

	ssp_info("prox-thresh-addval - %u, %u", data->prox_thresh_addval[PROX_THRESH_HIGH],
	         data->prox_thresh_addval[PROX_THRESH_LOW]);
#endif
#endif

#ifdef CONFIG_SENSORS_SSP_LIGHT
	if (of_property_read_u32_array(np, "ssp-light-position",
	                              data->light_position, sizeof(data->light_position)/sizeof(data->light_position[0]))) {
    	pr_err("no ssp-light-position, set as 0");

	}

	ssp_info("light-position - %u.%u %u.%u %u.%u",
	              data->light_position[0], data->light_position[1],
	              data->light_position[2], data->light_position[3],
	              data->light_position[4], data->light_position[5]);

	if (of_property_read_u32_array(np, "ssp-light-cam-lux",
	                              data->camera_lux_hysteresis, sizeof(data->camera_lux_hysteresis)/sizeof(data->camera_lux_hysteresis[0]))) {
    	pr_err("no ssp-light-cam-high");
		data->camera_lux_hysteresis[0] = -1;
		data->camera_lux_hysteresis[1] = 0;
	}

	if (of_property_read_u32_array(np, "ssp-light-cam-br",
	                              data->camera_br_hysteresis, sizeof(data->camera_br_hysteresis)/sizeof(data->camera_br_hysteresis[0]))) {
    	pr_err("no ssp-light-cam-low");
		data->camera_br_hysteresis[0] = 10000;
		data->camera_br_hysteresis[1] = 0;
	}

#endif

#ifdef CONFIG_SENSORS_SSP_MAGNETIC
	/* mag matrix */
	if (of_property_read_u8_array(np, "ssp-mag-array",
	                              data->pdc_matrix, sizeof(data->pdc_matrix))) {
		pr_err("no mag-array, set as 0");
	}

	/* check nfc/mst for mag matrix*/
	{
		int check_mst_gpio, check_nfc_gpio;
		int value_mst = 0, value_nfc = 0;
		
		check_nfc_gpio = of_get_named_gpio_flags(np, "mag-check-nfc", 0, NULL);
		if(check_nfc_gpio >= 0) {
			value_nfc = gpio_get_value(check_nfc_gpio);
		}
		
		check_mst_gpio = of_get_named_gpio_flags(np, "mag-check-mst", 0, NULL);
		if(check_mst_gpio >= 0) {
			value_mst = gpio_get_value(check_mst_gpio);
		}

		if(value_mst == 1) 
		{
			ssp_info("mag matrix(%d %d) nfc/mst array", value_nfc, value_mst);
			if (of_property_read_u8_array(np, "ssp-mag-mst-array",
			                              data->pdc_matrix, sizeof(data->pdc_matrix))) {
				pr_err("no mag-mst-array");
			}
		}
		else if(value_nfc == 1)
		{
			ssp_info("mag matrix(%d %d) nfc only array", value_nfc, value_mst);
			if (of_property_read_u8_array(np, "ssp-mag-nfc-array",
			                              data->pdc_matrix, sizeof(data->pdc_matrix))) {
				pr_err("no mag-nfc-array");
			}
		}
	}	
#endif
	return 0;
}

struct ssp_data* ssp_probe(struct device *dev)
{
	int ret = 0;
	struct ssp_data *data;
	
	ssp_infof();
	data = kzalloc(sizeof(struct ssp_data), GFP_KERNEL);

	data->is_probe_done = false;

	if(dev->of_node) {
		ret = ssp_parse_dt(dev, data);
		if (ret) {
			ssp_errf("failed to parse dt");
			goto err_setup;
		}
	} else {
		ssp_errf("failed to get device node");
		ret = -ENODEV;
		goto err_setup;
	}

	mutex_init(&data->comm_mutex);
	mutex_init(&data->pending_mutex);
	mutex_init(&data->enable_mutex);

	pr_info("\n#####################################################\n");
	
	INIT_DELAYED_WORK(&data->work_refresh, refresh_task);
	INIT_DELAYED_WORK(&data->work_power_on, power_on_task);

	wake_lock_init(&data->ssp_wake_lock,
	               WAKE_LOCK_SUSPEND, "ssp_wake_lock");

	initialize_variable(data);

	ret = initialize_indio_dev(dev, data);
	if (ret < 0) {
		ssp_errf("could not create input device");
		goto err_input_register_device;
	}

	ret = initialize_debug_timer(data);
	if (ret < 0) {
		ssp_errf("could not create workqueue");
		goto err_create_workqueue;
	}

	ret = initialize_timestamp_sync_timer(data);
	if (ret < 0) {
		ssp_errf("could not create ts_sync workqueue");
		goto err_create_ts_sync_workqueue;
	}

	ret = initialize_sysfs(data);
	if (ret < 0) {
		ssp_errf("could not create sysfs");
		goto err_sysfs_create;
	}

	ret = ssp_scontext_initialize(data);
	if (ret < 0) {
		ssp_errf("ssp_scontext_initialize err(%d)", ret);
		ssp_scontext_remove(data);
		goto err_init_scontext;
	}

	ret = ssp_injection_initialize(data);
	if (ret < 0) {
		ssp_errf("ssp_injection_initialize err(%d)", ret);
		ssp_injection_remove(data);
		goto err_init_injection;
	}
	
	data->is_probe_done = true;

	enable_debug_timer(data);
	schedule_delayed_work(&data->work_power_on, msecs_to_jiffies(7000));
	ssp_infof("probe success!");
	goto exit;

	//ssp_injection_remove(data);
err_init_injection:
	ssp_scontext_remove(data);
err_init_scontext:
	remove_sysfs(data);
err_sysfs_create:	
	destroy_workqueue(data->debug_wq);
err_create_ts_sync_workqueue:
	destroy_workqueue(data->ts_sync_wq);
err_create_workqueue:
	remove_indio_dev(data);
err_input_register_device:
	wake_lock_destroy(&data->ssp_wake_lock);
	mutex_destroy(&data->comm_mutex);
	mutex_destroy(&data->pending_mutex);
	mutex_destroy(&data->enable_mutex);
err_setup:
	kfree(data);
	data = ERR_PTR(ret);
	ssp_errf("probe failed!");
	
exit:
	pr_info("#####################################################\n\n");
	return data;
}

void ssp_remove(struct ssp_data *data)
{
	ssp_infof();
	if (data->is_probe_done == false) {
		goto exit;
	}

	disable_debug_timer(data);
	disable_timestamp_sync_timer(data);
#if 0
	if (SUCCESS != ssp_send_status(data, SCONTEXT_AP_STATUS_SHUTDOWN)) {
		ssp_errf("SCONTEXT_AP_STATUS_SHUTDOWN failed");
	}
#endif
	clean_pending_list(data);

	remove_sysfs(data);
	ssp_injection_remove(data);
	ssp_scontext_remove(data);

	del_timer_sync(&data->debug_timer);
	cancel_work_sync(&data->work_debug);
	destroy_workqueue(data->debug_wq);
	del_timer_sync(&data->ts_sync_timer);
	cancel_work_sync(&data->work_ts_sync);
	destroy_workqueue(data->ts_sync_wq);
	cancel_delayed_work_sync(&data->work_refresh);
	wake_lock_destroy(&data->ssp_wake_lock);
	mutex_destroy(&data->comm_mutex);
	mutex_destroy(&data->pending_mutex);
	mutex_destroy(&data->enable_mutex);
#if 0 			/* Yum : Not yet */	
	toggle_mcu_reset(data);
#endif
	cancel_delayed_work_sync(&data->work_power_on);
	ssp_infof("done");
exit:
	kfree(data);
}

void ssp_timestamp_resume(struct ssp_data *data)
{
	int type;
	for (type = 0; type < SENSOR_TYPE_MAX; type++) {
		data->latest_timestamp[type] = 0;
	}
}

/* this callback is called before suspend */
int ssp_suspend(struct ssp_data *data)
{
	ssp_infof();

	disable_debug_timer(data);
	disable_timestamp_sync_timer(data);

	data->last_resume_status = SCONTEXT_AP_STATUS_SUSPEND;
	//enable_irq_wake(data->irq);

//	if (SUCCESS != ssp_send_status(data, SCONTEXT_AP_STATUS_SUSPEND)) {
//		ssp_errf("SCONTEXT_AP_STATUS_SUSPEND failed");
//	}

	return 0;
}

/* this callback is called after resume */
void ssp_resume(struct ssp_data *data)
{
	ssp_infof();

	enable_debug_timer(data);
	enable_timestamp_sync_timer(data);
	//disable_irq_wake(data->irq);
	ssp_timestamp_resume(data);

//	if (SUCCESS != ssp_send_status(data, SCONTEXT_AP_STATUS_RESUME)) {
//		ssp_errf("SCONTEXT_AP_STATUS_RESUME failed");
//	}

	data->last_resume_status = SCONTEXT_AP_STATUS_RESUME;
	return;
}
