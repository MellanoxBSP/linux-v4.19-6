// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2020 Mellanox Technologies. All rights reserved */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_data/mlxreg.h>
#include <linux/regmap.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#include "core.h"
#include "platform.h"

#define MLXSW_PLAT_HOTPLUG_EVENT_OFF	1
#define MLXSW_PLAT_HOTPLUG_MASK_OFF	2

static void mlxsw_plat_work_handler(struct work_struct *work)
{
	struct mlxreg_core_hotplug_platform_data *mlxsw_pdata;
	struct mlxsw_plat *mlxsw_plat;
	struct mlxreg_core_item *item;
	struct mlxreg_core_data *data;
	unsigned long asserted;
	unsigned long flags;
	u32 regval, bit;
	int err;

	mlxsw_plat = container_of(work, struct mlxsw_plat, dwork_irq.work);
	mlxsw_pdata = mlxsw_plat->bus_info->dev->platform_data;
	item = mlxsw_pdata->items;
	/* Mask group event. */
	err = regmap_write(mlxsw_pdata->regmap, item->reg +
			   MLXSW_PLAT_HOTPLUG_MASK_OFF, 0);
	if (err)
		goto out;

	/* Read status. */
	err = regmap_read(mlxsw_pdata->regmap, item->reg, &regval);
	if (err)
		goto out;

	/* Set asserted bits and save last status. */
	regval &= item->mask;
	asserted = item->cache ^ regval;
	item->cache = regval;

	for_each_set_bit(bit, &asserted, item->count) {
		data = item->data + bit;
		if (regval & BIT(bit)) {
			/* Call here new API (slot == bit):
			 * mlxsw_core_lc_init(mlxsw_core, mlxsw_bus_info, bit);
			 * (1) mlxsw_m_ports_create(..., bit);
			 * (2) mlxsw_hwmon_init(..., bit);
			 * (3) mlxsw_thermal_init(..., bit);
			 * (4) Set bit in new field of lc_bitmap of mlxsw_core;
			 */
		} else {
			/* Call here: (slot == bit):
			 * mlxsw_core_lc_fini(mlxsw_core, mlxsw_bus_info, bit);
			 * (1) Clear bit in field of lc_bitmap of mlxsw_core;
			 * (2) mlxsw_thermal_fini(..., bit);
			 * (3) mlxsw_hwmon_fini(..., bit) ;
			 * (4) mlxsw_m_ports_remove(..., bit);
			 */
		}
	}

	/* Acknowledge event. */
	err = regmap_write(mlxsw_pdata->regmap, item->reg +
			    MLXSW_PLAT_HOTPLUG_EVENT_OFF, 0);
	if (err)
		goto out;

	/* Unmask event. */
	err = regmap_write(mlxsw_pdata->regmap, item->reg +
			   MLXSW_PLAT_HOTPLUG_MASK_OFF, item->mask);
	if (err)
		goto out;

	spin_lock_irqsave(&mlxsw_plat->lock, flags);

	/* It is possible, that some signals have been inserted, while
	 * interrupt has been masked by mlxsw_plat_work_handler. In this
	 * case such signals could be missed. In order to handle these signals
	 * delayed work is canceled and work task re-scheduled for immediate
	 * execution. It allows to handle missed signals, if any. In other case
	 * work handler just validates that no new signals have been received
	 * during masking.
	 */
	cancel_delayed_work(&mlxsw_plat->dwork_irq);
	schedule_delayed_work(&mlxsw_plat->dwork_irq, 0);

	spin_unlock_irqrestore(&mlxsw_plat->lock, flags);
out:
	if (err)
		dev_err(mlxsw_plat->bus_info->dev, "Failed to complete workqueue.\n");
}

static irqreturn_t mlxsw_plat_irq_handler(int irq, void *dev)
{
	struct mlxsw_plat *mlxsw_plat = (struct mlxsw_plat *)dev;

	/* Schedule work task for immediate execution.*/
	schedule_delayed_work(&mlxsw_plat->dwork_irq, 0);

	return IRQ_HANDLED;
}

static int mlxsw_plat_irq_handler_init(struct mlxsw_plat *mlxsw_plat)
{
	struct mlxreg_core_hotplug_platform_data *mlxsw_pdata;
	struct mlxreg_core_item *item;
	int i;
	int err;

	mlxsw_pdata = mlxsw_plat->bus_info->dev->platform_data;
	err = devm_request_irq(mlxsw_plat->bus_info->dev, mlxsw_pdata->irq,
			       mlxsw_plat_irq_handler, IRQF_TRIGGER_FALLING |
			       IRQF_SHARED, "mlxsw-minimal", mlxsw_plat);
	if (err) {
		dev_err(mlxsw_plat->bus_info->dev, "Failed to request irq: %d\n",
			err);
		return err;
	}

	disable_irq(mlxsw_pdata->irq);
	spin_lock_init(&mlxsw_plat->lock);
	INIT_DELAYED_WORK(&mlxsw_plat->dwork_irq, mlxsw_plat_work_handler);

	/* Clear group event register. */
	item = mlxsw_pdata->items;
	for (i = 0; i < mlxsw_pdata->counter; i++, item++) {
		err = regmap_write(mlxsw_pdata->regmap, item->reg +
				   MLXSW_PLAT_HOTPLUG_EVENT_OFF, 0);
		if (err)
			return err;
	}

	/* Invoke work handler for initializing hotplug devices setting;
	 * set interrupts mask configuration, insert devices, which could
	 * already be configured at this point (for example, in case of fast
	 * boot, performed through kexec).
	 */
	mlxsw_plat_work_handler(&mlxsw_plat->dwork_irq.work);

	return 0;
}

static void
mlxsw_plat_irq_handler_fini(struct mlxsw_plat *mlxsw_plat)
{
	struct mlxreg_core_hotplug_platform_data *mlxsw_pdata;
	struct mlxreg_core_item *item;
	int i;

	mlxsw_pdata = mlxsw_plat->bus_info->dev->platform_data;
	/* Clear interrupts setup. */
	item = mlxsw_pdata->items;
	for (i = 0; i < mlxsw_pdata->counter; i++, item++) {
		/* Mask group event. */
		regmap_write(mlxsw_pdata->regmap, item->reg +
			     MLXSW_PLAT_HOTPLUG_MASK_OFF, 0);
		/* Clear group event. */
		regmap_write(mlxsw_pdata->regmap, item->reg +
			     MLXSW_PLAT_HOTPLUG_EVENT_OFF, 0);

		/* Remove all the attached devices in group. */
		/* mlxsw_core_lc_all_fini(mlxsw_core, mlxsw_bus_info); */
	}

	devm_free_irq(mlxsw_plat->bus_info->dev, mlxsw_pdata->irq, mlxsw_plat);
}

int mlxsw_plat_init(struct mlxsw_core *core,
		    const struct mlxsw_bus_info *mlxsw_bus_info,
		    struct mlxsw_plat **p_mlxsw_plat)
{
	struct mlxsw_plat *mlxsw_plat;
	int err;

	mlxsw_plat = devm_kzalloc(mlxsw_bus_info->dev, sizeof(*mlxsw_plat),
				  GFP_KERNEL);
	if (!mlxsw_plat)
		return -ENOMEM;

	mlxsw_plat->core = core;
	mlxsw_plat->bus_info = mlxsw_bus_info;

	err = mlxsw_plat_irq_handler_init(mlxsw_plat);
	if (err)
		return 0;

	*p_mlxsw_plat = mlxsw_plat;

	return 0;
}

void mlxsw_plat_fini(struct mlxsw_plat *mlxsw_plat)
{
	mlxsw_plat_irq_handler_fini(mlxsw_plat);
	devm_kfree(mlxsw_plat->bus_info->dev, mlxsw_plat);
}
