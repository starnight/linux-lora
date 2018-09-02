// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Appcon Wireless RF1276TS
 *
 * Copyright (c) 2018 Andreas Färber
 *
 * Based on mm002.c:
 * Copyright (c) 2017-2018 Andreas Färber
 */

#include <linux/delay.h>
#include <linux/lora.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/serdev.h>
#include <linux/lora/dev.h>

struct rf1276ts_device {
	struct serdev_device *serdev;

	char rx_buf[2048];
	int rx_len;

	struct completion resp_recv_comp;
};

static int rf1276ts_send_command(struct rf1276ts_device *rfdev, const char *cmd, char **data, unsigned long timeout)
{
	struct serdev_device *sdev = rfdev->serdev;
	const char *crlf = "\r\n";
	char *resp;

	serdev_device_write_buf(sdev, cmd, strlen(cmd));
	serdev_device_write_buf(sdev, crlf, 2);

	timeout = wait_for_completion_timeout(&rfdev->resp_recv_comp, timeout);
	if (!timeout)
		return -ETIMEDOUT;

	resp = rfdev->rx_buf;
	dev_dbg(&sdev->dev, "Received: '%s'\n", resp);
	if (data)
		*data = kstrdup(resp, GFP_KERNEL);

	rfdev->rx_len = 0;
	reinit_completion(&rfdev->resp_recv_comp);

	return 0;
}

static int rf1276ts_simple_cmd(struct rf1276ts_device *rfdev, const char *cmd, unsigned long timeout)
{
	char *resp;
	int ret;

	ret = rf1276ts_send_command(rfdev, cmd, &resp, timeout);
	if (ret)
		return ret;

	if (strcmp(resp, "OK") == 0) {
		kfree(resp);
		return 0;
	}

	kfree(resp);

	return -EINVAL;
}

static int rf1276ts_do_at(struct rf1276ts_device *rfdev, unsigned long timeout)
{
	return rf1276ts_simple_cmd(rfdev, "AT", timeout);
}

static void rf1276ts_reset(struct rf1276ts_device *rfdev)
{
	struct serdev_device *sdev = rfdev->serdev;
	const char *cmd = "ATZ\r\n";

	serdev_device_write_buf(sdev, cmd, strlen(cmd));
	msleep(500);
	rfdev->rx_len = 0;
}

static int rf1276ts_get_version(struct rf1276ts_device *rfdev, char **version, unsigned long timeout)
{
	char *resp;
	int ret, len;

	ret = rf1276ts_send_command(rfdev, "AT+V?", &resp, timeout);
	if (ret)
		return ret;

	len = strlen(resp);
	if (len < 4 || strcmp(resp + len - 4, "\r\nOK") != 0) {
		kfree(resp);
		return -EINVAL;
	}

	if (strncmp(resp, "+V:", 3) == 0) {
		*version = kstrndup(resp + 3, len - 3 - 4, GFP_KERNEL);
		kfree(resp);
		return 0;
	}

	kfree(resp);

	return -EINVAL;
}

static int rf1276ts_receive_buf(struct serdev_device *sdev, const u8 *data, size_t count)
{
	struct rf1276ts_device *rfdev = serdev_device_get_drvdata(sdev);
	size_t i = 0;
	int len = 0;

	dev_dbg(&sdev->dev, "Receive (%d)\n", (int)count);

	for (i = 0; i < count; i++) {
		dev_dbg(&sdev->dev, "Receive: 0x%02x\n", (int)data[i]);
	}

	if (completion_done(&rfdev->resp_recv_comp)) {
		dev_info(&sdev->dev, "RX waiting on completion\n");
		return 0;
	}
	if (rfdev->rx_len == sizeof(rfdev->rx_buf) - 1) {
		dev_warn(&sdev->dev, "RX buffer full\n");
		return 0;
	}

	i = min(count, sizeof(rfdev->rx_buf) - 1 - rfdev->rx_len);
	if (i > 0) {
		memcpy(&rfdev->rx_buf[rfdev->rx_len], data, i);
		rfdev->rx_len += i;
		len += i;
	}
	if (rfdev->rx_len > 2 && strncmp(&rfdev->rx_buf[rfdev->rx_len - 2], "\r\n", 2) == 0) {
		if (strstarts(rfdev->rx_buf, "+SEND:") ||
		    strstarts(rfdev->rx_buf, "+JOIN:") ||
		    strcmp(rfdev->rx_buf, "+BLECONN\r\n") == 0 ||
		    strcmp(rfdev->rx_buf, "+BLEDISC\r\n") == 0 ||
		    strstarts(rfdev->rx_buf, "+BLEIDC:")) {
			rfdev->rx_len -= 2;
			rfdev->rx_buf[rfdev->rx_len] = '\0';
			dev_dbg(&sdev->dev, "event: %s\n", rfdev->rx_buf);
			rfdev->rx_len = 0;
		}
	}
	if ((rfdev->rx_len == 4 && strncmp(rfdev->rx_buf, "OK\r\n", 4) == 0) ||
	    (rfdev->rx_len >= 6 && strncmp(&rfdev->rx_buf[rfdev->rx_len - 6], "\r\nOK\r\n", 6) == 0) ||
	    (rfdev->rx_len == 9 && strncmp(rfdev->rx_buf, "ERROR:", 6) == 0
				&& strncmp(&rfdev->rx_buf[rfdev->rx_len - 2], "\r\n", 2) == 0) ||
	    (rfdev->rx_len >= 11 && strncmp(&rfdev->rx_buf[rfdev->rx_len - 11], "\r\nERROR:", 8) == 0
				 && strncmp(&rfdev->rx_buf[rfdev->rx_len - 2], "\r\n", 2) == 0)) {
		rfdev->rx_len -= 2;
		rfdev->rx_buf[rfdev->rx_len] = '\0';
		complete(&rfdev->resp_recv_comp);
	}

	return len;
}

static const struct serdev_device_ops rf1276ts_serdev_client_ops = {
	.receive_buf = rf1276ts_receive_buf,
};

static int rf1276ts_probe(struct serdev_device *sdev)
{
	struct rf1276ts_device *rfdev;
	char *sz;
	int ret;

	dev_info(&sdev->dev, "Probing\n");

	rfdev = devm_kzalloc(&sdev->dev, sizeof(struct rf1276ts_device), GFP_KERNEL);
	if (!rfdev)
		return -ENOMEM;

	rfdev->serdev = sdev;
	init_completion(&rfdev->resp_recv_comp);
	serdev_device_set_drvdata(sdev, rfdev);

	ret = serdev_device_open(sdev);
	if (ret) {
		dev_err(&sdev->dev, "Failed to open (%d)\n", ret);
		return ret;
	}

	serdev_device_set_baudrate(sdev, 9600);
	serdev_device_set_flow_control(sdev, false);
	serdev_device_set_client_ops(sdev, &rf1276ts_serdev_client_ops);

	ret = rf1276ts_do_at(rfdev, HZ);
	if (ret) {
		dev_err(&sdev->dev, "Failed to verify AT interface (%d)\n", ret);
		serdev_device_close(sdev);
		return ret;
	}

	rf1276ts_reset(rfdev);

	ret = rf1276ts_get_version(rfdev, &sz, HZ);
	if (ret) {
		dev_err(&sdev->dev, "Failed to get version (%d)\n", ret);
		serdev_device_close(sdev);
		return ret;
	}

	dev_info(&sdev->dev, "firmware version: %s\n", sz);
	kfree(sz);

	dev_info(&sdev->dev, "Done.\n");

	return 0;
}

static void rf1276ts_remove(struct serdev_device *sdev)
{
	serdev_device_close(sdev);

	dev_info(&sdev->dev, "Removed\n");
}

static const struct of_device_id rf1276ts_of_match[] = {
	{ .compatible = "appconwireless,rf1276ts" },
	{}
};
MODULE_DEVICE_TABLE(of, rf1276ts_of_match);

static struct serdev_device_driver rf1276ts_serdev_driver = {
	.probe = rf1276ts_probe,
	.remove = rf1276ts_remove,
	.driver = {
		.name = "rf1276ts",
		.of_match_table = rf1276ts_of_match,
	},
};

static int __init rf1276ts_init(void)
{
	int ret;

	ret = serdev_device_driver_register(&rf1276ts_serdev_driver);
	if (ret)
		return ret;

	return 0;
}

static void __exit rf1276ts_exit(void)
{
	serdev_device_driver_unregister(&rf1276ts_serdev_driver);
}

module_init(rf1276ts_init);
module_exit(rf1276ts_exit);

MODULE_DESCRIPTION("Appcon Wireless RF1276TS serdev driver");
MODULE_AUTHOR("Andreas Färber <afaerber@suse.de>");
MODULE_LICENSE("GPL");
