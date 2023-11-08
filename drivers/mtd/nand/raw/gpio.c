// SPDX-License-Identifier: GPL-2.0-only
/*
 * Updated, and converted to generic GPIO based driver by Russell King.
 *
 * Written by Ben Dooks <ben@simtec.co.uk>
 *   Based on 2.4 version by Mark Whittaker
 *
 * © 2004 Simtec Electronics
 *
 * Device driver for NAND flash that uses a memory mapped interface to
 * read/write the NAND commands and data, and GPIO pins for control signals
 * (the DT binding refers to this as "GPIO assisted NAND flash")
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/io.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/rawnand.h>
#include <linux/mtd/partitions.h>
#include <linux/property.h>
#include <linux/delay.h>

struct gpiomtd {
	struct nand_controller	base;
	void __iomem		*io;
	void __iomem		*io_sync;
	struct nand_chip	nand_chip;
	struct gpio_desc *ce; /* Optional chip enable */
	struct gpio_desc *cle;
	struct gpio_desc *ale;
	struct gpio_desc *rdy;
	struct gpio_desc *wp; /* Optional write protection */
};

static inline struct gpiomtd *gpio_nand_getpriv(struct mtd_info *mtd)
{
	return container_of(mtd_to_nand(mtd), struct gpiomtd, nand_chip);
}


#ifdef CONFIG_ARM
/* gpio_nand_dosync()
 *
 * Make sure the GPIO state changes occur in-order with writes to NAND
 * memory region.
 * Needed on PXA due to bus-reordering within the SoC itself (see section on
 * I/O ordering in PXA manual (section 2.3, p35)
 */
static void gpio_nand_dosync(struct gpiomtd *gpiomtd)
{
	unsigned long tmp;

	if (gpiomtd->io_sync) {
		/*
		 * Linux memory barriers don't cater for what's required here.
		 * What's required is what's here - a read from a separate
		 * region with a dependency on that read.
		 */
		tmp = readl(gpiomtd->io_sync);
		asm volatile("mov %1, %0\n" : "=r" (tmp) : "r" (tmp));
	}
}
#else
static inline void gpio_nand_dosync(struct gpiomtd *gpiomtd) {}
#endif

static int gpio_nand_exec_instr(struct nand_chip *chip,
				const struct nand_op_instr *instr)
{
	struct gpiomtd *gpiomtd = gpio_nand_getpriv(nand_to_mtd(chip));
	unsigned int i;

	switch (instr->type) {
	case NAND_OP_CMD_INSTR:
		gpio_nand_dosync(gpiomtd);
		gpiod_set_value(gpiomtd->cle, 1);
		gpio_nand_dosync(gpiomtd);
		writeb(instr->ctx.cmd.opcode, gpiomtd->io);
		gpio_nand_dosync(gpiomtd);
		gpiod_set_value(gpiomtd->cle, 0);
		return 0;

	case NAND_OP_ADDR_INSTR:
		gpio_nand_dosync(gpiomtd);
		gpiod_set_value(gpiomtd->ale, 1);
		gpio_nand_dosync(gpiomtd);
		for (i = 0; i < instr->ctx.addr.naddrs; i++)
			writeb(instr->ctx.addr.addrs[i], gpiomtd->io);
		gpio_nand_dosync(gpiomtd);
		gpiod_set_value(gpiomtd->ale, 0);
		return 0;

	case NAND_OP_DATA_IN_INSTR:
		gpio_nand_dosync(gpiomtd);
		if ((chip->options & NAND_BUSWIDTH_16) &&
		    !instr->ctx.data.force_8bit)
			ioread16_rep(gpiomtd->io, instr->ctx.data.buf.in,
				     instr->ctx.data.len / 2);
		else
			ioread8_rep(gpiomtd->io, instr->ctx.data.buf.in,
				    instr->ctx.data.len);
		return 0;

	case NAND_OP_DATA_OUT_INSTR:
		gpio_nand_dosync(gpiomtd);
		if ((chip->options & NAND_BUSWIDTH_16) &&
		    !instr->ctx.data.force_8bit)
			iowrite16_rep(gpiomtd->io, instr->ctx.data.buf.out,
				      instr->ctx.data.len / 2);
		else
			iowrite8_rep(gpiomtd->io, instr->ctx.data.buf.out,
				     instr->ctx.data.len);
		return 0;

	case NAND_OP_WAITRDY_INSTR:
		if (!gpiomtd->rdy)
			return nand_soft_waitrdy(chip, instr->ctx.waitrdy.timeout_ms);

		return nand_gpio_waitrdy(chip, gpiomtd->rdy,
					 instr->ctx.waitrdy.timeout_ms);

	default:
		return -EINVAL;
	}

	return 0;
}

static int gpio_nand_exec_op(struct nand_chip *chip,
			     const struct nand_operation *op,
			     bool check_only)
{
	struct gpiomtd *gpiomtd = gpio_nand_getpriv(nand_to_mtd(chip));
	unsigned int i;
	int ret = 0;

	if (check_only)
		return 0;

	gpio_nand_dosync(gpiomtd);
	gpiod_set_value(gpiomtd->ce, 1);
	for (i = 0; i < op->ninstrs; i++) {
		ret = gpio_nand_exec_instr(chip, &op->instrs[i]);
		if (ret)
			break;

		if (op->instrs[i].delay_ns)
			ndelay(op->instrs[i].delay_ns);
	}
	gpio_nand_dosync(gpiomtd);
	gpiod_set_value(gpiomtd->ce, 0);

	return ret;
}

static int gpio_nand_attach_chip(struct nand_chip *chip)
{
	if (chip->ecc.engine_type == NAND_ECC_ENGINE_TYPE_SOFT &&
	    chip->ecc.algo == NAND_ECC_ALGO_UNKNOWN)
		chip->ecc.algo = NAND_ECC_ALGO_HAMMING;

	return 0;
}

static const struct nand_controller_ops gpio_nand_ops = {
	.exec_op = gpio_nand_exec_op,
	.attach_chip = gpio_nand_attach_chip,
};

static const struct of_device_id gpio_nand_id_table[] = {
	{ .compatible = "gpio-control-nand" },
	{}
};
MODULE_DEVICE_TABLE(of, gpio_nand_id_table);

static int gpio_nand_get_config(struct device *dev,
				struct nand_chip *chip)
{
	u32 val;

	/* The preferred binding takes precedence */
	if (!device_property_read_u32(dev, "nand-bus-width", &val)) {
		if (val == 16) {
			chip->options |= NAND_BUSWIDTH_16;
		} else if (val != 8) {
			dev_err(dev, "invalid nand-bus-width %u\n", val);
			return -EINVAL;
		}
	} else if (!device_property_read_u32(dev, "bank-width", &val)) {
		if (val == 2) {
			chip->options |= NAND_BUSWIDTH_16;
		} else if (val != 1) {
			dev_err(dev, "invalid bank-width %u\n", val);
			return -EINVAL;
		}
	}

	return 0;
}

static struct resource *gpio_nand_get_io_sync_prop(struct device *dev)
{
	struct resource *r;
	u64 addr;

	if (device_property_read_u64(dev, "gpio-control-nand,io-sync-reg", &addr))
		return NULL;

	r = devm_kzalloc(dev, sizeof(*r), GFP_KERNEL);
	if (!r)
		return NULL;

	r->start = addr;
	r->end = r->start + 0x3;
	r->flags = IORESOURCE_MEM;

	return r;
}

static inline struct resource *
gpio_nand_get_io_sync(struct platform_device *pdev)
{
	struct resource *r = gpio_nand_get_io_sync_prop(&pdev->dev);

	if (r)
		return r;

	return platform_get_resource(pdev, IORESOURCE_MEM, 1);
}

static void gpio_nand_remove(struct platform_device *pdev)
{
	struct gpiomtd *gpiomtd = platform_get_drvdata(pdev);
	struct nand_chip *chip = &gpiomtd->nand_chip;
	int ret;

	ret = mtd_device_unregister(nand_to_mtd(chip));
	WARN_ON(ret);
	nand_cleanup(chip);

	/* Enable write protection and disable the chip */
	if (gpiomtd->wp && !IS_ERR(gpiomtd->wp))
		gpiod_set_value(gpiomtd->wp, 1);
	if (gpiomtd->ce && !IS_ERR(gpiomtd->ce))
		gpiod_set_value(gpiomtd->ce, 0);
}

static int gpio_nand_probe(struct platform_device *pdev)
{
	struct gpiomtd *gpiomtd;
	struct nand_chip *chip;
	struct mtd_info *mtd;
	struct resource *res;
	struct device *dev = &pdev->dev;
	int ret = 0;

	gpiomtd = devm_kzalloc(dev, sizeof(*gpiomtd), GFP_KERNEL);
	if (!gpiomtd)
		return -ENOMEM;

	chip = &gpiomtd->nand_chip;

	gpiomtd->io = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(gpiomtd->io))
		return PTR_ERR(gpiomtd->io);

	res = gpio_nand_get_io_sync(pdev);
	if (res) {
		gpiomtd->io_sync = devm_ioremap_resource(dev, res);
		if (IS_ERR(gpiomtd->io_sync))
			return PTR_ERR(gpiomtd->io_sync);
	}

	ret = gpio_nand_get_config(dev, chip);
	if (ret)
		return ret;

	/* Just enable the chip */
	gpiomtd->ce = devm_gpiod_get_optional(dev, "ce", GPIOD_OUT_HIGH);
	if (IS_ERR(gpiomtd->ce))
		return PTR_ERR(gpiomtd->ce);

	/* We disable write protection once we know probe() will succeed */
	gpiomtd->wp = devm_gpiod_get_optional(dev, "wp", GPIOD_OUT_HIGH);
	if (IS_ERR(gpiomtd->wp)) {
		ret = PTR_ERR(gpiomtd->wp);
		goto out_ce;
	}

	gpiomtd->ale = devm_gpiod_get(dev, "ale", GPIOD_OUT_LOW);
	if (IS_ERR(gpiomtd->ale)) {
		ret = PTR_ERR(gpiomtd->ale);
		goto out_ce;
	}

	gpiomtd->cle = devm_gpiod_get(dev, "cle", GPIOD_OUT_LOW);
	if (IS_ERR(gpiomtd->cle)) {
		ret = PTR_ERR(gpiomtd->cle);
		goto out_ce;
	}

	gpiomtd->rdy = devm_gpiod_get_optional(dev, "rdy", GPIOD_IN);
	if (IS_ERR(gpiomtd->rdy)) {
		ret = PTR_ERR(gpiomtd->rdy);
		goto out_ce;
	}

	nand_controller_init(&gpiomtd->base);
	gpiomtd->base.ops = &gpio_nand_ops;

	nand_set_flash_node(chip, pdev->dev.of_node);
	chip->controller	= &gpiomtd->base;

	mtd			= nand_to_mtd(chip);
	mtd->dev.parent		= dev;

	platform_set_drvdata(pdev, gpiomtd);

	/* Disable write protection, if wired up */
	if (gpiomtd->wp && !IS_ERR(gpiomtd->wp))
		gpiod_direction_output(gpiomtd->wp, 0);

	/*
	 * This driver assumes that the default ECC engine should be TYPE_SOFT.
	 * Set ->engine_type before registering the NAND devices in order to
	 * provide a driver specific default value.
	 */
	chip->ecc.engine_type = NAND_ECC_ENGINE_TYPE_SOFT;

	ret = nand_scan(chip, 1);
	if (ret)
		goto err_wp;

	ret = mtd_device_register(mtd, NULL, 0);
	if (!ret)
		return 0;

err_wp:
	if (gpiomtd->wp && !IS_ERR(gpiomtd->wp))
		gpiod_set_value(gpiomtd->wp, 1);
out_ce:
	if (gpiomtd->ce && !IS_ERR(gpiomtd->ce))
		gpiod_set_value(gpiomtd->ce, 0);

	return ret;
}

static struct platform_driver gpio_nand_driver = {
	.probe		= gpio_nand_probe,
	.remove_new	= gpio_nand_remove,
	.driver		= {
		.name	= "gpio-nand",
		.of_match_table = gpio_nand_id_table,
	},
};

module_platform_driver(gpio_nand_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ben Dooks <ben@simtec.co.uk>");
MODULE_DESCRIPTION("GPIO NAND Driver");
