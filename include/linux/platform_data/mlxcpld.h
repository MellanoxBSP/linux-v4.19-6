/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/*
 * Mellanox I2C multiplexer support in CPLD
 *
 * Copyright (C) 2016-2020 Mellanox Technologies
 */

#ifndef _LINUX_I2C_MLXCPLD_H
#define _LINUX_I2C_MLXCPLD_H

/* Platform data for the CPLD I2C multiplexers */

/* mlxcpld_mux_plat_data - per mux data, used with i2c_register_board_info
 * @base_nr: base I2C bus number to number adapters from or zero for setting
 *	     to adap_ids vector
 * @adap_ids - adapter array
 * @num_adaps - number of adapters
 * @sel_reg_addr - mux select register offset in CPLD space
 * @reg_size: register size in bytes (default 0 - 1 byte, I2C_SMBUS_BYTE_DATA,
 *	      I2C_SMBUS_WORD_DATA - 1 and 2 bytes)
 */
struct mlxcpld_mux_plat_data {
	int base_nr;
	int *adap_ids;
	int num_adaps;
	int sel_reg_addr;
	u8 reg_size;
};

#endif /* _LINUX_I2C_MLXCPLD_H */
