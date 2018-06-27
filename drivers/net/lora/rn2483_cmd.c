// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Microchip RN2483/RN2903 - UART commands
 *
 * Copyright (c) 2017-2018 Andreas Färber
 */
#include "rn2483.h"

#define RN2483_CMD_TIMEOUT HZ

int rn2483_send_command_timeout(struct rn2483_device *rndev,
	const char *cmd, char **resp, unsigned long timeout)
{
	int ret;

	ret = serdev_device_write_buf(rndev->serdev, cmd, strlen(cmd));
	if (ret < 0)
		return ret;

	ret = serdev_device_write_buf(rndev->serdev, "\r\n", 2);
	if (ret < 0)
		return ret;

	return rn2483_readline_timeout(rndev, resp, timeout);
}

int rn2483_sys_get_hweui(struct rn2483_device *rndev, lora_eui *val)
{
	int ret;
	char *line;

	mutex_lock(&rndev->cmd_lock);
	ret = rn2483_send_command_timeout(rndev, "sys get hweui", &line, RN2483_CMD_TIMEOUT);
	mutex_unlock(&rndev->cmd_lock);
	if (ret)
		return ret;

	ret = lora_strtoeui(line, val);
	devm_kfree(&rndev->serdev->dev, line);
	return ret;
}

int rn2483_mac_get_band(struct rn2483_device *rndev, uint *val)
{
	int ret;
	char *line;

	mutex_lock(&rndev->cmd_lock);
	ret = rn2483_send_command_timeout(rndev, "mac get band", &line, RN2483_CMD_TIMEOUT);
	mutex_unlock(&rndev->cmd_lock);
	if (ret)
		return ret;

	ret = kstrtouint(line, 10, val);
	devm_kfree(&rndev->serdev->dev, line);

	return ret;
}

int rn2483_mac_get_status(struct rn2483_device *rndev, u32 *val)
{
	int ret;
	char *line;

	mutex_lock(&rndev->cmd_lock);
	ret = rn2483_send_command_timeout(rndev, "mac get status", &line, RN2483_CMD_TIMEOUT);
	mutex_unlock(&rndev->cmd_lock);
	if (ret)
		return ret;

	ret = kstrtou32(line, 16, val);
	devm_kfree(&rndev->serdev->dev, line);
	return ret;
}

int rn2483_mac_reset_band(struct rn2483_device *rndev, unsigned band)
{
	int ret;
	char *line, *cmd;

	cmd = devm_kasprintf(&rndev->serdev->dev, GFP_KERNEL, "mac reset %u", band);
	mutex_lock(&rndev->cmd_lock);
	ret = rn2483_send_command_timeout(rndev, cmd, &line, RN2483_CMD_TIMEOUT);
	mutex_unlock(&rndev->cmd_lock);
	devm_kfree(&rndev->serdev->dev, cmd);
	if (ret)
		return ret;

	if (strcmp(line, "ok") == 0)
		ret = 0;
	else if (strcmp(line, "invalid_param") == 0)
		ret = -EINVAL;
	else
		ret = -EPROTO;

	devm_kfree(&rndev->serdev->dev, line);
	return ret;
}

int rn2483_mac_pause(struct rn2483_device *rndev, u32 *max_pause)
{
	int ret;
	char *line;

	mutex_lock(&rndev->cmd_lock);
	ret = rn2483_send_command_timeout(rndev, "mac pause", &line, RN2483_CMD_TIMEOUT);
	mutex_unlock(&rndev->cmd_lock);
	if (ret)
		return ret;

	ret = kstrtou32(line, 10, max_pause);
	devm_kfree(&rndev->serdev->dev, line);
	return ret;
}

int rn2483_mac_resume(struct rn2483_device *rndev)
{
	int ret;
	char *line;

	mutex_lock(&rndev->cmd_lock);
	ret = rn2483_send_command_timeout(rndev, "mac resume", &line, RN2483_CMD_TIMEOUT);
	mutex_unlock(&rndev->cmd_lock);
	if (ret)
		return ret;

	ret = (strcmp(line, "ok") == 0) ? 0 : -EPROTO;
	devm_kfree(&rndev->serdev->dev, line);
	return ret;
}
