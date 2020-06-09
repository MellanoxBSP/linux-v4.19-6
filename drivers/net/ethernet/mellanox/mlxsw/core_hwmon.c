// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2015-2018 Mellanox Technologies. All rights reserved */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/hwmon.h>
#include <linux/err.h>
#include <linux/sfp.h>

#include "core.h"
#include "core_env.h"

#define MLXSW_HWMON_TEMP_SENSOR_MAX_COUNT 127
#define MLXSW_HWMON_ATTR_COUNT (MLXSW_HWMON_TEMP_SENSOR_MAX_COUNT * 4 + \
				MLXSW_MFCR_TACHOS_MAX + MLXSW_MFCR_PWMS_MAX)

struct mlxsw_hwmon_gr;
struct mlxsw_hwmon;

struct mlxsw_hwmon_attr {
	struct device_attribute dev_attr;
	struct mlxsw_hwmon_gr *hwmon;
	unsigned int type_index;
	char name[32];
	u8 slot;
};

static int mlxsw_hwmon_get_attr_index(int index, int count, u8 slot, u8 base,
				      u8 max_modules, u8 max_gearbox)
{
	if (slot)
		slot--;
	if (index >= count) {
		return index % count + MLXSW_REG_MTMP_GBOX_INDEX_MIN + slot *
		       max_gearbox;
	} else {
		base = (base) ? base : MLXSW_REG_MTMP_MODULE_INDEX_MIN;
		return index + base + slot * max_modules;
	}
}

struct mlxsw_hwmon_gr {
	struct mlxsw_hwmon *hwmon;
	struct device *hwmon_dev;
	struct attribute_group group;
	const struct attribute_group *groups[2];
	struct attribute *attrs[MLXSW_HWMON_ATTR_COUNT + 1];
	struct mlxsw_hwmon_attr hwmon_attrs[MLXSW_HWMON_ATTR_COUNT];
	unsigned int attrs_count;
	u8 sensor_count;
	u8 module_sensor_max;
};

struct mlxsw_hwmon {
	struct mlxsw_core *core;
	const struct mlxsw_bus_info *bus_info;
	struct mlxsw_hwmon_gr *base;
	struct mlxsw_hwmon_gr **line_cards;
	u8 sensor_count;
	u8 module_sensor_max;
	u8 max_lc;
	u8 max_lc_modules;
	u8 max_lc_gearboxes;
};

static ssize_t mlxsw_hwmon_temp_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct mlxsw_hwmon_attr *mlwsw_hwmon_attr =
			container_of(attr, struct mlxsw_hwmon_attr, dev_attr);
	struct mlxsw_hwmon_gr *hwmon_gr = mlwsw_hwmon_attr->hwmon;
	char mtmp_pl[MLXSW_REG_MTMP_LEN];
	int temp, index;
	int err;

	index = mlxsw_hwmon_get_attr_index(mlwsw_hwmon_attr->type_index,
					   hwmon_gr->module_sensor_max,
					   mlwsw_hwmon_attr->slot, 0,
					   hwmon_gr->hwmon->max_lc_modules,
					   hwmon_gr->hwmon->max_lc_gearboxes);
	mlxsw_reg_mtmp_pack(mtmp_pl, index, false, false);
	err = mlxsw_reg_query(hwmon_gr->hwmon->core, MLXSW_REG(mtmp), mtmp_pl);
	if (err) {
		dev_err(hwmon_gr->hwmon->bus_info->dev, "Failed to query temp sensor\n");
		return err;
	}
	mlxsw_reg_mtmp_unpack(mtmp_pl, &temp, NULL, NULL);
	return sprintf(buf, "%d\n", temp);
}

static ssize_t mlxsw_hwmon_temp_max_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct mlxsw_hwmon_attr *mlwsw_hwmon_attr =
			container_of(attr, struct mlxsw_hwmon_attr, dev_attr);
	struct mlxsw_hwmon_gr *hwmon_gr = mlwsw_hwmon_attr->hwmon;
	char mtmp_pl[MLXSW_REG_MTMP_LEN];
	int temp_max, index;
	int err;

	index = mlxsw_hwmon_get_attr_index(mlwsw_hwmon_attr->type_index,
					   hwmon_gr->module_sensor_max,
					   mlwsw_hwmon_attr->slot, 0,
					   hwmon_gr->hwmon->max_lc_modules,
					   hwmon_gr->hwmon->max_lc_gearboxes);
	mlxsw_reg_mtmp_pack(mtmp_pl, index, false, false);
	err = mlxsw_reg_query(hwmon_gr->hwmon->core, MLXSW_REG(mtmp), mtmp_pl);
	if (err) {
		dev_err(hwmon_gr->hwmon->bus_info->dev, "Failed to query temp sensor\n");
		return err;
	}
	mlxsw_reg_mtmp_unpack(mtmp_pl, NULL, &temp_max, NULL);
	return sprintf(buf, "%d\n", temp_max);
}

static ssize_t mlxsw_hwmon_temp_rst_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t len)
{
	struct mlxsw_hwmon_attr *mlwsw_hwmon_attr =
			container_of(attr, struct mlxsw_hwmon_attr, dev_attr);
	struct mlxsw_hwmon_gr *hwmon_gr = mlwsw_hwmon_attr->hwmon;
	char mtmp_pl[MLXSW_REG_MTMP_LEN];
	unsigned long val;
	int index;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;
	if (val != 1)
		return -EINVAL;

	index = mlxsw_hwmon_get_attr_index(mlwsw_hwmon_attr->type_index,
					   hwmon_gr->module_sensor_max,
					   mlwsw_hwmon_attr->slot, 0,
					   hwmon_gr->hwmon->max_lc_modules,
					   hwmon_gr->hwmon->max_lc_gearboxes);
	mlxsw_reg_mtmp_pack(mtmp_pl, index, true, true);
	err = mlxsw_reg_write(hwmon_gr->hwmon->core, MLXSW_REG(mtmp), mtmp_pl);
	if (err) {
		dev_err(hwmon_gr->hwmon->bus_info->dev, "Failed to reset temp sensor history\n");
		return err;
	}
	return len;
}

static ssize_t mlxsw_hwmon_fan_rpm_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct mlxsw_hwmon_attr *mlwsw_hwmon_attr =
			container_of(attr, struct mlxsw_hwmon_attr, dev_attr);
	struct mlxsw_hwmon_gr *hwmon_gr = mlwsw_hwmon_attr->hwmon;
	char mfsm_pl[MLXSW_REG_MFSM_LEN];
	int err;

	mlxsw_reg_mfsm_pack(mfsm_pl, mlwsw_hwmon_attr->type_index);
	err = mlxsw_reg_query(hwmon_gr->hwmon->core, MLXSW_REG(mfsm), mfsm_pl);
	if (err) {
		dev_err(hwmon_gr->hwmon->bus_info->dev, "Failed to query fan\n");
		return err;
	}
	return sprintf(buf, "%u\n", mlxsw_reg_mfsm_rpm_get(mfsm_pl));
}

static ssize_t mlxsw_hwmon_fan_fault_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct mlxsw_hwmon_attr *mlwsw_hwmon_attr =
			container_of(attr, struct mlxsw_hwmon_attr, dev_attr);
	struct mlxsw_hwmon_gr *hwmon_gr = mlwsw_hwmon_attr->hwmon;
	char fore_pl[MLXSW_REG_FORE_LEN];
	bool fault;
	int err;

	err = mlxsw_reg_query(hwmon_gr->hwmon->core, MLXSW_REG(fore), fore_pl);
	if (err) {
		dev_err(hwmon_gr->hwmon->bus_info->dev, "Failed to query fan\n");
		return err;
	}
	mlxsw_reg_fore_unpack(fore_pl, mlwsw_hwmon_attr->type_index, &fault);

	return sprintf(buf, "%u\n", fault);
}

static ssize_t mlxsw_hwmon_pwm_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct mlxsw_hwmon_attr *mlwsw_hwmon_attr =
			container_of(attr, struct mlxsw_hwmon_attr, dev_attr);
	struct mlxsw_hwmon_gr *hwmon_gr = mlwsw_hwmon_attr->hwmon;
	char mfsc_pl[MLXSW_REG_MFSC_LEN];
	int err;

	mlxsw_reg_mfsc_pack(mfsc_pl, mlwsw_hwmon_attr->type_index, 0);
	err = mlxsw_reg_query(hwmon_gr->hwmon->core, MLXSW_REG(mfsc), mfsc_pl);
	if (err) {
		dev_err(hwmon_gr->hwmon->bus_info->dev, "Failed to query PWM\n");
		return err;
	}
	return sprintf(buf, "%u\n",
		       mlxsw_reg_mfsc_pwm_duty_cycle_get(mfsc_pl));
}

static ssize_t mlxsw_hwmon_pwm_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t len)
{
	struct mlxsw_hwmon_attr *mlwsw_hwmon_attr =
			container_of(attr, struct mlxsw_hwmon_attr, dev_attr);
	struct mlxsw_hwmon_gr *hwmon_gr = mlwsw_hwmon_attr->hwmon;
	char mfsc_pl[MLXSW_REG_MFSC_LEN];
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;
	if (val > 255)
		return -EINVAL;

	mlxsw_reg_mfsc_pack(mfsc_pl, mlwsw_hwmon_attr->type_index, val);
	err = mlxsw_reg_write(hwmon_gr->hwmon->core, MLXSW_REG(mfsc), mfsc_pl);
	if (err) {
		dev_err(hwmon_gr->hwmon->bus_info->dev, "Failed to write PWM\n");
		return err;
	}
	return len;
}

static ssize_t mlxsw_hwmon_module_temp_show(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct mlxsw_hwmon_attr *mlwsw_hwmon_attr =
			container_of(attr, struct mlxsw_hwmon_attr, dev_attr);
	struct mlxsw_hwmon_gr *hwmon_gr = mlwsw_hwmon_attr->hwmon;
	char mtmp_pl[MLXSW_REG_MTMP_LEN];
	u8 module;
	int temp;
	int err;

	module = mlwsw_hwmon_attr->type_index - hwmon_gr->sensor_count;
	module = mlxsw_hwmon_get_attr_index(mlwsw_hwmon_attr->type_index,
					   hwmon_gr->module_sensor_max,
					   mlwsw_hwmon_attr->slot, 0,
					   hwmon_gr->hwmon->max_lc_modules,
					   hwmon_gr->hwmon->max_lc_gearboxes);
	mlxsw_reg_mtmp_pack(mtmp_pl, module, false, false);
	err = mlxsw_reg_query(hwmon_gr->hwmon->core, MLXSW_REG(mtmp), mtmp_pl);
	if (err)
		return err;
	mlxsw_reg_mtmp_unpack(mtmp_pl, &temp, NULL, NULL);

	return sprintf(buf, "%d\n", temp);
}

static ssize_t mlxsw_hwmon_module_temp_fault_show(struct device *dev,
						  struct device_attribute *attr,
						  char *buf)
{
	struct mlxsw_hwmon_attr *mlwsw_hwmon_attr =
			container_of(attr, struct mlxsw_hwmon_attr, dev_attr);
	struct mlxsw_hwmon_gr *hwmon_gr = mlwsw_hwmon_attr->hwmon;
	char mtbr_pl[MLXSW_REG_MTBR_LEN] = {0};
	u8 module, fault;
	u16 temp;
	int err;

	module = mlwsw_hwmon_attr->type_index - hwmon_gr->sensor_count;
	module = mlxsw_hwmon_get_attr_index(mlwsw_hwmon_attr->type_index,
					   hwmon_gr->module_sensor_max,
					   mlwsw_hwmon_attr->slot,
					   MLXSW_REG_MTBR_BASE_MODULE_INDEX,
					   hwmon_gr->hwmon->max_lc_modules,
					   hwmon_gr->hwmon->max_lc_gearboxes);
	mlxsw_reg_mtbr_pack(mtbr_pl, module, 1);
	err = mlxsw_reg_query(hwmon_gr->hwmon->core, MLXSW_REG(mtbr), mtbr_pl);
	if (err) {
		dev_err(dev, "Failed to query module temperature sensor\n");
		return err;
	}

	mlxsw_reg_mtbr_temp_unpack(mtbr_pl, 0, &temp, NULL);

	/* Update status and temperature cache. */
	switch (temp) {
	case MLXSW_REG_MTBR_BAD_SENS_INFO:
		/* Untrusted cable is connected. Reading temperature from its
		 * sensor is faulty.
		 */
		fault = 1;
		break;
	case MLXSW_REG_MTBR_NO_CONN: /* fall-through */
	case MLXSW_REG_MTBR_NO_TEMP_SENS: /* fall-through */
	case MLXSW_REG_MTBR_INDEX_NA:
	default:
		fault = 0;
		break;
	}

	return sprintf(buf, "%u\n", fault);
}

static ssize_t
mlxsw_hwmon_module_temp_critical_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct mlxsw_hwmon_attr *mlwsw_hwmon_attr =
			container_of(attr, struct mlxsw_hwmon_attr, dev_attr);
	struct mlxsw_hwmon_gr *hwmon_gr = mlwsw_hwmon_attr->hwmon;
	int temp;
	u8 module;
	int err;

	module = mlwsw_hwmon_attr->type_index - hwmon_gr->sensor_count;
	err = mlxsw_env_module_temp_thresholds_get(hwmon_gr->hwmon->core,
						   module, SFP_TEMP_HIGH_WARN,
						   &temp);
	if (err) {
		dev_err(dev, "Failed to query module temperature thresholds\n");
		return err;
	}

	return sprintf(buf, "%u\n", temp);
}

static ssize_t
mlxsw_hwmon_module_temp_emergency_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct mlxsw_hwmon_attr *mlwsw_hwmon_attr =
			container_of(attr, struct mlxsw_hwmon_attr, dev_attr);
	struct mlxsw_hwmon_gr *hwmon_gr = mlwsw_hwmon_attr->hwmon;
	u8 module;
	int temp;
	int err;

	module = mlwsw_hwmon_attr->type_index - hwmon_gr->sensor_count;
	err = mlxsw_env_module_temp_thresholds_get(hwmon_gr->hwmon->core,
						   module, SFP_TEMP_HIGH_ALARM,
						   &temp);
	if (err) {
		dev_err(dev, "Failed to query module temperature thresholds\n");
		return err;
	}

	return sprintf(buf, "%u\n", temp);
}

static ssize_t
mlxsw_hwmon_module_temp_label_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct mlxsw_hwmon_attr *mlwsw_hwmon_attr =
			container_of(attr, struct mlxsw_hwmon_attr, dev_attr);

	return sprintf(buf, "front panel %03u\n",
		       mlwsw_hwmon_attr->type_index);
}

static ssize_t
mlxsw_hwmon_gbox_temp_label_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct mlxsw_hwmon_attr *mlwsw_hwmon_attr =
			container_of(attr, struct mlxsw_hwmon_attr, dev_attr);
	struct mlxsw_hwmon_gr *hwmon_gr = mlwsw_hwmon_attr->hwmon;
	int index = mlwsw_hwmon_attr->type_index -
		    hwmon_gr->module_sensor_max + 1;

	return sprintf(buf, "gearbox %03u\n", index);
}

enum mlxsw_hwmon_attr_type {
	MLXSW_HWMON_ATTR_TYPE_TEMP,
	MLXSW_HWMON_ATTR_TYPE_TEMP_MAX,
	MLXSW_HWMON_ATTR_TYPE_TEMP_RST,
	MLXSW_HWMON_ATTR_TYPE_FAN_RPM,
	MLXSW_HWMON_ATTR_TYPE_FAN_FAULT,
	MLXSW_HWMON_ATTR_TYPE_PWM,
	MLXSW_HWMON_ATTR_TYPE_TEMP_MODULE,
	MLXSW_HWMON_ATTR_TYPE_TEMP_MODULE_FAULT,
	MLXSW_HWMON_ATTR_TYPE_TEMP_MODULE_CRIT,
	MLXSW_HWMON_ATTR_TYPE_TEMP_MODULE_EMERG,
	MLXSW_HWMON_ATTR_TYPE_TEMP_MODULE_LABEL,
	MLXSW_HWMON_ATTR_TYPE_TEMP_GBOX_LABEL,
};

static void mlxsw_hwmon_attr_add(struct mlxsw_hwmon_gr *hwmon_gr,
				 enum mlxsw_hwmon_attr_type attr_type,
				 unsigned int type_index, unsigned int num,
				 u8 slot, struct attribute **attrs,
				 unsigned int *attrs_count)
{
	struct mlxsw_hwmon_attr *mlxsw_hwmon_attr;
	unsigned int attr_index;

	attr_index = *attrs_count;
	mlxsw_hwmon_attr = &hwmon_gr->hwmon_attrs[attr_index];

	switch (attr_type) {
	case MLXSW_HWMON_ATTR_TYPE_TEMP:
		mlxsw_hwmon_attr->dev_attr.show = mlxsw_hwmon_temp_show;
		mlxsw_hwmon_attr->dev_attr.attr.mode = 0444;
		snprintf(mlxsw_hwmon_attr->name, sizeof(mlxsw_hwmon_attr->name),
			 "temp%u_input", num + 1);
		break;
	case MLXSW_HWMON_ATTR_TYPE_TEMP_MAX:
		mlxsw_hwmon_attr->dev_attr.show = mlxsw_hwmon_temp_max_show;
		mlxsw_hwmon_attr->dev_attr.attr.mode = 0444;
		snprintf(mlxsw_hwmon_attr->name, sizeof(mlxsw_hwmon_attr->name),
			 "temp%u_highest", num + 1);
		break;
	case MLXSW_HWMON_ATTR_TYPE_TEMP_RST:
		mlxsw_hwmon_attr->dev_attr.store = mlxsw_hwmon_temp_rst_store;
		mlxsw_hwmon_attr->dev_attr.attr.mode = 0200;
		snprintf(mlxsw_hwmon_attr->name, sizeof(mlxsw_hwmon_attr->name),
			 "temp%u_reset_history", num + 1);
		break;
	case MLXSW_HWMON_ATTR_TYPE_FAN_RPM:
		mlxsw_hwmon_attr->dev_attr.show = mlxsw_hwmon_fan_rpm_show;
		mlxsw_hwmon_attr->dev_attr.attr.mode = 0444;
		snprintf(mlxsw_hwmon_attr->name, sizeof(mlxsw_hwmon_attr->name),
			 "fan%u_input", num + 1);
		break;
	case MLXSW_HWMON_ATTR_TYPE_FAN_FAULT:
		mlxsw_hwmon_attr->dev_attr.show = mlxsw_hwmon_fan_fault_show;
		mlxsw_hwmon_attr->dev_attr.attr.mode = 0444;
		snprintf(mlxsw_hwmon_attr->name, sizeof(mlxsw_hwmon_attr->name),
			 "fan%u_fault", num + 1);
		break;
	case MLXSW_HWMON_ATTR_TYPE_PWM:
		mlxsw_hwmon_attr->dev_attr.show = mlxsw_hwmon_pwm_show;
		mlxsw_hwmon_attr->dev_attr.store = mlxsw_hwmon_pwm_store;
		mlxsw_hwmon_attr->dev_attr.attr.mode = 0644;
		snprintf(mlxsw_hwmon_attr->name, sizeof(mlxsw_hwmon_attr->name),
			 "pwm%u", num + 1);
		break;
	case MLXSW_HWMON_ATTR_TYPE_TEMP_MODULE:
		mlxsw_hwmon_attr->dev_attr.show = mlxsw_hwmon_module_temp_show;
		mlxsw_hwmon_attr->dev_attr.attr.mode = 0444;
		snprintf(mlxsw_hwmon_attr->name, sizeof(mlxsw_hwmon_attr->name),
			 "temp%u_input", num + 1);
		break;
	case MLXSW_HWMON_ATTR_TYPE_TEMP_MODULE_FAULT:
		mlxsw_hwmon_attr->dev_attr.show =
					mlxsw_hwmon_module_temp_fault_show;
		mlxsw_hwmon_attr->dev_attr.attr.mode = 0444;
		snprintf(mlxsw_hwmon_attr->name, sizeof(mlxsw_hwmon_attr->name),
			 "temp%u_fault", num + 1);
		break;
	case MLXSW_HWMON_ATTR_TYPE_TEMP_MODULE_CRIT:
		mlxsw_hwmon_attr->dev_attr.show =
			mlxsw_hwmon_module_temp_critical_show;
		mlxsw_hwmon_attr->dev_attr.attr.mode = 0444;
		snprintf(mlxsw_hwmon_attr->name, sizeof(mlxsw_hwmon_attr->name),
			 "temp%u_crit", num + 1);
		break;
	case MLXSW_HWMON_ATTR_TYPE_TEMP_MODULE_EMERG:
		mlxsw_hwmon_attr->dev_attr.show =
			mlxsw_hwmon_module_temp_emergency_show;
		mlxsw_hwmon_attr->dev_attr.attr.mode = 0444;
		snprintf(mlxsw_hwmon_attr->name, sizeof(mlxsw_hwmon_attr->name),
			 "temp%u_emergency", num + 1);
		break;
	case MLXSW_HWMON_ATTR_TYPE_TEMP_MODULE_LABEL:
		mlxsw_hwmon_attr->dev_attr.show =
			mlxsw_hwmon_module_temp_label_show;
		mlxsw_hwmon_attr->dev_attr.attr.mode = 0444;
		snprintf(mlxsw_hwmon_attr->name, sizeof(mlxsw_hwmon_attr->name),
			 "temp%u_label", num + 1);
		break;
	case MLXSW_HWMON_ATTR_TYPE_TEMP_GBOX_LABEL:
		mlxsw_hwmon_attr->dev_attr.show =
			mlxsw_hwmon_gbox_temp_label_show;
		mlxsw_hwmon_attr->dev_attr.attr.mode = 0444;
		snprintf(mlxsw_hwmon_attr->name, sizeof(mlxsw_hwmon_attr->name),
			 "temp%u_label", num + 1);
		break;
	default:
		WARN_ON(1);
	}

	mlxsw_hwmon_attr->type_index = type_index;
	mlxsw_hwmon_attr->slot = slot;
	mlxsw_hwmon_attr->hwmon = hwmon_gr;
	mlxsw_hwmon_attr->dev_attr.attr.name = mlxsw_hwmon_attr->name;
	sysfs_attr_init(&mlxsw_hwmon_attr->dev_attr.attr);

	attrs[attr_index] = &mlxsw_hwmon_attr->dev_attr.attr;
	(*attrs_count)++;
}

static int mlxsw_hwmon_temp_init(struct mlxsw_hwmon_gr *hwmon_gr)
{
	char mtcap_pl[MLXSW_REG_MTCAP_LEN] = {0};
	char mtmp_pl[MLXSW_REG_MTMP_LEN];
	int i;
	int err;

	err = mlxsw_reg_query(hwmon_gr->hwmon->core, MLXSW_REG(mtcap),
			      mtcap_pl);
	if (err) {
		dev_err(hwmon_gr->hwmon->bus_info->dev, "Failed to get number of temp sensors\n");
		return err;
	}
	hwmon_gr->sensor_count = mlxsw_reg_mtcap_sensor_count_get(mtcap_pl);
	for (i = 0; i < hwmon_gr->sensor_count; i++) {
		mlxsw_reg_mtmp_pack(mtmp_pl, i, true, true);
		err = mlxsw_reg_write(hwmon_gr->hwmon->core,
				      MLXSW_REG(mtmp), mtmp_pl);
		if (err) {
			dev_err(hwmon_gr->hwmon->bus_info->dev, "Failed to setup temp sensor number %d\n",
				i);
			return err;
		}
		mlxsw_hwmon_attr_add(hwmon_gr,
				     MLXSW_HWMON_ATTR_TYPE_TEMP, i, i, 0,
				     hwmon_gr->attrs,
				     &hwmon_gr->attrs_count);
		mlxsw_hwmon_attr_add(hwmon_gr,
				     MLXSW_HWMON_ATTR_TYPE_TEMP_MAX, i, i, 0,
				     hwmon_gr->attrs,
				     &hwmon_gr->attrs_count);
		mlxsw_hwmon_attr_add(hwmon_gr,
				     MLXSW_HWMON_ATTR_TYPE_TEMP_RST, i, i, 0,
				     hwmon_gr->attrs,
				     &hwmon_gr->attrs_count);
	}
	return 0;
}

static int mlxsw_hwmon_fans_init(struct mlxsw_hwmon_gr *hwmon_gr)
{
	char mfcr_pl[MLXSW_REG_MFCR_LEN] = {0};
	enum mlxsw_reg_mfcr_pwm_frequency freq;
	unsigned int type_index;
	unsigned int num;
	u16 tacho_active;
	u8 pwm_active;
	int err;

	err = mlxsw_reg_query(hwmon_gr->hwmon->core, MLXSW_REG(mfcr), mfcr_pl);
	if (err) {
		dev_err(hwmon_gr->hwmon->bus_info->dev, "Failed to get to probe PWMs and Tachometers\n");
		return err;
	}
	mlxsw_reg_mfcr_unpack(mfcr_pl, &freq, &tacho_active, &pwm_active);
	num = 0;
	for (type_index = 0; type_index < MLXSW_MFCR_TACHOS_MAX; type_index++) {
		if (tacho_active & BIT(type_index)) {
			mlxsw_hwmon_attr_add(hwmon_gr,
					     MLXSW_HWMON_ATTR_TYPE_FAN_RPM,
					     type_index, num, 0,
					     hwmon_gr->attrs,
					     &hwmon_gr->attrs_count);
			mlxsw_hwmon_attr_add(hwmon_gr,
					     MLXSW_HWMON_ATTR_TYPE_FAN_FAULT,
					     type_index, num++, 0,
					     hwmon_gr->attrs,
					     &hwmon_gr->attrs_count);
		}
	}
	num = 0;
	for (type_index = 0; type_index < MLXSW_MFCR_PWMS_MAX; type_index++) {
		if (pwm_active & BIT(type_index))
			mlxsw_hwmon_attr_add(hwmon_gr,
					     MLXSW_HWMON_ATTR_TYPE_PWM,
					     type_index, num++, 0,
					     hwmon_gr->attrs,
					     &hwmon_gr->attrs_count);
	}
	return 0;
}

static int mlxsw_hwmon_module_init(struct mlxsw_hwmon_gr *hwmon_gr, int slot)
{
	char mgpir_pl[MLXSW_REG_MGPIR_LEN];
	int i, err;

	if (!mlxsw_core_res_query_enabled(hwmon_gr->hwmon->core))
		return 0;

	mlxsw_reg_mgpir_pack(mgpir_pl, slot);
	err = mlxsw_reg_query(hwmon_gr->hwmon->core, MLXSW_REG(mgpir),
			      mgpir_pl);
	if (err)
		return err;

	mlxsw_reg_mgpir_unpack(mgpir_pl, NULL, NULL, NULL,
			       &hwmon_gr->module_sensor_max, NULL, NULL, NULL);

	/* Add extra attributes for module temperature. Sensor index is
	 * assigned to sensor_count value, while all indexed before
	 * sensor_count are already utilized by the sensors connected through
	 * mtmp register by mlxsw_hwmon_temp_init().
	 */
	hwmon_gr->module_sensor_max = hwmon_gr->sensor_count +
				      hwmon_gr->module_sensor_max;
	for (i = hwmon_gr->sensor_count; i < hwmon_gr->module_sensor_max;
	     i++) {
		mlxsw_hwmon_attr_add(hwmon_gr,
				     MLXSW_HWMON_ATTR_TYPE_TEMP_MODULE, i, i,
				     slot, NULL, NULL);
		mlxsw_hwmon_attr_add(hwmon_gr,
				     MLXSW_HWMON_ATTR_TYPE_TEMP_MODULE_FAULT,
				     i, i, slot, NULL, NULL);
		mlxsw_hwmon_attr_add(hwmon_gr,
				     MLXSW_HWMON_ATTR_TYPE_TEMP_MODULE_CRIT, i,
				     i, slot, NULL, NULL);
		mlxsw_hwmon_attr_add(hwmon_gr,
				     MLXSW_HWMON_ATTR_TYPE_TEMP_MODULE_EMERG,
				     i, i, slot, NULL, NULL);
		mlxsw_hwmon_attr_add(hwmon_gr,
				     MLXSW_HWMON_ATTR_TYPE_TEMP_MODULE_LABEL,
				     i, i, slot, NULL, NULL);
	}

	return 0;
}

static int mlxsw_hwmon_gearbox_init(struct mlxsw_hwmon_gr *hwmon_gr, int slot)
{
	enum mlxsw_reg_mgpir_device_type device_type;
	int index, max_index, sensor_index;
	char mgpir_pl[MLXSW_REG_MGPIR_LEN];
	char mtmp_pl[MLXSW_REG_MTMP_LEN];
	u8 gbox_num;
	int err;

	mlxsw_reg_mgpir_pack(mgpir_pl, slot);
	err = mlxsw_reg_query(hwmon_gr->hwmon->core, MLXSW_REG(mgpir),
			      mgpir_pl);
	if (err)
		return err;

	mlxsw_reg_mgpir_unpack(mgpir_pl, &gbox_num, &device_type, NULL, NULL,
			       NULL, NULL, NULL);
	if (device_type != MLXSW_REG_MGPIR_DEVICE_TYPE_GEARBOX_DIE ||
	    !gbox_num)
		return 0;

	index = hwmon_gr->module_sensor_max;
	max_index = hwmon_gr->module_sensor_max + gbox_num;
	while (index < max_index) {
		sensor_index = index % hwmon_gr->module_sensor_max +
			       MLXSW_REG_MTMP_GBOX_INDEX_MIN;
		mlxsw_reg_mtmp_pack(mtmp_pl, sensor_index, true, true);
		err = mlxsw_reg_write(hwmon_gr->hwmon->core,
				      MLXSW_REG(mtmp), mtmp_pl);
		if (err) {
			dev_err(hwmon_gr->hwmon->bus_info->dev, "Failed to setup temp sensor number %d\n",
				sensor_index);
			return err;
		}
		mlxsw_hwmon_attr_add(hwmon_gr, MLXSW_HWMON_ATTR_TYPE_TEMP,
				     index, index, slot, NULL, NULL);
		mlxsw_hwmon_attr_add(hwmon_gr,
				     MLXSW_HWMON_ATTR_TYPE_TEMP_MAX, index,
				     index, slot, NULL, NULL);
		mlxsw_hwmon_attr_add(hwmon_gr,
				     MLXSW_HWMON_ATTR_TYPE_TEMP_RST, index,
				     index, slot, NULL, NULL);
		mlxsw_hwmon_attr_add(hwmon_gr,
				     MLXSW_HWMON_ATTR_TYPE_TEMP_GBOX_LABEL,
				     index, index, slot, NULL, NULL);
		index++;
	}

	return 0;
}

int mlxsw_hwmon_lc_init(struct mlxsw_hwmon *mlxsw_hwmon, int slot)
{
	struct mlxsw_hwmon_gr *lc = mlxsw_hwmon->line_cards[slot - 1];
	struct device *dev = mlxsw_hwmon->bus_info->dev;
	int err;

	err = mlxsw_hwmon_module_init(lc, slot);
	if (err)
		goto err_temp_lc_module_init;

	err = mlxsw_hwmon_gearbox_init(lc, slot);
	if (err)
		goto err_temp_lc_gearbox_init;

	lc->groups[0] = &lc->group;
	lc->group.attrs = lc->attrs;

	lc->hwmon_dev = hwmon_device_register_with_groups(dev,
							  "mlxsw", lc,
							   lc->groups);
	if (IS_ERR(lc->hwmon_dev)) {
		err = PTR_ERR(lc->hwmon_dev);
		goto err_hwmon_lc_register;
	}

	return 0;

err_hwmon_lc_register:
err_temp_lc_gearbox_init:
err_temp_lc_module_init:
	return err;
}

void mlxsw_hwmon_lc_fini(struct mlxsw_hwmon *mlxsw_hwmon, int slot)
{
	struct mlxsw_hwmon_gr *lc = mlxsw_hwmon->line_cards[slot - 1];

	if (lc->hwmon_dev)
		hwmon_device_unregister(lc->hwmon_dev);
}

static int mlxsw_hwmon_line_cards_init(struct mlxsw_core *mlxsw_core,
				       struct mlxsw_hwmon *mlxsw_hwmon)
{
	char mgpir_pl[MLXSW_REG_MGPIR_LEN];
	int err;

	/* Obtain line cards number. */
	mlxsw_reg_mgpir_pack(mgpir_pl, 0);
	err = mlxsw_reg_query(mlxsw_core, MLXSW_REG(mgpir), mgpir_pl);
	if (err)
		return err;
	mlxsw_reg_mgpir_unpack(mgpir_pl, NULL, NULL, NULL, NULL,
			       &mlxsw_hwmon->max_lc,
			       &mlxsw_hwmon->max_lc_modules,
			       &mlxsw_hwmon->max_lc_gearboxes);
	if (!mlxsw_hwmon->max_lc)
		return 0;

	mlxsw_hwmon->line_cards = kcalloc(mlxsw_hwmon->max_lc,
				      sizeof(*mlxsw_hwmon->line_cards),
				      GFP_KERNEL);
	if (!mlxsw_hwmon->line_cards)
		return -ENOMEM;

	return 0;
}

static void mlxsw_hwmon_line_cards_fini(struct mlxsw_hwmon *mlxsw_hwmon)
{
	int i;

	for (i = 0; i < mlxsw_hwmon->max_lc; i++) {
		if (mlxsw_hwmon->line_cards[i])
			mlxsw_hwmon_lc_fini(mlxsw_hwmon, i);
	}
	if (mlxsw_hwmon->line_cards)
		kfree(mlxsw_hwmon->line_cards);
}

int mlxsw_hwmon_init(struct mlxsw_core *mlxsw_core,
		     const struct mlxsw_bus_info *mlxsw_bus_info,
		     struct mlxsw_hwmon **p_hwmon)
{
	struct mlxsw_hwmon *mlxsw_hwmon;
	struct device *hwmon_dev;
	int err;

	mlxsw_hwmon = kzalloc(sizeof(*mlxsw_hwmon), GFP_KERNEL);
	if (!mlxsw_hwmon)
		return -ENOMEM;
	mlxsw_hwmon->base = kzalloc(sizeof(*mlxsw_hwmon->base), GFP_KERNEL);
	if (!mlxsw_hwmon->base)
		return -ENOMEM;
	mlxsw_hwmon->core = mlxsw_core;
	mlxsw_hwmon->bus_info = mlxsw_bus_info;

	err = mlxsw_hwmon_line_cards_init(mlxsw_core, mlxsw_hwmon);
	if (err)
		goto err_hwmon_line_cards_init;

	err = mlxsw_hwmon_temp_init(mlxsw_hwmon->base);
	if (err)
		goto err_temp_init;

	err = mlxsw_hwmon_fans_init(mlxsw_hwmon->base);
	if (err)
		goto err_fans_init;

	err = mlxsw_hwmon_module_init(mlxsw_hwmon->base, 0);
	if (err)
		goto err_temp_module_init;

	err = mlxsw_hwmon_gearbox_init(mlxsw_hwmon->base, 0);
	if (err)
		goto err_temp_gearbox_init;

	mlxsw_hwmon->base->groups[0] = &mlxsw_hwmon->base->group;
	mlxsw_hwmon->base->group.attrs = mlxsw_hwmon->base->attrs;

	hwmon_dev = hwmon_device_register_with_groups(mlxsw_bus_info->dev,
						"mlxsw", mlxsw_hwmon->base,
						mlxsw_hwmon->base->groups);
	if (IS_ERR(hwmon_dev)) {
		err = PTR_ERR(hwmon_dev);
		goto err_hwmon_register;
	}

	mlxsw_hwmon->base->hwmon_dev = hwmon_dev;
	*p_hwmon = mlxsw_hwmon;
	return 0;

err_hwmon_line_cards_init:
err_hwmon_register:
err_temp_gearbox_init:
err_temp_module_init:
err_fans_init:
err_temp_init:
	kfree(mlxsw_hwmon);
	return err;
}

void mlxsw_hwmon_fini(struct mlxsw_hwmon *mlxsw_hwmon)
{
	mlxsw_hwmon_line_cards_fini(mlxsw_hwmon);
	hwmon_device_unregister(mlxsw_hwmon->base->hwmon_dev);
	kfree(mlxsw_hwmon->base);
	kfree(mlxsw_hwmon);
}
