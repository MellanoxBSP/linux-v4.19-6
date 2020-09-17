// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/*
 *
 * Copyright (C) 2016-2020 Mellanox Technologies
 */

#ifndef __LINUX_PLATFORM_DATA_MLXREG_H
#define __LINUX_PLATFORM_DATA_MLXREG_H

#define MLXREG_CORE_LABEL_MAX_SIZE	32
#define MLXREG_CORE_WD_FEATURE_NOWAYOUT		BIT(0)
#define MLXREG_CORE_WD_FEATURE_START_AT_BOOT	BIT(1)

/**
 * enum mlxreg_wdt_type - type of HW watchdog
 *
 * TYPE1 HW watchdog implementation exist in old systems.
 * All new systems have TYPE2 HW watchdog.
 * TYPE3 HW watchdog can exist on all systems with new CPLD.
 * TYPE3 is selected by WD capability bit.
 */
enum mlxreg_wdt_type {
	MLX_WDT_TYPE1,
	MLX_WDT_TYPE2,
	MLX_WDT_TYPE3,
};

/**
 * enum mlxreg_hotplug_kind - kind of hotplug entry
 *
 * @MLXREG_HOTPLUG_DEVICE_NA: do not care;
 * @MLXREG_HOTPLUG_LC_PRESENT: entry for line card presence events;
 * @MLXREG_HOTPLUG_LC_VERIFIED: entry for line card verification events;
 * @MLXREG_HOTPLUG_LC_POWERED: entry for line card power events;
 * @MLXREG_HOTPLUG_LC_SYNCED: entry for line card bus synchronization events;
 * @MLXREG_HOTPLUG_LC_READY: entry for line card ready events;
 */
enum mlxreg_hotplug_kind {
	MLXREG_HOTPLUG_DEVICE_NA = 0,
	MLXREG_HOTPLUG_LC_PRESENT = 1,
	MLXREG_HOTPLUG_LC_VERIFIED = 2,
	MLXREG_HOTPLUG_LC_POWERED = 3,
	MLXREG_HOTPLUG_LC_SYNCED = 4,
	MLXREG_HOTPLUG_LC_READY = 5,
};

/**
 * enum mlxreg_hotplug_device_action - hotplug device action required for
 *				       driver's connectivity
 *
 * @MLXREG_HOTPLUG_DEVICE_DEFAULT_ACTION: probe device for 'on' event, remove
 *					  for 'off' event;
 * @MLXREG_HOTPLUG_DEVICE_PLATFORM_ACTION: probe platform device for 'on'
 *					   event, remove for 'off' event;
 * @MLXREG_HOTPLUG_DEVICE_NO_ACTION: no connectivity action is required;
 */
enum mlxreg_hotplug_device_action {
	MLXREG_HOTPLUG_DEVICE_DEFAULT_ACTION = 0,
	MLXREG_HOTPLUG_DEVICE_PLATFORM_ACTION = 1,
	MLXREG_HOTPLUG_DEVICE_NO_ACTION = 2,
};

/**
 * struct mlxreg_hotplug_device - I2C device data:
 *
 * @adapter: I2C device adapter;
 * @client: I2C device client;
 * @brdinfo: device board information;
 * @nr: I2C device adapter number, to which device is to be attached;
 * @pdev: platform device, if device is instantiated as a platform device;
 * @action: action to be performed upon event receiving;
 *
 * Structure represents I2C hotplug device static data (board topology) and
 * dynamic data (related kernel objects handles).
 */
struct mlxreg_hotplug_device {
	struct i2c_adapter *adapter;
	struct i2c_client *client;
	struct i2c_board_info *brdinfo;
	int nr;
	struct platform_device *pdev;
	enum mlxreg_hotplug_device_action action;
};

/**
 * struct mlxreg_core_data - attributes control data:
 *
 * @label: attribute label;
 * @reg: attribute register;
 * @mask: attribute access mask;
 * @bit: attribute effective bit;
 * @capability: attribute capability register;
 * @reg_prsnt: attribute presence register;
 * @mode: access mode;
 * @np - pointer to node platform associated with attribute;
 * @hpdev - hotplug device data;
 * @health_cntr: dynamic device health indication counter;
 * @attached: true if device has been attached after good health indication;
 * @regnum: number of registers occupied by multi-register attribute;
 * @slot: slot number, at which device is located;
 */
struct mlxreg_core_data {
	char label[MLXREG_CORE_LABEL_MAX_SIZE];
	u32 reg;
	u32 mask;
	u32 bit;
	u32 capability;
	u32 reg_prsnt;
	umode_t	mode;
	struct device_node *np;
	struct mlxreg_hotplug_device hpdev;
	u32 health_cntr;
	bool attached;
	u8 regnum;
	u8 slot;
};

/**
 * struct mlxreg_core_item - same type components controlled by the driver:
 *
 * @data: component data;
 * @kind: kind of hotplug attribute;
 * @aggr_mask: group aggregation mask;
 * @reg: group interrupt status register;
 * @mask: group interrupt mask;
 * @capability: group capability register;
 * @cache: last status value for elements fro the same group;
 * @count: number of available elements in the group;
 * @ind: element's index inside the group;
 * @inversed: if 0: 0 for signal status is OK, if 1 - 1 is OK;
 * @health: true if device has health indication, false in other case;
 */
struct mlxreg_core_item {
	struct mlxreg_core_data *data;
	enum mlxreg_hotplug_kind kind;
	u32 aggr_mask;
	u32 reg;
	u32 mask;
	u32 capability;
	u32 cache;
	u8 count;
	u8 ind;
	u8 inversed;
	u8 health;
};

/**
 * struct mlxreg_core_platform_data - platform data:
 *
 * @data: instance private data;
 * @regmap: register map of parent device;
 * @counter: number of instances;
 * @features: supported features of device;
 * @version: implementation version;
 * @identity: device identity name;
 */
struct mlxreg_core_platform_data {
	struct mlxreg_core_data *data;
	void *regmap;
	int counter;
	u32 features;
	u32 version;
	char identity[MLXREG_CORE_LABEL_MAX_SIZE];
	u32 capability;
};

/**
 * struct mlxreg_core_hotplug_platform_data - hotplug platform data:
 *
 * @items: same type components with the hotplug capability;
 * @irq: platform interrupt number;
 * @regmap: register map of parent device;
 * @counter: number of the components with the hotplug capability;
 * @cell: location of top aggregation interrupt register;
 * @mask: top aggregation interrupt common mask;
 * @cell_low: location of low aggregation interrupt register;
 * @mask_low: low aggregation interrupt common mask;
 * @deferred_nr: I2C adapter number must be exist prior probing execution;
 * @shift_nr: I2C adapter numbers must be incremented by this value;
 */
struct mlxreg_core_hotplug_platform_data {
	struct mlxreg_core_item *items;
	int irq;
	void *regmap;
	int counter;
	u32 cell;
	u32 mask;
	u32 cell_low;
	u32 mask_low;
	int deferred_nr;
	int shift_nr;
};

/**
 * struct mlxplat_notifier_info - platform data notifier info:
 *
 * @handle: handle of device, for which event has been generated;
 * @label: label of attribute, associated with event;
 * @slot: device location;
 * @topo_id: device topology id;
 * @event_data: event user data;
 * @action: true - non zero - action is on, zero - is off;
 */
struct mlxplat_notifier_info {
	void *handle;
	char label[MLXREG_CORE_LABEL_MAX_SIZE];
	int slot;
	u32 topo_id;
	u32 event_data;
	u8 action;
};

int mlxplat_blk_notifiers_call_chain(unsigned long val,
				     struct mlxplat_notifier_info *info);

int mlxplat_blk_notifier_register(struct notifier_block *nb);

int mlxplat_blk_notifier_unregister(struct notifier_block *nb);

#endif /* __LINUX_PLATFORM_DATA_MLXREG_H */
