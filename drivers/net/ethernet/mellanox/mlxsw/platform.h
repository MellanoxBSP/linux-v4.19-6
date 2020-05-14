/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2020 Mellanox Technologies. All rights reserved */

#ifndef _MLXSW_PLATFORM_H
#define _MLXSW_PLATFORM_H

struct mlxsw_plat {
	struct mlxsw_core *core;
	const struct mlxsw_bus_info *bus_info;
	struct delayed_work dwork_irq;
	spinlock_t lock; /* sync with interrupt */
};

int mlxsw_plat_init(struct mlxsw_core *mlxsw_core,
		    const struct mlxsw_bus_info *mlxsw_bus_info,
		    struct mlxsw_plat **p_mlxsw_plat);

void mlxsw_plat_fini(struct mlxsw_plat *mlxsw_plat);

#endif
