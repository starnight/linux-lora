// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Semtech PicoCell gateway USB interface
 *
 * Copyright (c) 2018-2019 Andreas Färber
 */

#define pr_fmt(fmt) "picocell: " fmt

#include <linux/module.h>
#include <linux/of.h>
#include <linux/serial.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/usb.h>
#include <linux/usb/cdc.h>

#define PICO_VID 0x0483
#define PICO_PID 0x5740

static struct usb_driver *picogw_get_acm_driver(struct usb_interface *iface)
{
	struct device_driver *drv;

	drv = driver_find("cdc_acm", iface->dev.bus);
	if (!drv)
		return NULL;

	return to_usb_driver(drv);
}

static void picogw_kobj_release(struct kobject *kobj)
{
	struct device_node *node = container_of(kobj, struct device_node, kobj);
	struct property *prop;

	prop = node->properties;
	while (prop) {
		struct property *next = prop->next;
		kfree(prop);
		prop = next;
	}

	kfree(node);
}

static struct kobj_type picogw_kobj_type = {
	.release = picogw_kobj_release,
};

static u32 picogw_radio_a_reg = cpu_to_be32(0);
static u32 picogw_radio_b_reg = cpu_to_be32(1);

static int picogw_fake_of_nodes(struct device *dev)
{
	struct device_node *node = NULL;
	struct device_node *child, *spi, *radio_a, *radio_b;
	struct property *prop;

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return -ENOMEM;
	node->name = "<NULL>";
	node->full_name = "usb0483,5740";
	node->type = "<NULL>";
	kobject_init(&node->kobj, &picogw_kobj_type);
	node->fwnode.ops = &of_fwnode_ops;

	child = kzalloc(sizeof(*child), GFP_KERNEL);
	if (!child) {
		of_node_put(node);
		return -ENOMEM;
	}
	child->name = "lora";
	child->full_name = "lora";
	child->type = "<NULL>";
	child->parent = node;
	kobject_init(&child->kobj, &picogw_kobj_type);
	child->fwnode.ops = &of_fwnode_ops;
	node->child = child;

	prop = kzalloc(sizeof(*prop), GFP_KERNEL);
	if (!prop) {
		of_node_put(child);
		of_node_put(node);
		return -ENOMEM;
	}
	prop->name = "compatible";
	prop->value = "semtech,lora-picocell";
	prop->length = 22;
	child->properties = prop;

	spi = kzalloc(sizeof(*spi), GFP_KERNEL);
	if (!spi) {
		of_node_put(child);
		of_node_put(node);
		return -ENOMEM;
	}
	spi->name = "radio-spi";
	spi->full_name = "radio-spi";
	spi->type = "<NULL>";
	spi->parent = child;
	kobject_init(&spi->kobj, &picogw_kobj_type);
	spi->fwnode.ops = &of_fwnode_ops;
	child->child = spi;

	radio_a = kzalloc(sizeof(*radio_a), GFP_KERNEL);
	if (!radio_a) {
		of_node_put(spi);
		of_node_put(child);
		of_node_put(node);
		return -ENOMEM;
	}
	radio_a->name = "lora@0";
	radio_a->full_name = "lora@0";
	radio_a->type = "<NULL>";
	radio_a->parent = spi;
	kobject_init(&radio_a->kobj, &picogw_kobj_type);
	radio_a->fwnode.ops = &of_fwnode_ops;
	spi->child = radio_a;

	prop = kzalloc(sizeof(*prop), GFP_KERNEL);
	if (!prop) {
		of_node_put(radio_a);
		of_node_put(spi);
		of_node_put(child);
		of_node_put(node);
		return -ENOMEM;
	}
	prop->name = "compatible";
	prop->value = "semtech,sx1257";
	prop->length = 15;
	radio_a->properties = prop;

	prop = kzalloc(sizeof(*prop), GFP_KERNEL);
	if (!prop) {
		of_node_put(radio_a);
		of_node_put(spi);
		of_node_put(child);
		of_node_put(node);
		return -ENOMEM;
	}
	prop->name = "reg";
	prop->value = &picogw_radio_a_reg;
	prop->length = 4;
	radio_a->properties->next = prop;

	radio_b = kzalloc(sizeof(*radio_b), GFP_KERNEL);
	if (!radio_b) {
		of_node_put(radio_a);
		of_node_put(spi);
		of_node_put(child);
		of_node_put(node);
		return -ENOMEM;
	}
	radio_b->name = "lora@1";
	radio_b->full_name = "Lora@1";
	radio_b->type = "<NULL>";
	radio_b->parent = spi;
	kobject_init(&radio_b->kobj, &picogw_kobj_type);
	radio_b->fwnode.ops = &of_fwnode_ops;
	radio_a->sibling = radio_b;

	prop = kzalloc(sizeof(*prop), GFP_KERNEL);
	if (!prop) {
		of_node_put(radio_b);
		of_node_put(radio_a);
		of_node_put(spi);
		of_node_put(child);
		of_node_put(node);
		return -ENOMEM;
	}
	prop->name = "compatible";
	prop->value = "semtech,sx1257";
	prop->length = 15;
	radio_b->properties = prop;

	prop = kzalloc(sizeof(*prop), GFP_KERNEL);
	if (!prop) {
		of_node_put(radio_a);
		of_node_put(spi);
		of_node_put(child);
		of_node_put(node);
		return -ENOMEM;
	}
	prop->name = "reg";
	prop->value = &picogw_radio_b_reg;
	prop->length = 4;
	radio_b->properties->next = prop;

	dev->of_node = node;

	return 0;
}

static void picogw_cleanup_of_nodes(struct device *dev)
{
	if (dev->of_node->parent)
		return;

	of_node_put(dev->of_node->child->child->child->sibling); /* lora@1 */
	of_node_put(dev->of_node->child->child->child); /* lora@0 */
	of_node_put(dev->of_node->child->child); /* radio-spi*/
	of_node_put(dev->of_node->child); /* serdev */
	of_node_put(dev->of_node); /* usb */
	dev->of_node = NULL;
}

static int picogw_probe(struct usb_interface *interface,
	const struct usb_device_id *id)
{
	struct usb_driver *drv;
	int ret;

	drv = picogw_get_acm_driver(interface);
	if (!drv) {
		dev_err(&interface->dev, "driver_find failed\n");
		return -EPROBE_DEFER;
	}

	if (!interface->dev.of_node) {
		dev_dbg(&interface->dev, "no of_node\n");
		ret = picogw_fake_of_nodes(&interface->dev);
		if (ret)
			return ret;
	}

	ret = drv->probe(interface, id);
	if (ret) {
		picogw_cleanup_of_nodes(&interface->dev);
		return ret;
	}

	return 0;
}

static void picogw_disconnect(struct usb_interface *intf)
{
	struct usb_driver *drv = picogw_get_acm_driver(intf);

	if (drv)
		drv->disconnect(intf);
	else
		dev_warn(&intf->dev, "%s: failed to get cdc_acm driver\n", __func__);

	picogw_cleanup_of_nodes(&intf->dev);
}

static int picogw_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct usb_driver *drv = picogw_get_acm_driver(intf);

	if (!drv) {
		dev_err(&intf->dev, "%s: failed to get cdc_acm driver\n", __func__);
		return -ENODEV;
	}

	return drv->suspend(intf, message);
}

static int picogw_resume(struct usb_interface *intf)
{
	struct usb_driver *drv = picogw_get_acm_driver(intf);

	if (!drv) {
		dev_err(&intf->dev, "%s: failed to get cdc_acm driver\n", __func__);
		return -ENODEV;
	}

	return drv->resume(intf);
}

static int picogw_reset_resume(struct usb_interface *intf)
{
	struct usb_driver *drv = picogw_get_acm_driver(intf);

	if (!drv) {
		dev_err(&intf->dev, "%s: failed to get cdc_acm driver\n", __func__);
		return -ENODEV;
	}

	return drv->reset_resume(intf);
}

static int picogw_pre_reset(struct usb_interface *intf)
{
	struct usb_driver *drv = picogw_get_acm_driver(intf);

	if (!drv) {
		dev_err(&intf->dev, "%s: failed to get cdc_acm driver\n", __func__);
		return -ENODEV;
	}

	return drv->pre_reset(intf);
}

static const struct usb_device_id picogw_usb_id_table[] = {
	{ USB_DEVICE_AND_INTERFACE_INFO(PICO_VID, PICO_PID,
	  USB_CLASS_COMM, USB_CDC_SUBCLASS_ACM, USB_CDC_ACM_PROTO_AT_V25TER) },
	{}
};
MODULE_DEVICE_TABLE(usb, picogw_usb_id_table);

static struct usb_driver picogw_usb_driver = {
	.name = "lora-picogw-usb",
	.probe = picogw_probe,
	.disconnect = picogw_disconnect,
	.suspend = picogw_suspend,
	.resume = picogw_resume,
	.reset_resume = picogw_reset_resume,
	.pre_reset = picogw_pre_reset,
	.id_table = picogw_usb_id_table,
	.supports_autosuspend = 1,
	.disable_hub_initiated_lpm = 1,
};

static int __init picogw_init(void)
{
	int ret;

	ret = usb_register(&picogw_usb_driver);
	if (ret < 0){
		pr_err("usb_register failed (%d)\n", ret);
		return ret;
	}

	return 0;
}
module_init(picogw_init);

static void __exit picogw_exit(void)
{
	usb_deregister(&picogw_usb_driver);
}
module_exit(picogw_exit);

MODULE_LICENSE("GPL");
