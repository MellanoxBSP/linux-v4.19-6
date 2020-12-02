// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2018 Mellanox Technologies. All rights reserved */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/sfp.h>

#include "core.h"
#include "core_env.h"
#include "item.h"
#include "reg.h"

#define MLXSW_REG_MCIA_EEPROM_FLAT_MEMORY	BIT(7)
#define MLXSW_ENV_CMIS_PAGE3_PRESENSE_OFF	0x8e
#define MLXSW_ENV_CMIS_PAGE3_PRESENSE_BIT	BIT(2)
#define MLXSW_ENV_CMIS_BANKS_PRESENSE_BITS	GENMASK(1, 0)
#define MLXSW_ENV_CMIS_OPT_PAGE			0x03
#define MLXSW_ENV_CMIS_CH_CTRL_MASK_PAGE	0x10

/* Enumerator for indication which banks are implemented for pages 16, 17.
 * "Common Management Interface Specification Rev 4.0"
 * Table 8-28 "Implemented Management Interface Features Advertising".
 */
enum mlxsw_env_cmis_banks {
	MLXSW_ENV_CMIS_BANKS1	= 0x00, /* Only bank 0 is implemented. */
	MLXSW_ENV_CMIS_BANKS2	= 0x01, /* Banks 0, 1 are implemented. */
	MLXSW_ENV_CMIS_BANKS4	= 0x10, /* Banks 0, 1, 2, 3 are implemented.*/
};

static int mlxsw_env_validate_cable_ident(struct mlxsw_core *core, u8 slot_index, int id,
					  bool *qsfp, bool *cmis)
{
	char eeprom_tmp[MLXSW_REG_MCIA_EEPROM_SIZE];
	char mcia_pl[MLXSW_REG_MCIA_LEN];
	u8 ident;
	int err;

	mlxsw_reg_mcia_pack(mcia_pl, slot_index, id, 0,
			    MLXSW_REG_MCIA_PAGE0_LO_OFF, 0, 0,
			    1, MLXSW_REG_MCIA_I2C_ADDR_LOW);
	err = mlxsw_reg_query(core, MLXSW_REG(mcia), mcia_pl);
	if (err)
		return err;
	mlxsw_reg_mcia_eeprom_memcpy_from(mcia_pl, eeprom_tmp);
	ident = eeprom_tmp[0];
	*cmis = false;
	switch (ident) {
	case MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_SFP: /* fall-through */
	case MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_SFP_DD:
		*qsfp = false;
		break;
	case MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_QSFP: /* fall-through */
	case MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_QSFP_PLUS: /* fall-through */
	case MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_QSFP28:
		*qsfp = true;
		break;
	case MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_QSFP_DD: /* fall-through */
	case MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_QSFP_PLUS_CMIS:
		*qsfp = true;
		*cmis = true;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void
mlxsw_env_cmis_opt_page_banks_map(u8 opt_page, u8 opt_banks, u8 *page,
				  u8 *bank)
{
	u8 p, s;

	/* If sequential page number is 3 and physical page 3 is implemented -
	 * do not modify page.
	 */
	if (*page == MLXSW_ENV_CMIS_OPT_PAGE && opt_page)
		return;

	/* Page sequential number conversion to physical page and bank
	 * for pages 16 and 17 are following the next logic:
	 *	|---------------|---------------|---------------|
	 *	|number of	|seq num -> (page, bank)	|
	 *	|banks		|-------------------------------|
	 *	|		|page 3		|page 3 not	|
	 *	|		|implemented	|implemented	|
	 *	|---------------|---------------|---------------|
	 *	|	1	|4 -> (16, 0)	|3 -> (16, 0)	|
	 *	|		|5 -> (17, 0)	|4 -> (17, 0)	|
	 *	----------------|---------------|---------------|
	 *	|	2	|4 -> (16, 0)	|3 -> (16, 0)	|
	 *	|		|5 -> (16, 1)	|4 -> (16, 1)	|
	 *	|		|6 -> (17, 0)	|5 -> (17, 0)	|
	 *	|		|7 -> (17, 1)	|6 -> (17, 1)	|
	 *	|---------------|---------------|---------------|
	 *	|	4	|4 -> (16, 0)	|3 -> (16, 0)	|
	 *	|		|5 -> (16, 1)	|4 -> (16, 1)	|
	 *	|		|6 -> (16, 2)	|5 -> (16, 2)	|
	 *	|		|7 -> (16, 3)	|6 -> (16, 3)	|
	 *	|		|8 -> (17, 0)	|7 -> (17, 0)	|
	 *	|	`	|9 -> (17, 1)	|8 -> (17, 1)	|
	 *	|		|10-> (17, 2)	|9 -> (17, 2)	|
	 *	|		|11-> (17, 3)	|10-> (17, 3)	|
	 *	----------------|---------------|---------------|
	 *
	 * Aligned page number between configuration with and with no optional
	 * page 3 and set scale parameterto simplify mapping calculation.
	 */
	p = *page + !!opt_page;
	switch (opt_banks) {
	case 2:
		s = 6;
		break;
	case 4:
		s = 8;
		break;
	default:
		/* Could be reached only in case banks number is 1. */
		s = 4;
		break;
	}

	/* Map page and bank. */
	*page = p / s + MLXSW_ENV_CMIS_CH_CTRL_MASK_PAGE;
	*bank = (p % s)  % opt_banks;
}

static int
mlxsw_env_query_module_eeprom(struct mlxsw_core *mlxsw_core, u8 slot_index,
			      int module, u16 offset, u16 size, bool qsfp,
			      bool cmis, u8 opt_page, u8 opt_banks,
			      void *data, unsigned int *p_read_size)
{
	char eeprom_tmp[MLXSW_REG_MCIA_EEPROM_SIZE];
	char mcia_pl[MLXSW_REG_MCIA_LEN];
	u8 page = 0, bank = 0;
	u16 i2c_addr;
	int status;
	int err;

	/* MCIA register accepts buffer size <= 48. Page of size 128 should be
	 * read by chunks of size 48, 48, 32. Align the size of the last chunk
	 * to avoid reading after the end of the page.
	 */
	size = min_t(u16, size, MLXSW_REG_MCIA_EEPROM_SIZE);

	if (offset < MLXSW_REG_MCIA_EEPROM_PAGE_LENGTH &&
	    offset + size > MLXSW_REG_MCIA_EEPROM_PAGE_LENGTH)
		/* Cross pages read, read until offset 256 in low page */
		size = MLXSW_REG_MCIA_EEPROM_PAGE_LENGTH - offset;

	i2c_addr = MLXSW_REG_MCIA_I2C_ADDR_LOW;
	if (offset >= MLXSW_REG_MCIA_EEPROM_PAGE_LENGTH) {
		if (qsfp) {
			/* When reading upper pages 1, 2 and 3 the offset
			 * starts at 128. Please refer to "QSFP+ Memory Map"
			 * figure in SFF-8436 specification and to "CMIS Module
			 * Memory Map" figure in CMIS specification for
			 * graphical depiction.
			 */
			page = MLXSW_REG_MCIA_PAGE_GET(offset);
			offset -= MLXSW_REG_MCIA_EEPROM_UP_PAGE_LENGTH * page;
			if (offset + size > MLXSW_REG_MCIA_EEPROM_PAGE_LENGTH)
				size = MLXSW_REG_MCIA_EEPROM_PAGE_LENGTH - offset;
		} else {
			/* When reading upper pages 1, 2 and 3 the offset
			 * starts at 0 and I2C high address is used. Please refer
			 * refer to "Memory Organization" figure in SFF-8472
			 * specification for graphical depiction.
			 */
			i2c_addr = MLXSW_REG_MCIA_I2C_ADDR_HIGH;
			offset -= MLXSW_REG_MCIA_EEPROM_PAGE_LENGTH;
		}
	}

	/* Map buffer to correct page and banks. */
	if (cmis && page >= MLXSW_ENV_CMIS_OPT_PAGE)
		mlxsw_env_cmis_opt_page_banks_map(opt_page, opt_banks, &page,
						  &bank);
	mlxsw_reg_mcia_pack(mcia_pl, slot_index, module, 0, page, bank, offset, size,
			    i2c_addr);

	err = mlxsw_reg_query(mlxsw_core, MLXSW_REG(mcia), mcia_pl);
	if (err)
		return err;

	status = mlxsw_reg_mcia_status_get(mcia_pl);
	if (status)
		return -EIO;

	mlxsw_reg_mcia_eeprom_memcpy_from(mcia_pl, eeprom_tmp);
	memcpy(data, eeprom_tmp, size);
	*p_read_size = size;

	return 0;
}

static int
mlxsw_env_cmis_optional_get(struct mlxsw_core *mlxsw_core, u8 slot_index,
			    int module, unsigned int *size, u8 *page, u8 *banks)
{
	int options, read_size, num_banks;
	int err;

	/* Verify if implemented optional page 03h - "User EEPROM (NVRs)", and
	 * if implemented bank of pages 16 and 17, indicating through the page
	 * 01h.Refer to "Common Management Interface Specification Rev 4.0",
	 * Table 8-28 "Implemented Management Interface Features Advertising
	 * (Page 01h)".
	 */
	err = mlxsw_env_query_module_eeprom(mlxsw_core, slot_index, module,
					    MLXSW_ENV_CMIS_PAGE3_PRESENSE_OFF,
					    1, false, false, 0, 0, &options,
					    &read_size);
	if (err)
		return err;

	if (read_size < 1)
		return -EIO;

	/* Check if optional page 3 in implemented. */
	if (options & MLXSW_ENV_CMIS_PAGE3_PRESENSE_BIT) {
		if (size)
			*size += MLXSW_REG_MCIA_EEPROM_UP_PAGE_LENGTH;
		if (page)
			*page = 1;
	}

	switch (options & MLXSW_ENV_CMIS_BANKS_PRESENSE_BITS) {
	case MLXSW_ENV_CMIS_BANKS1:
		num_banks = 1;
		break;
	case MLXSW_ENV_CMIS_BANKS2:
		num_banks = 2;
		break;
	case MLXSW_ENV_CMIS_BANKS4:
		num_banks = 4;
		break;
	default:
		return -EINVAL;
	}

	if (size)
		*size += num_banks * MLXSW_REG_MCIA_EEPROM_UP_PAGE_LENGTH;
	if (banks)
		*banks = num_banks;

	return 0;
}

int mlxsw_env_module_temp_thresholds_get(struct mlxsw_core *core, u8 slot_index,
					 int module, int off, int *temp)
{
	char eeprom_tmp[MLXSW_REG_MCIA_EEPROM_SIZE];
	union {
		u8 buf[MLXSW_REG_MCIA_TH_ITEM_SIZE];
		u16 temp;
	} temp_thresh;
	char mcia_pl[MLXSW_REG_MCIA_LEN] = {0};
	char mtmp_pl[MLXSW_REG_MTMP_LEN];
	unsigned int module_temp;
	bool qsfp, cmis;
	int page;
	int err;

	mlxsw_reg_mtmp_pack(mtmp_pl, slot_index,
			    MLXSW_REG_MTMP_MODULE_INDEX_MIN + module, false,
			    false);
	err = mlxsw_reg_query(core, MLXSW_REG(mtmp), mtmp_pl);
	if (err)
		return err;
	mlxsw_reg_mtmp_unpack(mtmp_pl, &module_temp, NULL, NULL);
	if (!module_temp) {
		*temp = 0;
		return 0;
	}

	/* Read Free Side Device Temperature Thresholds from page 03h
	 * (MSB at lower byte address).
	 * Bytes:
	 * 128-129 - Temp High Alarm (SFP_TEMP_HIGH_ALARM);
	 * 130-131 - Temp Low Alarm (SFP_TEMP_LOW_ALARM);
	 * 132-133 - Temp High Warning (SFP_TEMP_HIGH_WARN);
	 * 134-135 - Temp Low Warning (SFP_TEMP_LOW_WARN);
	 */

	/* Validate module identifier value. */
	err = mlxsw_env_validate_cable_ident(core, slot_index, module, &qsfp,
					     &cmis);
	if (err)
		return err;

	if (qsfp) {
		/* For QSFP/CMIS module-defined thresholds are located in page
		 * 02h, otherwise in page 03h.
		 */
		if (cmis)
			page = MLXSW_REG_MCIA_TH_PAGE_CMIS_NUM;
		else
			page = MLXSW_REG_MCIA_TH_PAGE_NUM;
		mlxsw_reg_mcia_pack(mcia_pl, slot_index, module, 0, page, 0,
				    MLXSW_REG_MCIA_TH_PAGE_OFF + off,
				    MLXSW_REG_MCIA_TH_ITEM_SIZE,
				    MLXSW_REG_MCIA_I2C_ADDR_LOW);
	} else {
		mlxsw_reg_mcia_pack(mcia_pl, slot_index, module, 0,
				    MLXSW_REG_MCIA_PAGE0_LO, 0,
				    off, MLXSW_REG_MCIA_TH_ITEM_SIZE,
				    MLXSW_REG_MCIA_I2C_ADDR_HIGH);
	}

	err = mlxsw_reg_query(core, MLXSW_REG(mcia), mcia_pl);
	if (err)
		return err;

	mlxsw_reg_mcia_eeprom_memcpy_from(mcia_pl, eeprom_tmp);
	memcpy(temp_thresh.buf, eeprom_tmp, MLXSW_REG_MCIA_TH_ITEM_SIZE);
	*temp = temp_thresh.temp * 1000;

	return 0;
}

int mlxsw_env_get_module_info(struct mlxsw_core *mlxsw_core, u8 slot_index,
			      int module, struct ethtool_modinfo *modinfo)
{
	u8 module_info[MLXSW_REG_MCIA_EEPROM_MODULE_INFO_SIZE];
	u16 offset = MLXSW_REG_MCIA_EEPROM_MODULE_INFO_SIZE;
	u8 module_rev_id, module_id, diag_mon;
	unsigned int read_size, opt_size = 0;
	int err;

	err = mlxsw_env_query_module_eeprom(mlxsw_core, slot_index, module, 0, offset,
					    false, false, 0, 0, module_info,
					    &read_size);
	if (err)
		return err;

	if (read_size < offset)
		return -EIO;

	module_rev_id = module_info[MLXSW_REG_MCIA_EEPROM_MODULE_INFO_REV_ID];
	module_id = module_info[MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID];

	switch (module_id) {
	case MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_QSFP:
		modinfo->type       = ETH_MODULE_SFF_8436;
		modinfo->eeprom_len = ETH_MODULE_SFF_8436_MAX_LEN;
		break;
	case MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_QSFP_PLUS: /* fall-through */
	case MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_QSFP28:
		if (module_id == MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_QSFP28 ||
		    module_rev_id >=
		    MLXSW_REG_MCIA_EEPROM_MODULE_INFO_REV_ID_8636) {
			modinfo->type       = ETH_MODULE_SFF_8636;
			modinfo->eeprom_len = ETH_MODULE_SFF_8636_MAX_LEN;
		} else {
			modinfo->type       = ETH_MODULE_SFF_8436;
			modinfo->eeprom_len = ETH_MODULE_SFF_8436_MAX_LEN;
		}
		break;
	case MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_SFP: /* fall-through */
	case MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_SFP_DD:
		/* Verify if transceiver provides diagnostic monitoring page */
		err = mlxsw_env_query_module_eeprom(mlxsw_core, slot_index,
						    module, SFP_DIAGMON, 1, false,
						    false, 0, 0, &diag_mon,
						    &read_size);
		if (err)
			return err;

		if (read_size < 1)
			return -EIO;

		modinfo->type       = ETH_MODULE_SFF_8472;
		if (diag_mon)
			modinfo->eeprom_len = ETH_MODULE_SFF_8472_LEN;
		else
			modinfo->eeprom_len = ETH_MODULE_SFF_8472_LEN / 2;
		break;
	case MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_QSFP_DD: /* fall-through */
	case MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_QSFP_PLUS_CMIS:
		modinfo->type       = ETH_MODULE_SFF_8636;
		/* Verify if module's EEPROM is a flat memory. In case of flat
		 * memory only page 00h 0-255 bytes can be read.
		 */
		if (module_info[MLXSW_REG_MCIA_EEPROM_MODULE_INFO_TYPE_ID] &
		    MLXSW_REG_MCIA_EEPROM_FLAT_MEMORY) {
			modinfo->eeprom_len = ETH_MODULE_SFF_8636_LEN;
		} else {
			/* Get size of optional pages and banks. */
			err = mlxsw_env_cmis_optional_get(mlxsw_core, slot_index,
							  module, &opt_size,
							  NULL, NULL);
			if (err)
				return err;

			modinfo->eeprom_len = ETH_MODULE_SFF_8472_LEN +
					      opt_size;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(mlxsw_env_get_module_info);

int mlxsw_env_get_module_eeprom(struct net_device *netdev,
				struct mlxsw_core *mlxsw_core, u8 slot_index,
				int module, struct ethtool_eeprom *ee,
				u8 *data)
{
	u8 opt_page = 0, opt_banks = 0;
	int offset = ee->offset;
	unsigned int read_size;
	bool qsfp, cmis;
	int i = 0;
	int err;

	if (!ee->len)
		return -EINVAL;

	memset(data, 0, ee->len);
	/* Validate module identifier value. */
	err = mlxsw_env_validate_cable_ident(mlxsw_core, slot_index, module,
					     &qsfp, &cmis);
	if (err)
		return err;

	if (cmis) {
		err = mlxsw_env_cmis_optional_get(mlxsw_core, slot_index, module,
						  NULL, &opt_page, &opt_banks);
		if (err)
			return err;
	}

	while (i < ee->len) {
		err = mlxsw_env_query_module_eeprom(mlxsw_core, slot_index,
						    module, offset,
						    ee->len - i, qsfp, cmis,
						    opt_page, opt_banks,
						    data + i, &read_size);
		if (err) {
			netdev_err(netdev, "Eeprom query failed\n");
			return err;
		}

		i += read_size;
		offset += read_size;
	}

	return 0;
}
EXPORT_SYMBOL(mlxsw_env_get_module_eeprom);
