// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/*
 * Mellanox i2c mux driver
 *
 * Copyright (C) 2016-2020 Mellanox Technologies
 */

#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/i2c-mux.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_data/mlxcpld.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

/* mlxcpld_mux - mux control structure:
 * @last_chan - last register value
 * @client - I2C device client
 * @pdata: platform data
 * @sel_buf: I2C message buffer for mux select 16 bits transactions
 */
struct mlxcpld_mux {
	int last_chan;
	struct i2c_client *client;
	struct mlxcpld_mux_plat_data pdata;
	u8 sel_buf[3];
};

/* MUX logic description.
 * Driver can support different mux control logic, according to CPLD
 * implementation.
 *
 * Connectivity schema.
 *
 * i2c-mlxcpld                                 Digital               Analog
 * driver
 * *--------*                                 * -> mux1 (virt bus2) -> mux -> |
 * | I2CLPC | i2c physical                    * -> mux2 (virt bus3) -> mux -> |
 * | bridge | bus 1                 *---------*                               |
 * | logic  |---------------------> * mux reg *                               |
 * | in CPLD|                       *---------*                               |
 * *--------*   i2c-mux-mlxpcld          ^    * -> muxn (virt busn) -> mux -> |
 *     |        driver                   |                                    |
 *     |        *---------------*        |                              Devices
 *     |        * CPLD (i2c bus)* select |
 *     |        * registers for *--------*
 *     |        * mux selection * deselect
 *     |        *---------------*
 *     |                 |
 * <-------->     <----------->
 * i2c cntrl      Board cntrl reg
 * reg space      space (mux select,
 *                IO, LED, WD, info)
 *
 */

/* Write to mux register. Don't use i2c_transfer() and i2c_smbus_xfer()
 * for this as they will try to lock adapter a second time.
 */
static int mlxcpld_mux_reg_write(struct i2c_adapter *adap,
				 struct mlxcpld_mux *mux, int chan)
{
	struct i2c_client *client = mux->client;
	union i2c_smbus_data data;
	struct i2c_msg msg;

	switch (mux->pdata.reg_size) {
	case 1:
		data.byte = (chan < 0) ? 0 : chan;
		return __i2c_smbus_xfer(adap, client->addr, client->flags,
					I2C_SMBUS_WRITE,
					mux->pdata.sel_reg_addr,
					I2C_SMBUS_BYTE_DATA, &data);
	case 2:
		mux->sel_buf[mux->pdata.reg_size] = (chan < 0) ? 0 :
						    mux->pdata.adap_ids[chan];
		msg.addr = client->addr;
		msg.buf = mux->sel_buf;
		msg.len = mux->pdata.reg_size + 1;
		msg.flags = 0;
		return __i2c_transfer(adap, &msg, 1);
	default:
		return -EINVAL;
	}
}

static int mlxcpld_mux_select_chan(struct i2c_mux_core *muxc, u32 chan)
{
	struct mlxcpld_mux *mux = i2c_mux_priv(muxc);
	int err = 0;

	/* Only select the channel if its different from the last channel */
	if (mux->last_chan != chan) {
		err = mlxcpld_mux_reg_write(muxc->parent, mux, chan);
		mux->last_chan = err < 0 ? 0 : chan;
	}

	return err;
}

static int mlxcpld_mux_deselect(struct i2c_mux_core *muxc, u32 chan)
{
	struct mlxcpld_mux *mux = i2c_mux_priv(muxc);

	/* Deselect active channel */
	mux->last_chan = -1;

	return mlxcpld_mux_reg_write(muxc->parent, mux, mux->last_chan);
}

/* Probe/reomove functions */
static int mlxcpld_mux_probe(struct platform_device *pdev)
{
	struct mlxcpld_mux_plat_data *pdata = dev_get_platdata(&pdev->dev);
	struct i2c_client *client = to_i2c_client(pdev->dev.parent);
	struct i2c_mux_core *muxc;
	int num, force;
	struct mlxcpld_mux *data;
	u16 sel_reg_addr = 0;
	u32 func;
	int err;

	if (!pdata)
		return -EINVAL;

	switch (pdata->reg_size) {
	case 1:
		func = I2C_FUNC_SMBUS_WRITE_BYTE_DATA;
		break;
	case 2:
		func = I2C_FUNC_SMBUS_WRITE_WORD_DATA;
		sel_reg_addr = cpu_to_be16(pdata->sel_reg_addr);
		break;
	default:
		return -EINVAL;
	}

	if (!i2c_check_functionality(client->adapter, func))
		return -ENODEV;

	muxc = i2c_mux_alloc(client->adapter, &pdev->dev, pdata->num_adaps,
			     sizeof(*data), 0, mlxcpld_mux_select_chan,
			     mlxcpld_mux_deselect);
	if (!muxc)
		return -ENOMEM;

	platform_set_drvdata(pdev, muxc);
	data = i2c_mux_priv(muxc);
	data->client = client;
	memcpy(&data->pdata, pdata, sizeof(*pdata));
	/* Save mux select address for 16 bits transaction size. */
	memcpy(data->sel_buf, &sel_reg_addr, 2);
	data->last_chan = 0; /* force the first selection */

	/* Create an adapter for each channel. */
	for (num = 0; num < pdata->num_adaps; num++) {
		force = pdata->base_nr ? (pdata->base_nr +
			pdata->adap_ids[num]) : pdata->adap_ids[num];
		err = i2c_mux_add_adapter(muxc, force, num, 0);
		if (err)
			goto virt_reg_failed;
	}

	return 0;

virt_reg_failed:
	i2c_mux_del_adapters(muxc);
	return err;
}

static int mlxcpld_mux_remove(struct platform_device *pdev)
{
	struct i2c_mux_core *muxc = platform_get_drvdata(pdev);

	i2c_mux_del_adapters(muxc);
	return 0;
}

static struct platform_driver mlxcpld_mux_driver = {
	.driver = {
		.name = "i2c-mux-mlxcpld",
	},
	.probe = mlxcpld_mux_probe,
	.remove = mlxcpld_mux_remove,
};

module_platform_driver(mlxcpld_mux_driver);

MODULE_AUTHOR("Michael Shych (michaels@mellanox.com)");
MODULE_DESCRIPTION("Mellanox I2C-CPLD-MUX driver");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("platform:i2c-mux-mlxcpld");
