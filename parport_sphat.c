/*
 * Add on for parport to drive the Serial & Parallel HAT
 *
 * James Barker <jbarker@elesar.co.uk>
 *
 * Copyright (C) 2017 Elesar Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/parport.h>
#include <linux/interrupt.h>
#include <linux/errno.h>

#include "parport_sphat.h"

#define SPHAT_ACK_IRQ           1 /* 1 = do use ACK handshaking IRQ, 0 = poll it */
#define SPHAT_DEBUG_REGEMU      0 /* 1 = detail data/control/status register activity, 0 = quiet */
#if SPHAT_DEBUG_REGEMU
#define dprintk(x)      pr_info x
#else
#define dprintk(x)      /* Nothing */
#endif

static struct parport *port;
static int ack_irq;
static bool ack_irq_en;
static int global_offset;
#define GPIO_TO_DESC(p) gpio_to_desc(global_offset + (p))
#define GPIO_TO_IRQ(p)  gpio_to_irq(global_offset + (p))
static struct gpio_desc *control_nstrobe;
static struct gpio_desc *control_nautolf;
static struct gpio_desc *control_init;
static struct gpio_desc *control_nselect;
static struct gpio_desc *control_bidi;
static struct gpio_desc *databit[HAT_DATA_BITS];
static struct gpio_desc *status_error;
static struct gpio_desc *status_select;
static struct gpio_desc *status_paperout;
static struct gpio_desc *status_ack;
static struct gpio_desc *status_nbusy;

static void sphat_write_data(struct parport *p, unsigned char data)
{
	size_t i;

	dprintk(("wb=%02x\n", data));
	for (i = 0; i < HAT_DATA_BITS; i++) {
		gpiod_set_value(databit[i], data & 1);
		data = data >> 1;
	}
}

static unsigned char sphat_read_data(struct parport *p)
{
	unsigned char result = 0;
	size_t i;

	for (i = 0; i < HAT_DATA_BITS; i++) {
		if (gpiod_get_value(databit[i])) {
			result |= (1 << i);
		}
	}
	dprintk(("rb=%02x\n", result));

	return result;
}

static void sphat_data_forward(struct parport *p)
{
	size_t i;

	gpiod_set_value(control_bidi, 1);
	for (i = 0; i < HAT_DATA_BITS; i++) {
		gpiod_direction_output(databit[i], 1);
	}
}

static void sphat_data_reverse(struct parport *p)
{
	size_t i;

	for (i = 0; i < HAT_DATA_BITS; i++) {
		gpiod_direction_input(databit[i]);
	}
	gpiod_set_value(control_bidi, 0);
}

static void sphat_enable_irq(struct parport *p)
{
#if SPHAT_ACK_IRQ
	unsigned long flags;

	local_irq_save(flags);
	if (!ack_irq_en) {
		enable_irq(ack_irq);
		ack_irq_en = true;
	}
	local_irq_restore(flags);
#endif
}

static void sphat_disable_irq(struct parport *p)
{
#if SPHAT_ACK_IRQ
	unsigned long flags;

	local_irq_save(flags);
	if (ack_irq_en) {
		disable_irq(ack_irq);
		ack_irq_en = false;
	}
	local_irq_restore(flags);
#endif
}

static unsigned char sphat_read_control(struct parport *p)
{
	int reg = 0;

	/* Read each bit and pack as though read all at once */
	reg |= gpiod_get_value(control_nstrobe) ? (1 << HAT_CONTROL_NSTROBE) : 0;
	reg |= gpiod_get_value(control_nautolf) ? (1 << HAT_CONTROL_NAUTOLF) : 0;
	reg |= gpiod_get_value(control_init) ? (1 << HAT_CONTROL_INIT) : 0;
	reg |= gpiod_get_value(control_nselect) ? (1 << HAT_CONTROL_NSELECT) : 0;
	reg |= gpiod_get_value(control_bidi) ? (1 << HAT_CONTROL_BIDI) : 0;
	dprintk(("rc=%02x\n", HAT_CONTROL_UNMAP(reg)));

	return HAT_CONTROL_UNMAP(reg);
}

static void sphat_write_control(struct parport *p, unsigned char new)
{
	int reg = HAT_CONTROL_MAP(new);
	unsigned char old = sphat_read_control(p);
	bool dirchange, irqchange;

	dirchange = ((old ^ new) & LPT_REG_CONTROL_BIDI) != 0;
	irqchange = ((old ^ new) & LPT_REG_CONTROL_IRQEN) != 0;
	dprintk(("wc=%02x dir=%d irq=%d\n", new, dirchange, irqchange));

	if (dirchange && (new & LPT_REG_CONTROL_BIDI)) {
		/* Was output, becoming input, need to change GPIO direction
		 * before the level translation buffer.
		 */
		sphat_data_reverse(p);
	}

	/* Tweezer the bits out one at a time */
	gpiod_set_value(control_nstrobe, reg & (1 << HAT_CONTROL_NSTROBE) ? 1 : 0);
	gpiod_set_value(control_nautolf, reg & (1 << HAT_CONTROL_NAUTOLF) ? 1 : 0);
	gpiod_set_value(control_init, reg & (1 << HAT_CONTROL_INIT) ? 1 : 0);
	gpiod_set_value(control_nselect, reg & (1 << HAT_CONTROL_NSELECT) ? 1 : 0);
	gpiod_set_value(control_bidi, reg & (1 << HAT_CONTROL_BIDI) ? 1 : 0);

	if (dirchange && (old & LPT_REG_CONTROL_BIDI)) {
		/* Was input, becoming output, need to change GPIO direction
		 * after the level translation buffer.
		 */
		sphat_data_forward(p);
	}
	if (irqchange) {
		if (new & LPT_REG_CONTROL_IRQEN) {
			/* Enable it */
			sphat_enable_irq(p);
		} else {
			/* Disable it */
			sphat_disable_irq(p);
		}
	}
}

static unsigned char sphat_frob_control(struct parport *p, unsigned char mask, unsigned char val)
{
	unsigned char old;

	old = sphat_read_control(p);
	sphat_write_control(p, (old & ~mask) ^ val);

	return old;
}

static unsigned char sphat_read_status(struct parport *p)
{
	int reg = 0;

	/* Read each bit and pack as though read all at once */
	reg |= gpiod_get_value(status_error) ? (1 << HAT_STATUS_ERROR) : 0;
	reg |= gpiod_get_value(status_select) ? (1 << HAT_STATUS_SELECT) : 0;
	reg |= gpiod_get_value(status_paperout) ? (1 << HAT_STATUS_PAPEROUT) : 0;
	reg |= gpiod_get_value(status_ack) ? (1 << HAT_STATUS_ACK) : 0;
	reg |= gpiod_get_value(status_nbusy) ? (1 << HAT_STATUS_NBUSY) : 0;
	dprintk(("rs=%02x\n", HAT_STATUS_UNMAP(reg)));

	return HAT_STATUS_UNMAP(reg);
}

static void sphat_init_state(struct pardevice *dev, struct parport_state *s)
{
	/* Although not a PC, we only need to 2 things */
	s->u.pc.ctr = LPT_REG_CONTROL_DEFAULT;
	s->u.pc.ecr = 0x0; /* Used for data */
}

static void sphat_save_state(struct parport *p, struct parport_state *s)
{
	s->u.pc.ctr = sphat_read_control(p);
	s->u.pc.ecr = sphat_read_data(p);
}

static void sphat_restore_state(struct parport *p, struct parport_state *s)
{
	sphat_write_control(p, s->u.pc.ctr);
	sphat_write_data(p, s->u.pc.ecr);
}

static int sphat_capture_offset(struct gpio_chip *chip, void *data)
{
	global_offset = chip->base;
	return 1; /* Stop enumeration */
}

static struct parport_operations sphat_ops = {
	/* SPP emulation entry points */
	.write_data	= sphat_write_data,
	.read_data	= sphat_read_data,

	.write_control	= sphat_write_control,
	.read_control	= sphat_read_control,
	.frob_control	= sphat_frob_control,

	.read_status	= sphat_read_status,

	.enable_irq	= sphat_enable_irq,
	.disable_irq	= sphat_disable_irq,

	.data_forward	= sphat_data_forward,
	.data_reverse	= sphat_data_reverse,

	.init_state	= sphat_init_state,
	.save_state	= sphat_save_state,
	.restore_state	= sphat_restore_state,

	/* EPP and ECP and IEEE1284 aren't directly supported, use common code */
	.epp_write_data	= parport_ieee1284_epp_write_data,
	.epp_read_data	= parport_ieee1284_epp_read_data,
	.epp_write_addr	= parport_ieee1284_epp_write_addr,
	.epp_read_addr	= parport_ieee1284_epp_read_addr,

	.ecp_write_data	= parport_ieee1284_ecp_write_data,
	.ecp_read_data	= parport_ieee1284_ecp_read_data,
	.ecp_write_addr	= parport_ieee1284_ecp_write_addr,

	.compat_write_data	= parport_ieee1284_write_compat,
	.nibble_read_data	= parport_ieee1284_read_nibble,
	.byte_read_data		= parport_ieee1284_read_byte,

	.owner		= THIS_MODULE,
};

static int __init parport_sphat_initialise(void)
{
	struct gpio_desc *detect;
	struct gpio_desc *bidi;
	int olddir_detect, olddir_bidi;
	int oldval_detect, oldval_bidi;
	int whenhigh, whenlow;
	struct device_node *node;
	int pid, err;
	const char *pidstr;
	size_t i;
	bool dataok = true;

	/* Look for the HAT signature in the device tree */
	node = of_find_node_by_name(NULL, "hat");
	if ((node == NULL) ||
	    (of_property_read_string(node, "product_id", &pidstr) < 0) ||
	    (kstrtoint(pidstr, 16 /* Hex */, &pid) != 0) ||
	    (pid != ELESAR_SPHAT_PID) ||
	    (of_property_match_string(node, "vendor", ELESAR_VENDOR_STR) < 0)) {
		printk(KERN_NOTICE "No HAT, or bad product match\n");
		return -EINVAL;
	}

	/* Prior to kernel 6.6 the pin offset was forced to 0 so global GPIO
	 * pin numbers mapped directly to the Pi's port pin numbers, but this
	 * was changed to make space for dynamic allocation.
	 * Use a fake call to gpio_device_find() to capture the first chip as
	 * as all our GPIO lines are on the 0th controller, then peep at the
	 * chip structure and apply that offset to our descriptors.
	 */
	gpio_device_find(NULL, sphat_capture_offset);
	detect = GPIO_TO_DESC(HAT_DETECT);
	bidi = GPIO_TO_DESC(HAT_CONTROL_BIDI);
	if ((detect == NULL) || (bidi == NULL)) {
		printk(KERN_NOTICE "Cannot probe for SPHAT\n");
		return -EINVAL;
	}

	/* Toggle CONTROL_BIDI and see if anything loops back to DETECT */
	olddir_detect = gpiod_get_direction(detect);
	oldval_detect = gpiod_get_value(detect);
	olddir_bidi = gpiod_get_direction(bidi);
	oldval_bidi = gpiod_get_value(bidi);
	gpiod_direction_input(detect);
	gpiod_direction_output(bidi, 1);
	whenhigh = gpiod_get_value(detect);
	gpiod_set_value(bidi, 0);
	whenlow = gpiod_get_value(detect);

	/* Put the lines back */
	if (olddir_detect == GPIOF_DIR_IN) {
		gpiod_direction_input(detect);
	} else {
		gpiod_direction_output(detect, oldval_bidi);
	}
	if (olddir_bidi == GPIOF_DIR_IN) {
		gpiod_direction_input(bidi);
	} else {
		gpiod_direction_output(bidi, oldval_detect);
	}

	/* Watch out for people unplugging it while turned on */
	if ((whenhigh != 1) || (whenlow != 0)) {
		printk(KERN_NOTICE "Cannot find SPHAT\n");
		return -EIO;
	}

	/* Grab the IOs */
	control_nstrobe = GPIO_TO_DESC(HAT_CONTROL_NSTROBE);
	control_nautolf = GPIO_TO_DESC(HAT_CONTROL_NAUTOLF);
	control_init = GPIO_TO_DESC(HAT_CONTROL_INIT);
	control_nselect = GPIO_TO_DESC(HAT_CONTROL_NSELECT);
	control_bidi = GPIO_TO_DESC(HAT_CONTROL_BIDI);
	for (i = 0; i < HAT_DATA_BITS; i++) {
		databit[i] = GPIO_TO_DESC(HAT_DATA_SHIFT + i);
		if (databit[i] != NULL) {
			gpiod_direction_output(databit[i], 1);
		} else {
			dataok = false;
		}
	}
	status_error = GPIO_TO_DESC(HAT_STATUS_ERROR);
	status_select = GPIO_TO_DESC(HAT_STATUS_SELECT);
	status_paperout = GPIO_TO_DESC(HAT_STATUS_PAPEROUT);
	status_ack = GPIO_TO_DESC(HAT_STATUS_ACK);
	status_nbusy = GPIO_TO_DESC(HAT_STATUS_NBUSY);
	if (status_error && status_select && status_paperout &&
	    status_ack && status_nbusy &&
	    dataok &&
	    control_nstrobe && control_nautolf && control_init &&
	    control_nselect && control_bidi) {
		/* Inputs */
		gpiod_direction_input(status_error);
		gpiod_direction_input(status_select);
		gpiod_direction_input(status_paperout);
		gpiod_direction_input(status_ack);
		gpiod_direction_input(status_nbusy);
		/* Initialise the lines to the equivalent of LPT_REG_CONTROL_DEFAULT */
		gpiod_direction_output(control_nstrobe, 0);
		gpiod_direction_output(control_nautolf, 0);
		gpiod_direction_output(control_init, 0);
		gpiod_direction_output(control_nselect, 1);
		gpiod_direction_output(control_bidi, 1);
	} else {
		printk(KERN_NOTICE "Cannot lookup SPHAT IO lines\n");
		return -EINVAL;
	}

	/* Register with parport */
	port = parport_register_port(-1, PARPORT_IRQ_NONE, PARPORT_DMA_NONE, &sphat_ops);
	if (port == NULL) {
		printk(KERN_INFO "SPHAT parport not registered\n");
		return -EBUSY;
	}

#if SPHAT_ACK_IRQ
	/* Get on interrupt on rising edge of nACK */
	ack_irq = GPIO_TO_IRQ(HAT_STATUS_ACK);
	err = 0;
	if (ack_irq >= 0) {
		err = request_irq(ack_irq, parport_irq_handler, 
		                  IRQF_TRIGGER_RISING, port->name, port);
	}
	if ((ack_irq < 0) || err) {
		printk(KERN_NOTICE "Couldn't get SPHAT nACK interrupt\n");
		parport_remove_port(port);
		return -EBUSY;
	}
	ack_irq_en = true;
#else
	ack_irq = PARPORT_IRQ_NONE;
	err = 0;
#endif
	port->irq = ack_irq;

	/* Open for business */
	parport_announce_port(port);

	printk(KERN_INFO "Found Elesar PID=%04X, parport started OK\n", pid);
	return 0;
}

static void __exit parport_sphat_finalise(void)
{
	/* No need to return the pins to their previous state, since
	 * we check very carefully during initialisation that the HAT is
	 * there. Just get off the interrupt.
	 */
	printk(KERN_INFO "SPHAT parport quitting\n");
	if (port->irq != PARPORT_IRQ_NONE) {
		free_irq(port->irq, port);
	}
	parport_remove_port(port);
}

module_init(parport_sphat_initialise);
module_exit(parport_sphat_finalise);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("James Barker <jbarker@elesar.co.uk>");
MODULE_DESCRIPTION("Elesar Serial Parallel HAT parallel port driver");
