// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * RAK RAK811
 *
 * Copyright (c) 2017-2018 Andreas Färber
 */

#include <linux/delay.h>
#include <linux/lora.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/serdev.h>
#include <linux/lora/dev.h>

struct rak811_device {
	struct serdev_device *serdev;

	char rx_buf[4096];
	int rx_len;

	struct completion line_recv_comp;
};

static int rak811_send_command(struct rak811_device *rakdev, const char *cmd, char **data, unsigned long timeout)
{
	struct serdev_device *sdev = rakdev->serdev;
	const char *crlf = "\r\n";
	char *resp;

	serdev_device_write_buf(sdev, cmd, strlen(cmd));
	serdev_device_write_buf(sdev, crlf, 2);

	timeout = wait_for_completion_timeout(&rakdev->line_recv_comp, timeout);
	if (!timeout)
		return -ETIMEDOUT;

	resp = rakdev->rx_buf;
	dev_dbg(&sdev->dev, "Received: '%s'\n", resp);
	if (data)
		*data = kstrdup(resp, GFP_KERNEL);

	rakdev->rx_len = 0;
	reinit_completion(&rakdev->line_recv_comp);

	return 0;
}

static int rak811_simple_cmd(struct rak811_device *rakdev, const char *cmd, unsigned long timeout)
{
	char *resp;
	int ret;

	ret = rak811_send_command(rakdev, cmd, &resp, timeout);
	if (ret)
		return ret;

	if (strncmp(resp, "OK", 2) == 0) {
		kfree(resp);
		return 0;
	}

	kfree(resp);

	return -EINVAL;
}

static int rak811_get_version(struct rak811_device *rakdev, char **version, unsigned long timeout)
{
	char *resp;
	int ret;

	ret = rak811_send_command(rakdev, "at+version", &resp, timeout);
	if (ret)
		return ret;

	if (strncmp(resp, "OK", 2) == 0) {
		*version = kstrdup(resp + 2, GFP_KERNEL);
		kfree(resp);
		return 0;
	}

	kfree(resp);

	return -EINVAL;
}

static int rak811_receive_buf(struct serdev_device *sdev, const u8 *data, size_t count)
{
	struct rak811_device *rakdev = serdev_device_get_drvdata(sdev);
	size_t i = 0;
	int len = 0;

	dev_dbg(&sdev->dev, "Receive (%d)\n", (int)count);

	for (i = 0; i < count; i++) {
		dev_dbg(&sdev->dev, "Receive: 0x%02x\n", (int)data[i]);
	}

	if (completion_done(&rakdev->line_recv_comp)) {
		dev_info(&sdev->dev, "RX waiting on completion\n");
		return 0;
	}
	if (rakdev->rx_len == sizeof(rakdev->rx_buf) - 1) {
		dev_warn(&sdev->dev, "RX buffer full\n");
		return 0;
	}

	i = min(count, sizeof(rakdev->rx_buf) - 1 - rakdev->rx_len);
	if (i > 0) {
		memcpy(&rakdev->rx_buf[rakdev->rx_len], data, i);
		rakdev->rx_len += i;
		len += i;
	}
	if (rakdev->rx_len >= 2 && strncmp(&rakdev->rx_buf[rakdev->rx_len - 2], "\r\n", 2) == 0) {
		rakdev->rx_len -= 2;
		rakdev->rx_buf[rakdev->rx_len] = '\0';
		complete(&rakdev->line_recv_comp);
	}

	return len;
}

static const struct serdev_device_ops rak811_serdev_client_ops = {
	.receive_buf = rak811_receive_buf,
};

static int rak811_probe(struct serdev_device *sdev)
{
	struct rak811_device *rakdev;
	char *sz;
	int ret;

	dev_info(&sdev->dev, "Probing\n");

	rakdev = devm_kzalloc(&sdev->dev, sizeof(struct rak811_device), GFP_KERNEL);
	if (!rakdev)
		return -ENOMEM;

	rakdev->serdev = sdev;
	init_completion(&rakdev->line_recv_comp);
	serdev_device_set_drvdata(sdev, rakdev);

	ret = serdev_device_open(sdev);
	if (ret) {
		dev_err(&sdev->dev, "Failed to open (%d)\n", ret);
		return ret;
	}

	serdev_device_set_baudrate(sdev, 115200);
	serdev_device_set_flow_control(sdev, false);
	serdev_device_set_client_ops(sdev, &rak811_serdev_client_ops);

	ret = rak811_get_version(rakdev, &sz, HZ);
	if (ret) {
		dev_err(&sdev->dev, "Failed to get version (%d)\n", ret);
		serdev_device_close(sdev);
		return ret;
	}

	dev_info(&sdev->dev, "firmware version: %s\n", sz);
	kfree(sz);

	ret = rak811_simple_cmd(rakdev, "at+mode=1", 2 * HZ);
	if (ret) {
		dev_err(&sdev->dev, "Failed to set mode to P2P (%d)\n", ret);
		serdev_device_close(sdev);
		return ret;
	}

	dev_info(&sdev->dev, "Done.\n");

	return 0;
}

static void rak811_remove(struct serdev_device *sdev)
{
	serdev_device_close(sdev);

	dev_info(&sdev->dev, "Removed\n");
}

static const struct of_device_id rak811_of_match[] = {
	{ .compatible = "rakwireless,rak811" },
	{}
};
MODULE_DEVICE_TABLE(of, rak811_of_match);

static struct serdev_device_driver rak811_serdev_driver = {
	.probe = rak811_probe,
	.remove = rak811_remove,
	.driver = {
		.name = "rak811",
		.of_match_table = rak811_of_match,
	},
};

static int __init rak811_init(void)
{
	int ret;

	ret = serdev_device_driver_register(&rak811_serdev_driver);
	if (ret)
		return ret;

	return 0;
}

static void __exit rak811_exit(void)
{
	serdev_device_driver_unregister(&rak811_serdev_driver);
}

module_init(rak811_init);
module_exit(rak811_exit);

MODULE_DESCRIPTION("RAK811 serdev driver");
MODULE_AUTHOR("Andreas Färber <afaerber@suse.de>");
MODULE_LICENSE("GPL");
