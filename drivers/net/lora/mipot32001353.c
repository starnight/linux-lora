// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Mipot 32001353
 *
 * Copyright (c) 2019 Andreas Färber
 */

#include <linux/bitops.h>
#include <linux/completion.h>
#include <linux/gpio/consumer.h>
#include <linux/lora.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/rculist.h>
#include <linux/serdev.h>

struct mipot_device {
	struct serdev_device *serdev;
	struct gpio_desc *nrst_gpio;
	struct gpio_desc *nwake_gpio;

	struct list_head packet_dispatchers;
};

struct mipot_packet_dispatcher {
	struct list_head list;
	u8 cmd;
	void (*dispatchee)(const u8 *data, u8 len, struct mipot_packet_dispatcher *d);
};

static void mipot_add_dispatcher(struct mipot_device *mdev,
	struct mipot_packet_dispatcher *entry)
{
	list_add_tail_rcu(&entry->list, &mdev->packet_dispatchers);
}

static void mipot_remove_dispatcher(struct mipot_device *mdev,
	struct mipot_packet_dispatcher *entry)
{
	list_del_rcu(&entry->list);
}

struct mipot_packet_completion {
	struct mipot_packet_dispatcher disp;
	struct completion comp;
};

static void mipot_packet_dispatch_completion(const u8 *data, u8 len,
	struct mipot_packet_dispatcher *d)
{
	struct mipot_packet_completion *disp =
		container_of(d, struct mipot_packet_completion, disp);

	if (completion_done(&disp->comp))
		return;

	complete(&disp->comp);
}

static int mipot_send_command(struct mipot_device *mdev, u8 cmd,
	const void *data, u8 data_len, unsigned long timeout)
{
	const u8 *data_buf = data;
	u8 buf[3];
	u8 chksum = 0xff;
	int ret;
	u8 i;

	buf[0] = 0xAA;
	buf[1] = cmd;
	buf[2] = data_len;

	chksum += buf[0] + buf[1] + buf[2];
	for (i = 0; i < data_len; i++) {
		chksum += data_buf[i];
	}
	chksum = ~chksum;

	ret = serdev_device_write(mdev->serdev, buf, 3, timeout);
	if (ret < 0)
		return ret;
	if (ret > 0 && ret != 3) {
		dev_err(&mdev->serdev->dev, "wrote only %u bytes of header\n", ret);
		return -EIO;
	}

	ret = serdev_device_write(mdev->serdev, data, data_len, timeout);
	if (ret < 0)
		return ret;
	if (ret > 0 && ret != data_len) {
		dev_err(&mdev->serdev->dev, "wrote only %u bytes of payload\n", ret);
		return -EIO;
	}

	ret = serdev_device_write(mdev->serdev, &chksum, 1, timeout);
	if (ret < 0)
		return ret;
	if (ret > 0 && ret != 1) {
		dev_err(&mdev->serdev->dev, "wrote only %u bytes of checksum\n", ret);
		return -EIO;
	}
	return 0;
}

static int mipot_soft_reset(struct mipot_device *mdev, unsigned long timeout)
{
	struct mipot_packet_completion packet = {0};
	int ret;

	packet.disp.cmd = BIT(7) | 0x30;
	packet.disp.dispatchee = mipot_packet_dispatch_completion;
	init_completion(&packet.comp);

	mipot_add_dispatcher(mdev, &packet.disp);

	ret = mipot_send_command(mdev, 0x30, NULL, 0, timeout);
	if (ret) {
		mipot_remove_dispatcher(mdev, &packet.disp);
		return ret;
	}
	timeout = wait_for_completion_timeout(&packet.comp, timeout);
	mipot_remove_dispatcher(mdev, &packet.disp);
	if (!timeout)
		return -ETIMEDOUT;

	return 0;
}

static int mipot_receive_buf(struct serdev_device *sdev, const u8 *data, size_t count)
{
	struct mipot_device *mdev = serdev_device_get_drvdata(sdev);
	struct mipot_packet_dispatcher *e;
	u8 chksum = 0xff;
	int i;

	dev_dbg(&sdev->dev, "Receive (%zu)\n", count);

	if (data[0] != 0xAA) {
		dev_warn(&sdev->dev, "Skipping one byte\n");
		return 1;
	}

	if (count < 4)
		return 0;

	if (!(data[1] & BIT(7)))
		dev_warn(&sdev->dev, "received cmd does not have 0x80 set\n");

	if (count < 3 + data[2] + 1)
		return 0;

	for (i = 0; i < 3 + data[2]; i++) {
		chksum += data[i];
	}
	chksum = ~chksum;
	if (data[3 + data[2]] != chksum) {
		dev_warn(&sdev->dev, "discarding packet due to checksum mismatch\n");
		return 3 + data[2] + 1;
	}

	list_for_each_entry(e, &mdev->packet_dispatchers, list) {
		if (e->cmd == data[1]) {
			e->dispatchee(data, 3 + data[2] + 1, e);
			break;
		}
	}

	return 3 + data[2] + 1;
}

static const struct serdev_device_ops mipot_serdev_client_ops = {
	.receive_buf = mipot_receive_buf,
	.write_wakeup = serdev_device_write_wakeup,
};

static int mipot_probe(struct serdev_device *sdev)
{
	struct mipot_device *mdev;
	int ret;

	dev_info(&sdev->dev, "Probing");

	mdev = devm_kzalloc(&sdev->dev, sizeof(*mdev), GFP_KERNEL);
	if (!mdev)
		return -ENOMEM;

	mdev->nrst_gpio = devm_gpiod_get(&sdev->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(mdev->nrst_gpio))
		return PTR_ERR(mdev->nrst_gpio);

	mdev->nwake_gpio = devm_gpiod_get(&sdev->dev, "wake", GPIOD_OUT_LOW);
	if (IS_ERR(mdev->nwake_gpio))
		return PTR_ERR(mdev->nwake_gpio);

	mdev->serdev = sdev;
	INIT_LIST_HEAD(&mdev->packet_dispatchers);
	serdev_device_set_drvdata(sdev, mdev);

	ret = serdev_device_open(sdev);
	if (ret) {
		dev_err(&sdev->dev, "Failed to open (%d)\n", ret);
		return ret;
	}

	serdev_device_set_baudrate(sdev, 115200);
	serdev_device_set_flow_control(sdev, false);
	serdev_device_set_client_ops(sdev, &mipot_serdev_client_ops);

	if (mdev->nrst_gpio)
		gpiod_set_value(mdev->nrst_gpio, 1);

	if (mdev->nwake_gpio)
		gpiod_set_value(mdev->nwake_gpio, 1);

	ret = mipot_soft_reset(mdev, HZ);
	if (ret) {
		dev_err(&sdev->dev, "Failed to reset (%d)\n", ret);
		return ret;
	}

	dev_info(&sdev->dev, "Done.\n");

	return 0;
}

static void mipot_remove(struct serdev_device *sdev)
{
	serdev_device_close(sdev);

	dev_info(&sdev->dev, "Removed\n");
}

static const struct of_device_id mipot_of_match[] = {
	{ .compatible = "mipot,32001353" },
	{}
};
MODULE_DEVICE_TABLE(of, mipot_of_match);

static struct serdev_device_driver mipot_serdev_driver = {
	.probe = mipot_probe,
	.remove = mipot_remove,
	.driver = {
		.name = "mipot32001353",
		.of_match_table = mipot_of_match,
	},
};
module_serdev_device_driver(mipot_serdev_driver);

MODULE_DESCRIPTION("Mipot 32001353 serdev driver");
MODULE_AUTHOR("Andreas Färber <afaerber@suse.de>");
MODULE_LICENSE("GPL");
