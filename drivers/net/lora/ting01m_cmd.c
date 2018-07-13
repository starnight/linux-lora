// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Widora   Ting-01M
 * Himalaya HIMO-01M
 *
 * Copyright (c) 2017-2018 Andreas Färber
 */

#include <linux/serdev.h>

#include "ting01m.h"

int widora_send_command(struct widora_device *widev, const char *cmd, char **data, unsigned long timeout)
{
	struct serdev_device *sdev = widev->serdev;
	const char *crlf = "\r\n";
	char *resp;

	serdev_device_write_buf(sdev, cmd, strlen(cmd));
	serdev_device_write_buf(sdev, crlf, 2);

	timeout = wait_for_completion_timeout(&widev->line_recv_comp, timeout);
	if (!timeout)
		return -ETIMEDOUT;

	resp = widev->rx_buf;
	dev_dbg(&sdev->dev, "Received: '%s'\n", resp);
	if (data)
		*data = kstrdup(resp, GFP_KERNEL);

	widev->rx_len = 0;
	reinit_completion(&widev->line_recv_comp);

	return 0;
}

int widora_simple_cmd(struct widora_device *widev, const char *cmd, unsigned long timeout)
{
	char *resp;
	int ret;

	ret = widora_send_command(widev, cmd, &resp, timeout);
	if (ret)
		return ret;

	if (strcmp(resp, "AT,OK") == 0) {
		kfree(resp);
		return 0;
	}

	kfree(resp);

	return -EINVAL;
}

int widora_do_reset(struct widora_device *widev, unsigned long timeout)
{
	char *resp;
	int ret;

	ret = widora_simple_cmd(widev, "AT+RST", timeout);
	if (ret)
		return ret;

	timeout = wait_for_completion_timeout(&widev->line_recv_comp, timeout);
	if (!timeout)
		return -ETIMEDOUT;

	resp = widev->rx_buf;

	dev_info(&widev->serdev->dev, "reset: '%s'\n", resp);

	widev->rx_len = 0;
	reinit_completion(&widev->line_recv_comp);

	return 0;
}

int widora_get_version(struct widora_device *widev, char **version, unsigned long timeout)
{
	char *resp;
	int ret, len;

	ret = widora_send_command(widev, "AT+VER", &resp, timeout);
	if (ret)
		return ret;

	len = strlen(resp);

	if ((strncmp(resp, "AT,", 3) == 0) && (strncmp(resp + len - 3, ",OK", 3) == 0)) {
		*version = kstrndup(resp + 3, len - 3 - 3, GFP_KERNEL);
		kfree(resp);
		return 0;
	}

	kfree(resp);

	return -EINVAL;
}

int widora_set_gpio(struct widora_device *widev, char bank, char pin, bool enabled, unsigned long timeout)
{
	char cmd[] = "AT+Pxx=x";

	cmd[4] = bank;
	cmd[5] = pin;
	cmd[7] = enabled ? '1' : '0';

	return widora_simple_cmd(widev, cmd, timeout);
}
