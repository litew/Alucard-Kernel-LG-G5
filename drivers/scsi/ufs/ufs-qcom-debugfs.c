/*
 * Copyright (c) 2015, Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/debugfs.h>
#include "ufs-qcom.h"
#include "ufs-qcom-debugfs.h"
#include "ufs-debugfs.h"
#ifdef CONFIG_UFS_LGE_FEATURE
#include <linux/phy/phy-qcom-ufs.h>
#endif

#define TESTBUS_CFG_BUFF_LINE_SIZE	sizeof("0xXY, 0xXY")

static void ufs_qcom_dbg_remove_debugfs(struct ufs_qcom_host *host);

static int ufs_qcom_dbg_print_en_read(void *data, u64 *attr_val)
{
	struct ufs_qcom_host *host = data;

	if (!host)
		return -EINVAL;

	*attr_val = (u64)host->dbg_print_en;
	return 0;
}

static int ufs_qcom_dbg_print_en_set(void *data, u64 attr_id)
{
	struct ufs_qcom_host *host = data;

	if (!host)
		return -EINVAL;

	if (attr_id & ~UFS_QCOM_DBG_PRINT_ALL)
		return -EINVAL;

	host->dbg_print_en = (u32)attr_id;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(ufs_qcom_dbg_print_en_ops,
			ufs_qcom_dbg_print_en_read,
			ufs_qcom_dbg_print_en_set,
			"%llu\n");

static int ufs_qcom_dbg_testbus_en_read(void *data, u64 *attr_val)
{
	struct ufs_qcom_host *host = data;
	bool enabled;

	if (!host)
		return -EINVAL;

	enabled = !!(host->dbg_print_en & UFS_QCOM_DBG_PRINT_TEST_BUS_EN);
	*attr_val = (u64)enabled;
	return 0;
}

static int ufs_qcom_dbg_testbus_en_set(void *data, u64 attr_id)
{
	struct ufs_qcom_host *host = data;

	if (!host)
		return -EINVAL;

	if (!!attr_id)
		host->dbg_print_en |= UFS_QCOM_DBG_PRINT_TEST_BUS_EN;
	else
		host->dbg_print_en &= ~UFS_QCOM_DBG_PRINT_TEST_BUS_EN;

	return ufs_qcom_testbus_config(host);
}

DEFINE_SIMPLE_ATTRIBUTE(ufs_qcom_dbg_testbus_en_ops,
			ufs_qcom_dbg_testbus_en_read,
			ufs_qcom_dbg_testbus_en_set,
			"%llu\n");

static int ufs_qcom_dbg_testbus_cfg_show(struct seq_file *file, void *data)
{
	struct ufs_qcom_host *host = (struct ufs_qcom_host *)file->private;

	seq_printf(file , "Current configuration: major=%d, minor=%d\n\n",
			host->testbus.select_major, host->testbus.select_minor);

	/* Print usage */
	seq_puts(file,
		"To change the test-bus configuration, write 'MAJ,MIN' where:\n"
		"MAJ - major select\n"
		"MIN - minor select\n\n");
	return 0;
}

static ssize_t ufs_qcom_dbg_testbus_cfg_write(struct file *file,
				const char __user *ubuf, size_t cnt,
				loff_t *ppos)
{
	struct ufs_qcom_host *host = file->f_mapping->host->i_private;
	char configuration[TESTBUS_CFG_BUFF_LINE_SIZE] = {0};
	loff_t buff_pos = 0;
	char *comma;
	int ret = 0;
	int major;
	int minor;

	ret = simple_write_to_buffer(configuration, TESTBUS_CFG_BUFF_LINE_SIZE,
		&buff_pos, ubuf, cnt);
	if (ret < 0) {
		dev_err(host->hba->dev, "%s: failed to read user data\n",
			__func__);
		goto out;
	}

	comma = strnchr(configuration, TESTBUS_CFG_BUFF_LINE_SIZE, ',');
	if (!comma || comma == configuration) {
		dev_err(host->hba->dev,
			"%s: error in configuration of testbus\n", __func__);
		ret = -EINVAL;
		goto out;
	}

	if (sscanf(configuration, "%i,%i", &major, &minor) != 2) {
		dev_err(host->hba->dev,
			"%s: couldn't parse input to 2 numeric values\n",
			__func__);
		ret = -EINVAL;
		goto out;
	}

	host->testbus.select_major = (u8)major;
	host->testbus.select_minor = (u8)minor;

	/*
	 * Sanity check of the {major, minor} tuple is done in the
	 * config function
	 */
	ret = ufs_qcom_testbus_config(host);
	if (!ret)
		dev_dbg(host->hba->dev,
				"%s: New configuration: major=%d, minor=%d\n",
				__func__, host->testbus.select_major,
				host->testbus.select_minor);

out:
	return ret ? ret : cnt;
}

static int ufs_qcom_dbg_testbus_cfg_open(struct inode *inode, struct file *file)
{
	return single_open(file, ufs_qcom_dbg_testbus_cfg_show,
				inode->i_private);
}

static const struct file_operations ufs_qcom_dbg_testbus_cfg_desc = {
	.open		= ufs_qcom_dbg_testbus_cfg_open,
	.read		= seq_read,
	.write		= ufs_qcom_dbg_testbus_cfg_write,
};

static int ufs_qcom_dbg_testbus_bus_read(void *data, u64 *attr_val)
{
	struct ufs_qcom_host *host = data;

	if (!host)
		return -EINVAL;

	pm_runtime_get_sync(host->hba->dev);
	ufshcd_hold(host->hba, false);
	*attr_val = (u64)ufshcd_readl(host->hba, UFS_TEST_BUS);
	ufshcd_release(host->hba, false);
	pm_runtime_put_sync(host->hba->dev);
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(ufs_qcom_dbg_testbus_bus_ops,
			ufs_qcom_dbg_testbus_bus_read,
			NULL,
			"%llu\n");

static int ufs_qcom_dbg_dbg_regs_show(struct seq_file *file, void *data)
{
	struct ufs_qcom_host *host = (struct ufs_qcom_host *)file->private;
	bool dbg_print_reg = !!(host->dbg_print_en &
				UFS_QCOM_DBG_PRINT_REGS_EN);

	pm_runtime_get_sync(host->hba->dev);
	ufshcd_hold(host->hba, false);

	/* Temporarily override the debug print enable */
	host->dbg_print_en |= UFS_QCOM_DBG_PRINT_REGS_EN;
	ufs_qcom_print_hw_debug_reg_all(host->hba, file, ufsdbg_pr_buf_to_std);
	/* Restore previous debug print enable value */
	if (!dbg_print_reg)
		host->dbg_print_en &= ~UFS_QCOM_DBG_PRINT_REGS_EN;

	ufshcd_release(host->hba, false);
	pm_runtime_put_sync(host->hba->dev);

	return 0;
}

static int ufs_qcom_dbg_dbg_regs_open(struct inode *inode,
					      struct file *file)
{
	return single_open(file, ufs_qcom_dbg_dbg_regs_show,
				inode->i_private);
}

static const struct file_operations ufs_qcom_dbg_dbg_regs_desc = {
	.open		= ufs_qcom_dbg_dbg_regs_open,
	.read		= seq_read,
};

static int ufs_qcom_dbg_pm_qos_show(struct seq_file *file, void *data)
{
	struct ufs_qcom_host *host = (struct ufs_qcom_host *)file->private;
	unsigned long flags;
	int i;

	spin_lock_irqsave(host->hba->host->host_lock, flags);

	seq_printf(file, "enabled: %d\n", host->pm_qos.is_enabled);
	for (i = 0; i < host->pm_qos.num_groups && host->pm_qos.groups; i++)
		seq_printf(file,
			"CPU Group #%d(mask=0x%lx): active_reqs=%d, state=%d, latency=%d\n",
			i, host->pm_qos.groups[i].mask.bits[0],
			host->pm_qos.groups[i].active_reqs,
			host->pm_qos.groups[i].state,
			host->pm_qos.groups[i].latency_us);

	spin_unlock_irqrestore(host->hba->host->host_lock, flags);

	return 0;
}

static int ufs_qcom_dbg_pm_qos_open(struct inode *inode,
					      struct file *file)
{
	return single_open(file, ufs_qcom_dbg_pm_qos_show, inode->i_private);
}

static const struct file_operations ufs_qcom_dbg_pm_qos_desc = {
	.open		= ufs_qcom_dbg_pm_qos_open,
	.read		= seq_read,
};

#ifdef CONFIG_UFS_LGE_FEATURE
static ssize_t ufs_qcom_phy_drv_str_write(struct file *file,
					const char __user *ubuf, size_t cnt,
					loff_t *ppos)
{
	struct ufs_qcom_host *host = file->f_mapping->host->i_private;
	struct phy *phy = host->generic_phy;
	char set_val[5] = {0};
	loff_t buff_pos = 0;
	int ret = 0;
	int val=0;
	int tmp;
	int i;

	ret = simple_write_to_buffer(set_val, 5, &buff_pos, ubuf, cnt);
	if (ret < 0) {
		dev_err(host->hba->dev, "%s: failed to read user data\n",
				__func__);
		goto out;
	}

	for(i=0; i<cnt; i++){
		tmp = set_val[i] - 48;
		if(tmp < 0 || tmp > 9)
			break;
		else {
			if(val)
				val *= 10;
		}
		val += tmp;
	}

	if (val > 31 || val < 0) {
		ret = -1;
		dev_err(host->hba->dev, "%s: failed invalid value : %d\n",
				__func__, val);
		goto out;
	}

	pm_runtime_get_sync(host->hba->dev);
	ufshcd_hold(host->hba, false);
	ufs_qcom_phy_ctrl_tx_drv_strength(phy, 1, &val);
	ufshcd_release(host->hba, false);
	pm_runtime_put_sync(host->hba->dev);
out:
	return ret? ret:cnt;
}

static int ufs_qcom_drv_str_show(struct seq_file *file, void *data)
{
	u32 val=0;
	struct ufs_qcom_host *host = (struct ufs_qcom_host *)file->private;
	struct phy *phy = host->generic_phy;

	pm_runtime_get_sync(host->hba->dev);
	ufshcd_hold(host->hba, false);

	ufs_qcom_phy_ctrl_tx_drv_strength(phy, 0, &val);
	seq_printf(file, "Read UFSPHY TX Driver Strength : %d\n", val);

	ufshcd_release(host->hba, false);
	pm_runtime_put_sync(host->hba->dev);

	return 0;
}

static int ufs_qcom_phy_drv_str_open(struct inode *inode, struct file *file)
{
	return single_open(file, ufs_qcom_drv_str_show, inode->i_private);
}

static const struct file_operations ufs_qcom_ctrl_drv_str = {
	.open		= ufs_qcom_phy_drv_str_open,
	.read		= seq_read,
	.write		= ufs_qcom_phy_drv_str_write,
};
#endif

void ufs_qcom_dbg_add_debugfs(struct ufs_hba *hba, struct dentry *root)
{
	struct ufs_qcom_host *host;

	if (!hba || !hba->priv) {
		pr_err("%s: NULL host, exiting\n", __func__);
		return;
	}

	host = hba->priv;
	host->debugfs_files.debugfs_root = debugfs_create_dir("qcom", root);
	if (IS_ERR(host->debugfs_files.debugfs_root))
		/* Don't complain -- debugfs just isn't enabled */
		goto err_no_root;
	if (!host->debugfs_files.debugfs_root) {
		/*
		 * Complain -- debugfs is enabled, but it failed to
		 * create the directory
		 */
		dev_err(host->hba->dev,
			"%s: NULL debugfs root directory, exiting", __func__);
		goto err_no_root;
	}

	host->debugfs_files.dbg_print_en =
		debugfs_create_file("dbg_print_en", S_IRUSR | S_IWUSR,
				    host->debugfs_files.debugfs_root, host,
				    &ufs_qcom_dbg_print_en_ops);
	if (!host->debugfs_files.dbg_print_en) {
		dev_err(host->hba->dev,
			"%s: failed to create dbg_print_en debugfs entry\n",
			__func__);
		goto err;
	}

	host->debugfs_files.testbus = debugfs_create_dir("testbus",
					host->debugfs_files.debugfs_root);
	if (!host->debugfs_files.testbus) {
		dev_err(host->hba->dev,
			"%s: failed create testbus directory\n",
			__func__);
		goto err;
	}

	host->debugfs_files.testbus_en =
		debugfs_create_file("enable", S_IRUSR | S_IWUSR,
				    host->debugfs_files.testbus, host,
				    &ufs_qcom_dbg_testbus_en_ops);
	if (!host->debugfs_files.testbus_en) {
		dev_err(host->hba->dev,
			"%s: failed create testbus_en debugfs entry\n",
			__func__);
		goto err;
	}

	host->debugfs_files.testbus_cfg =
		debugfs_create_file("configuration", S_IRUSR | S_IWUSR,
				    host->debugfs_files.testbus, host,
				    &ufs_qcom_dbg_testbus_cfg_desc);
	if (!host->debugfs_files.testbus_cfg) {
		dev_err(host->hba->dev,
			"%s: failed create testbus_cfg debugfs entry\n",
			__func__);
		goto err;
	}

	host->debugfs_files.testbus_bus =
		debugfs_create_file("TEST_BUS", S_IRUSR,
				    host->debugfs_files.testbus, host,
				    &ufs_qcom_dbg_testbus_bus_ops);
	if (!host->debugfs_files.testbus_bus) {
		dev_err(host->hba->dev,
			"%s: failed create testbus_bus debugfs entry\n",
			__func__);
		goto err;
	}

	host->debugfs_files.dbg_regs =
		debugfs_create_file("debug-regs", S_IRUSR,
				    host->debugfs_files.debugfs_root, host,
				    &ufs_qcom_dbg_dbg_regs_desc);
	if (!host->debugfs_files.dbg_regs) {
		dev_err(host->hba->dev,
			"%s: failed create dbg_regs debugfs entry\n",
			__func__);
		goto err;
	}

	host->debugfs_files.pm_qos =
		debugfs_create_file("pm_qos", S_IRUSR,
				host->debugfs_files.debugfs_root, host,
				&ufs_qcom_dbg_pm_qos_desc);
		if (!host->debugfs_files.dbg_regs) {
			dev_err(host->hba->dev,
				"%s: failed create dbg_regs debugfs entry\n",
				__func__);
			goto err;
		}
#ifdef CONFIG_UFS_LGE_FEATURE
	host->debugfs_files.phy_tx_drv_str =
		debugfs_create_file("phy_tx_drv_str", S_IRUSR | S_IWUSR,
				host->debugfs_files.debugfs_root, host,
				&ufs_qcom_ctrl_drv_str);
	if (!host->debugfs_files.phy_tx_drv_str) {
		dev_err(host->hba->dev,
				"%s: failed create phy_tx_drv_str debugfs entry\n",
				__func__);
		goto err;
	}
#endif

	return;

err:
	ufs_qcom_dbg_remove_debugfs(host);
err_no_root:
	dev_err(host->hba->dev, "%s: failed to initialize debugfs\n", __func__);
}

static void ufs_qcom_dbg_remove_debugfs(struct ufs_qcom_host *host)
{
	debugfs_remove_recursive(host->debugfs_files.debugfs_root);
	host->debugfs_files.debugfs_root = NULL;
}
