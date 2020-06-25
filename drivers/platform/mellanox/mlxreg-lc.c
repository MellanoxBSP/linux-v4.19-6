// SPDX-License-Identifier: GPL-2.0+
/*
 * Mellanox line card driver
 *
 * Copyright (C) 2020 Mellanox Technologies Ltd.
 */

#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/platform_data/mlxcpld.h>
#include <linux/platform_data/mlxreg.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

/* I2C bus IO offsets */
#define MLXREG_LC_REG_CPLD1_VER_OFFSET		0x00
#define MLXREG_LC_REG_CPLD2_VER_OFFSET		0x01
#define MLXREG_LC_REG_CPLD1_PN_OFFSET		0x04
#define MLXREG_LC_REG_CPLD2_PN_OFFSET		0x06
#define MLXREG_LC_REG_RESET_CAUSE_OFFSET	0x1d
#define MLXREG_LC_REG_LED1_OFFSET		0x20
#define MLXREG_LC_REG_GP0_OFFSET		0x2e
#define MLXREG_LC_REG_FIELD_UPGRADE		0x34
#define MLXREG_LC_CHANNEL_I2C_REG		0xdc
#define MLXREG_LC_REG_CPLD1_MVER_OFFSET		0xde
#define MLXREG_LC_REG_CPLD2_MVER_OFFSET		0xdf
#define MLXREG_LC_REG_MAX_POWER_OFFSET		0xf1
#define MLXREG_LC_REG_CONFIG_OFFSET		0xfb

#define MLXREG_LC_BASE_NR		100
#define MLXREG_LC_CHAN_MAX		32
#define MLXREG_LC_SET_BASE_NR(slot)	(MLXREG_LC_BASE_NR + MLXREG_LC_CHAN_MAX * ((slot) - 1))

/**
 * enum mlxreg_lc_type - line cards types
 *
 * MLXREG_LC_SN4800_C16 - 100GbE line card with 16 QSFP28 ports;
 */
enum mlxreg_lc_type {
	MLXREG_LC_SN4800_C16 = 0x00f3,
};

/* mlxreg_lc - device private data
 * @list: list of line card objects;
 * @dev - platform device;
 * regs_io_data - register access platform data;
 * led_data - LED platform data ;
 * mux_data - MUX platform data;
 * @led - LED device;
 * @io_regs - register access device;
 * @mux_brdinfo - mux configuration;
 * @mux - mux devices;
 * @aux_devs - I2C devices feeding by auxiliary power;
 * @aux_devs_num - number of I2C devices feeding by auxiliary power;
 * @main_devs - I2C devices feeding by main power;
 * @main_devs_num - number of I2C devices feeding by main power;
 * @topo_id - topology Id of line card;
 */
struct mlxreg_lc {
	struct list_head list;
	struct device *dev;
	struct mlxreg_core_platform_data *regs_io_data;
	struct mlxreg_core_platform_data *led_data;
	struct mlxcpld_mux_plat_data *mux_data;
	struct platform_device *led;
	struct platform_device *io_regs;
	struct i2c_board_info *mux_brdinfo;
	struct i2c_client *mux;
	struct mlxreg_hotplug_device *aux_devs;
	int aux_devs_num;
	struct mlxreg_hotplug_device *main_devs;
	int main_devs_num;
	int topo_id;
};

static struct mlxreg_lc_list {
	struct list_head list;
} mlxreg_lc_list;

static bool mlxreg_lc_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MLXREG_LC_REG_LED1_OFFSET:
	case MLXREG_LC_REG_GP0_OFFSET:
	case MLXREG_LC_REG_FIELD_UPGRADE:
	case MLXREG_LC_CHANNEL_I2C_REG:
		return true;
	}
	return false;
}

static bool mlxreg_lc_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MLXREG_LC_REG_CPLD1_VER_OFFSET:
	case MLXREG_LC_REG_CPLD2_VER_OFFSET:
	case MLXREG_LC_REG_CPLD1_PN_OFFSET:
	case MLXREG_LC_REG_CPLD2_PN_OFFSET:
	case MLXREG_LC_REG_RESET_CAUSE_OFFSET:
	case MLXREG_LC_REG_LED1_OFFSET:
	case MLXREG_LC_REG_GP0_OFFSET:
	case MLXREG_LC_REG_FIELD_UPGRADE:
	case MLXREG_LC_CHANNEL_I2C_REG:
	case MLXREG_LC_REG_CPLD1_MVER_OFFSET:
	case MLXREG_LC_REG_CPLD2_MVER_OFFSET:
	case MLXREG_LC_REG_MAX_POWER_OFFSET:
	case MLXREG_LC_REG_CONFIG_OFFSET:
		return true;
	}
	return false;
}

static bool mlxreg_lc_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MLXREG_LC_REG_CPLD1_VER_OFFSET:
	case MLXREG_LC_REG_CPLD2_VER_OFFSET:
	case MLXREG_LC_REG_CPLD1_PN_OFFSET:
	case MLXREG_LC_REG_CPLD2_PN_OFFSET:
	case MLXREG_LC_REG_RESET_CAUSE_OFFSET:
	case MLXREG_LC_REG_LED1_OFFSET:
	case MLXREG_LC_REG_GP0_OFFSET:
	case MLXREG_LC_REG_FIELD_UPGRADE:
	case MLXREG_LC_CHANNEL_I2C_REG:
	case MLXREG_LC_REG_CPLD1_MVER_OFFSET:
	case MLXREG_LC_REG_CPLD2_MVER_OFFSET:
	case MLXREG_LC_REG_MAX_POWER_OFFSET:
	case MLXREG_LC_REG_CONFIG_OFFSET:
		return true;
	}
	return false;
}

static const struct reg_default mlxreg_lc_regmap_default[] = {
	{ MLXREG_LC_CHANNEL_I2C_REG, 0x00 },
};

/* Configuration for the register map of a device with 2 bytes address space. */
static const struct regmap_config mlxreg_lc_regmap_conf = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = 1024,
	.cache_type = REGCACHE_FLAT,
	.writeable_reg = mlxreg_lc_writeable_reg,
	.readable_reg = mlxreg_lc_readable_reg,
	.volatile_reg = mlxreg_lc_volatile_reg,
	.reg_defaults = mlxreg_lc_regmap_default,
	.num_reg_defaults = ARRAY_SIZE(mlxreg_lc_regmap_default),
};

/* Defaul channels vector. */
static int mlxreg_lc_default_channels[] = { 0x04, 0x05, 0x06, 0x07, 0x08, 0x10, 0x20, 0x21, 0x22, 0x23, 0x40, 0x41,
					    0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d,
					    0x4e, 0x4f
};

/* Defaul mux configuration. */
static struct mlxcpld_mux_plat_data mlxreg_lc_default_mux_data[] = {
	{
		.adap_ids = mlxreg_lc_default_channels,
		.num_adaps = ARRAY_SIZE(mlxreg_lc_default_channels),
		.sel_reg_addr = MLXREG_LC_CHANNEL_I2C_REG,
		.reg_size = 2,
	},
};

/* Defaul mux board info. */
static struct i2c_board_info mlxreg_lc_default_mux_brdinfo = {
	.type = "mlxcpld-mux",
};

/* Line card default auxiliary power static devices. */
static struct i2c_board_info mlxreg_lc_default_aux_power_devices[] = {
	{
		I2C_BOARD_INFO("24c32", 0x51),
	},
	{
		I2C_BOARD_INFO("24c32", 0x51),
	},
};

/* Line card default auxiliary power board info. */
static struct mlxreg_hotplug_device mlxreg_lc_default_aux_power_brdinfo[] = {
	{
		.brdinfo = &mlxreg_lc_default_aux_power_devices[0],
		.nr = 7,
	},
	{
		.brdinfo = &mlxreg_lc_default_aux_power_devices[1],
		.nr = 8,
	},
};

/* Line card default main power static devices. */
static struct i2c_board_info mlxreg_lc_default_main_power_devices[] = {
	{
		I2C_BOARD_INFO("xdpe12284", 0x62),
	},
	{
		I2C_BOARD_INFO("xdpe12284", 0x64),
	},
	{
		I2C_BOARD_INFO("max11603", 0x6d),
	},
	{
		I2C_BOARD_INFO("lm25066", 0x15),
	},
};

/* Line card default main power board info. */
static struct mlxreg_hotplug_device mlxreg_lc_default_main_power_brdinfo[] = {
	{
		.brdinfo = &mlxreg_lc_default_main_power_devices[0],
		.nr = 4,
	},
	{
		.brdinfo = &mlxreg_lc_default_main_power_devices[1],
		.nr = 4,
	},
	{
		.brdinfo = &mlxreg_lc_default_main_power_devices[2],
		.nr = 5,
	},
	{
		.brdinfo = &mlxreg_lc_default_main_power_devices[3],
		.nr = 6,
	},
};

/* LED default data. */
static struct mlxreg_core_data mlxreg_lc_default_led_data[] = {
	{
		.label = "status:green",
		.reg = MLXREG_LC_REG_LED1_OFFSET,
		.mask = GENMASK(7, 4),
	},
	{
		.label = "status:orange",
		.reg = MLXREG_LC_REG_LED1_OFFSET,
		.mask = GENMASK(7, 4),
	},
};

static struct mlxreg_core_platform_data mlxreg_lc_default_led = {
	.data = mlxreg_lc_default_led_data,
	.counter = ARRAY_SIZE(mlxreg_lc_default_led_data),
};

/* Default register access data. */
static struct mlxreg_core_data mlxreg_lc_regs_io_data[] = {
	{
		.label = "cpld1_version",
		.reg = MLXREG_LC_REG_CPLD1_VER_OFFSET,
		.bit = GENMASK(7, 0),
		.mode = 0444,
	},
	{
		.label = "cpld2_version",
		.reg = MLXREG_LC_REG_CPLD2_VER_OFFSET,
		.bit = GENMASK(7, 0),
		.mode = 0444,
	},
	{
		.label = "cpld1_pn",
		.reg = MLXREG_LC_REG_CPLD1_PN_OFFSET,
		.bit = GENMASK(15, 0),
		.mode = 0444,
		.regnum = 2,
	},
	{
		.label = "cpld2_pn",
		.reg = MLXREG_LC_REG_CPLD2_PN_OFFSET,
		.bit = GENMASK(15, 0),
		.mode = 0444,
		.regnum = 2,
	},
	{
		.label = "cpld1_version_min",
		.reg = MLXREG_LC_REG_CPLD1_MVER_OFFSET,
		.bit = GENMASK(7, 0),
		.mode = 0444,
	},
	{
		.label = "cpld2_version_min",
		.reg = MLXREG_LC_REG_CPLD2_MVER_OFFSET,
		.bit = GENMASK(7, 0),
		.mode = 0444,
	},
	{
		.label = "reset_fpga_not_done",
		.reg = MLXREG_LC_REG_RESET_CAUSE_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(1),
		.mode = 0444,
	},
	{
		.label = "reset_aux_pwr_or_ref",
		.reg = MLXREG_LC_REG_RESET_CAUSE_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(2),
		.mode = 0444,
	},
	{
		.label = "reset_dc_dc_pwr_fail",
		.reg = MLXREG_LC_REG_RESET_CAUSE_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(3),
		.mode = 0444,
	},
	{
		.label = "reset_from_chassis",
		.reg = MLXREG_LC_REG_RESET_CAUSE_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(4),
		.mode = 0444,
	},
	{
		.label = "reset_pwr_off_from_chassis",
		.reg = MLXREG_LC_REG_RESET_CAUSE_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(5),
		.mode = 0444,
	},
	{
		.label = "reset_line_card",
		.reg = MLXREG_LC_REG_RESET_CAUSE_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(6),
		.mode = 0444,
	},
	{
		.label = "lc_pwr_en",
		.reg = MLXREG_LC_REG_RESET_CAUSE_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(7),
		.mode = 0444,
	},
	{
		.label = "cpld_upgrade_en",
		.reg = MLXREG_LC_REG_FIELD_UPGRADE,
		.mask = GENMASK(7, 0) & ~BIT(0),
		.mode = 0644,
	},
	{
		.label = "fpga_upgrade_en",
		.reg = MLXREG_LC_REG_FIELD_UPGRADE,
		.mask = GENMASK(7, 0) & ~BIT(1),
		.mode = 0644,
	},
	{
		.label = "qsfp_pwr_en",
		.reg = MLXREG_LC_REG_GP0_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(0),
		.mode = 0644,
	},
	{
		.label = "vpd_wp",
		.reg = MLXREG_LC_REG_GP0_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(3),
		.mode = 0644,
	},
	{
		.label = "ini_wp",
		.reg = MLXREG_LC_REG_GP0_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(4),
		.mode = 0644,
	},
	{
		.label = "agb_spi_burn_en",
		.reg = MLXREG_LC_REG_GP0_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(5),
		.mode = 0644,
	},
	{
		.label = "fpga_spi_burn_en",
		.reg = MLXREG_LC_REG_GP0_OFFSET,
		.mask = GENMASK(7, 0) & ~BIT(6),
		.mode = 0644,
	},
	{
		.label = "max_power",
		.reg = MLXREG_LC_REG_MAX_POWER_OFFSET,
		.bit = GENMASK(15, 0),
		.mode = 0444,
		.regnum = 2,
	},
	{
		.label = "config",
		.reg = MLXREG_LC_REG_CONFIG_OFFSET,
		.bit = GENMASK(15, 0),
		.mode = 0444,
		.regnum = 2,
	},
};

static struct mlxreg_core_platform_data mlxreg_lc_regs_io = {
	.data = mlxreg_lc_regs_io_data,
	.counter = ARRAY_SIZE(mlxreg_lc_regs_io_data),
};

static int mlxreg_lc_create_static_devices(struct mlxreg_lc *mlxreg_lc, struct mlxreg_hotplug_device *devs, int size)
{
	struct mlxreg_hotplug_device *dev = devs;
	int i;

	/* Create static I2C device feeding by auxiliary power. */
	for (i = 0; i < size; i++, dev++) {
		dev->adapter = i2c_get_adapter(dev->nr);
		if (!dev->adapter) {
			dev_err(mlxreg_lc->dev, "Failed to get adapter for bus %d\n", dev->nr);
			goto fail_create_static_devices;
		}
		dev->client = i2c_new_device(dev->adapter, dev->brdinfo);
		if (IS_ERR(dev->client)) {
			dev_err(mlxreg_lc->dev, "Failed to create client %s at bus %d at addr 0x%02x\n",
				dev->brdinfo->type, dev->nr, dev->brdinfo->addr);

			i2c_put_adapter(dev->adapter);
			dev->adapter = NULL;
			goto fail_create_static_devices;
		}
	}

	return 0;

fail_create_static_devices:
	while (--i >= 0) {
		dev = devs + i;
		i2c_unregister_device(dev->client);
		dev->client = NULL;
		i2c_put_adapter(dev->adapter);
		dev->adapter = NULL;
	}
	return IS_ERR(dev->client);
}

static void mlxreg_lc_destroy_static_devices(struct mlxreg_lc *mlxreg_lc, struct mlxreg_hotplug_device *devs, int size)
{
	struct mlxreg_hotplug_device *dev = devs;
	int i;

	/* Destroy static I2C device feeding by auxiliary power. */
	for (i = 0; i < size; i++, dev++) {
		if (dev->client) {
			i2c_unregister_device(dev->client);
			dev->client = NULL;
		}
		if (dev->adapter) {
			i2c_put_adapter(dev->adapter);
			dev->adapter = NULL;
		}
	}
}

static int mlxreg_lc_powered_secured_init(struct mlxplat_notifier_info *info)
{
	struct mlxreg_lc *mlxreg_lc;

	list_for_each_entry(mlxreg_lc, &mlxreg_lc_list.list, list) {
		/* Create static I2C device feeding by main power. */
		if (mlxreg_lc->topo_id == info->topo_id)
			return mlxreg_lc_create_static_devices(mlxreg_lc, mlxreg_lc->aux_devs,
							       mlxreg_lc->main_devs_num);
	}

	return -ENODEV;
}

static void mlxreg_lc_powered_secured_exit(struct mlxplat_notifier_info *info)
{
	struct mlxreg_lc *mlxreg_lc;

	list_for_each_entry(mlxreg_lc, &mlxreg_lc_list.list, list) {
		/* Destroy static I2C device feeding by main power. */
		if (mlxreg_lc->topo_id == info->topo_id)
			return mlxreg_lc_destroy_static_devices(mlxreg_lc, mlxreg_lc->aux_devs,
								mlxreg_lc->main_devs_num);
	}
}

/* Called under rcu_read_lock() */
static int mlxreg_lc_event(struct notifier_block *unused, unsigned long event, void *data)
{
	struct mlxplat_notifier_info *info = data;
	int err = NOTIFY_DONE;

	switch (event) {
	case MLXREG_HOTPLUG_LC_SECURED:
		if (info->action)
			err = mlxreg_lc_powered_secured_init(info);
		else
			mlxreg_lc_powered_secured_exit(info);
		break;
	default:
		break;
	}

	return err;
}

struct notifier_block mlxreg_lc_notifier_block = {
	.notifier_call = mlxreg_lc_event,
};

static int mlxreg_lc_sn4800_c16_config_init(struct mlxreg_lc *mlxreg_lc, void *regmap, struct mlxreg_core_data *data)
{
	struct device *dev = &data->hpdev.client->dev;
	int err;

	/* Set line card configuration according to the type. */
	mlxreg_lc->mux_data = mlxreg_lc_default_mux_data;
	mlxreg_lc->regs_io_data = &mlxreg_lc_regs_io;
	mlxreg_lc->led_data = &mlxreg_lc_default_led;
	mlxreg_lc->aux_devs = mlxreg_lc_default_aux_power_brdinfo;
	mlxreg_lc->main_devs = mlxreg_lc_default_main_power_brdinfo;
	mlxreg_lc->mux_brdinfo = &mlxreg_lc_default_mux_brdinfo;

	mlxreg_lc->aux_devs = devm_kmemdup(dev, mlxreg_lc_default_aux_power_brdinfo,
					   sizeof(mlxreg_lc_default_aux_power_brdinfo), GFP_KERNEL);
	if (!mlxreg_lc->aux_devs)
		return -ENOMEM;
	mlxreg_lc->aux_devs_num = ARRAY_SIZE(mlxreg_lc_default_aux_power_brdinfo);

	mlxreg_lc->main_devs = devm_kmemdup(dev, mlxreg_lc_default_main_power_brdinfo,
					    sizeof(mlxreg_lc_default_main_power_brdinfo), GFP_KERNEL);
	if (!mlxreg_lc->main_devs)
		return -ENOMEM;
	mlxreg_lc->main_devs_num = ARRAY_SIZE(mlxreg_lc_default_main_power_brdinfo);

	return err;
}

static int mlxreg_lc_config_init(struct mlxreg_lc *mlxreg_lc, void *regmap, struct mlxreg_core_data *data)
{
	struct device *dev = &data->hpdev.client->dev;
	int lsb, regval, err;

	/* Validate line card type. */
	err = regmap_read(regmap, MLXREG_LC_REG_CONFIG_OFFSET, &lsb);
	err = (!err) ? regmap_read(regmap, MLXREG_LC_REG_CONFIG_OFFSET, &regval) : err;
	if (err)
		return err;
	regval = (regval & GENMASK(7, 0)) << 8 | (lsb & GENMASK(7, 0));

	switch (regval) {
	case MLXREG_LC_SN4800_C16:
		err = mlxreg_lc_sn4800_c16_config_init(mlxreg_lc, regmap, data);
		if (err)
			return err;
		break;
	default:
		return -ENODEV;
	}

	/* Create mux infrastructure. */
	mlxreg_lc->mux_data->base_nr = MLXREG_LC_SET_BASE_NR(data->slot);
	mlxreg_lc->mux_brdinfo->platform_data = mlxreg_lc->mux_data;
	mlxreg_lc->mux = i2c_new_device(data->hpdev.adapter, mlxreg_lc->mux_brdinfo);
	if (IS_ERR(mlxreg_lc->mux))
		return PTR_ERR(mlxreg_lc->mux);

	/* Register IO access driver. */
	if (mlxreg_lc->regs_io_data) {
		mlxreg_lc->regs_io_data->regmap = regmap;
		mlxreg_lc->io_regs = platform_device_register_resndata(dev, "mlxreg-io", data->hpdev.nr, NULL, 0,
								       mlxreg_lc->regs_io_data, sizeof(*mlxreg_lc->regs_io_data));
		if (IS_ERR(mlxreg_lc->io_regs)) {
			err = PTR_ERR(mlxreg_lc->io_regs);
			goto fail_register_io;
		}
	}

	/* Register LED driver. */
	if (mlxreg_lc->led_data) {
		mlxreg_lc->led_data->regmap = regmap;
		mlxreg_lc->led = platform_device_register_resndata(dev, "leds-mlxreg", data->hpdev.nr, NULL, 0,
								   mlxreg_lc->led_data, sizeof(*mlxreg_lc->led_data));
		if (IS_ERR(mlxreg_lc->led)) {
			err = PTR_ERR(mlxreg_lc->led);
			goto fail_register_led;
		}
	}

	return 0;

fail_register_led:
	if (mlxreg_lc->io_regs)
		platform_device_unregister(mlxreg_lc->io_regs);
fail_register_io:
	if (mlxreg_lc->mux)
		i2c_unregister_device(mlxreg_lc->mux);
	return err;
}

static void mlxreg_lc_config_exit(struct mlxreg_lc *mlxreg_lc)
{
	/* Unregister LED driver. */
	if (mlxreg_lc->led)
		platform_device_unregister(mlxreg_lc->led);
	/* Unregister IO access driver. */
	if (mlxreg_lc->io_regs)
		platform_device_unregister(mlxreg_lc->io_regs);
	/* Create mux infrastructure. */
	if (mlxreg_lc->mux)
		i2c_unregister_device(mlxreg_lc->mux);
}

static int mlxreg_lc_probe(struct platform_device *pdev)
{
	struct mlxreg_core_hotplug_platform_data *pdata;
	struct i2c_adapter *deferred_adap;
	struct mlxreg_core_data *data;
	struct mlxreg_lc *mlxreg_lc;
	void *regmap;
	int i, err;

	pdata = dev_get_platdata(&pdev->dev);
	if (!pdata)
		return -EINVAL;

	mlxreg_lc = devm_kzalloc(&pdev->dev, sizeof(*mlxreg_lc), GFP_KERNEL);
	if (!mlxreg_lc)
		return -ENOMEM;

	data = pdata->items->data;
	data->hpdev.adapter = i2c_get_adapter(data->hpdev.nr);
	if (!data->hpdev.adapter) {
		dev_err(&pdev->dev, "Failed to get adapter for bus %d\n", data->hpdev.nr);
		return -EFAULT;
	}

	data->hpdev.client = i2c_new_device(data->hpdev.adapter, data->hpdev.brdinfo);
	if (IS_ERR(data->hpdev.client)) {
		dev_err(&pdev->dev, "Failed to create client %s at bus %d at addr 0x%02x\n",
			data->hpdev.brdinfo->type, data->hpdev.nr, data->hpdev.brdinfo->addr);

		i2c_put_adapter(data->hpdev.adapter);
		data->hpdev.adapter = NULL;
		return PTR_ERR(data->hpdev.client);
	}

	regmap = devm_regmap_init_i2c(data->hpdev.client, &mlxreg_lc_regmap_conf);
	if (IS_ERR(regmap)) {
		err = PTR_ERR(regmap);
		goto mlxreg_lc_probe_fail;
	}

	/* Set default registers. */
	for (i = 0; i < mlxreg_lc_regmap_conf.num_reg_defaults; i++) {
		err = regmap_write(regmap, mlxreg_lc_regmap_default[i].reg,
				   mlxreg_lc_regmap_default[i].def);
		if (err)
			goto mlxreg_lc_probe_fail;
	}

	/* Sync registers with hardware. */
	regcache_mark_dirty(regmap);
	err = regcache_sync(regmap);
	if (err)
		goto mlxreg_lc_probe_fail;

	/* Configure line card. */
	err = mlxreg_lc_config_init(mlxreg_lc, regmap, data);
	if (err)
		goto mlxreg_lc_probe_fail;

	/* Defer probing if the necessary adapter is not configured yet. */
	deferred_adap = i2c_get_adapter(pdata->deferred_nr);
	if (!deferred_adap)
		return -EPROBE_DEFER;
	i2c_put_adapter(deferred_adap);

	/* Create static I2C device feeding by auxiliary power. */
	err = mlxreg_lc_create_static_devices(mlxreg_lc, mlxreg_lc->aux_devs, mlxreg_lc->aux_devs_num);
	if (err)
		goto mlxreg_lc_probe_fail;

	platform_set_drvdata(pdev, mlxreg_lc);
	mlxreg_lc->topo_id = rol32(data->hpdev.nr, 16) | data->hpdev.brdinfo->addr;
	list_add(&mlxreg_lc->list, &mlxreg_lc_list.list);

	return err;

mlxreg_lc_probe_fail:
	i2c_put_adapter(data->hpdev.adapter);
	return err;
}

static int mlxreg_lc_remove(struct platform_device *pdev)
{
	struct mlxreg_lc *mlxreg_lc = platform_get_drvdata(pdev);
	struct mlxreg_core_hotplug_platform_data *pdata;
	struct mlxreg_core_data *data;

	pdata = dev_get_platdata(&pdev->dev);
	if (!pdata)
		return -EINVAL;

	if (!list_empty(&mlxreg_lc_list.list))
		list_del_rcu(&mlxreg_lc->list);

	/* Destroy static I2C device feeding by main power. */
	mlxreg_lc_destroy_static_devices(mlxreg_lc, mlxreg_lc->aux_devs, mlxreg_lc->main_devs_num);
	/* Destroy static I2C device feeding by auxiliary power. */
	mlxreg_lc_destroy_static_devices(mlxreg_lc, mlxreg_lc->aux_devs, mlxreg_lc->aux_devs_num);
	/* Unregister underlying drivers. */
	mlxreg_lc_config_exit(mlxreg_lc);
	data = pdata->items->data;
	if (data->hpdev.client) {
		i2c_unregister_device(data->hpdev.client);
		data->hpdev.client = NULL;
		i2c_put_adapter(data->hpdev.adapter);
		data->hpdev.adapter = NULL;
	}

	return 0;
}

static const struct of_device_id mlxreg_lc_of_match[] = {
	{ .compatible = "mlxreg-lc", },
	{},
};
MODULE_DEVICE_TABLE(of, mlxreg_lc_of_match);

static struct platform_driver mlxreg_lc_driver = {
	.probe = mlxreg_lc_probe,
	.remove = mlxreg_lc_remove,
	.driver = {
		.name = "mlxreg-lc",
		.of_match_table = of_match_ptr(mlxreg_lc_of_match),
	},
};

static int __init mlxreg_lc_init(void)
{
	int err;

	err = platform_driver_register(&mlxreg_lc_driver);
	if (!err)
		return err;
	err = mlxplat_blk_notifier_register(&mlxreg_lc_notifier_block);
	if (!err)
		goto mlxreg_lc_init_failed;
	INIT_LIST_HEAD(&mlxreg_lc_list.list);

	return err;

mlxreg_lc_init_failed:
	platform_driver_unregister(&mlxreg_lc_driver);
	return err;
}

static void __exit mlxreg_lc_exit(void)
{
	mlxplat_blk_notifier_unregister(&mlxreg_lc_notifier_block);
	platform_driver_unregister(&mlxreg_lc_driver);
}

module_init(mlxreg_lc_init);
module_exit(mlxreg_lc_exit);

MODULE_DESCRIPTION("Mellanox line cards platform driver");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("platform:mlxreg-lc");
