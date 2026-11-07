// SPDX-License-Identifier: GPL-2.0-only
/*
 * PCI glue driver for SPI PXA2xx compatible controllers.
 * CE4100's SPI device is more or less the same one as found on PXA.
 *
 * Copyright (C) 2016, 2021 Intel Corporation
 */
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/sprintf.h>
#include <linux/string.h>
#include <linux/types.h>

#include <linux/dmaengine.h>
#include <linux/platform_data/dma-dw.h>

#include "spi-pxa2xx.h"

#define PCI_DEVICE_ID_INTEL_QUARK_X1000		0x0935
#define PCI_DEVICE_ID_INTEL_BYT			0x0f0e
#define PCI_DEVICE_ID_INTEL_MRFLD		0x1194
#define PCI_DEVICE_ID_INTEL_BSW0		0x228e
#define PCI_DEVICE_ID_INTEL_BSW1		0x2290
#define PCI_DEVICE_ID_INTEL_BSW2		0x22ac
#define PCI_DEVICE_ID_INTEL_CE4100		0x2e6a
#define PCI_DEVICE_ID_INTEL_LPT0_0		0x9c65
#define PCI_DEVICE_ID_INTEL_LPT0_1		0x9c66
#define PCI_DEVICE_ID_INTEL_LPT1_0		0x9ce5
#define PCI_DEVICE_ID_INTEL_LPT1_1		0x9ce6

struct pxa_spi_info {
	int (*setup)(struct pci_dev *pdev, struct pxa2xx_spi_controller *c);
};

static struct dw_dma_slave byt_tx_param = { .dst_id = 0 };
static struct dw_dma_slave byt_rx_param = { .src_id = 1 };

static struct dw_dma_slave mrfld3_tx_param = { .dst_id = 15 };
static struct dw_dma_slave mrfld3_rx_param = { .src_id = 14 };
static struct dw_dma_slave mrfld5_tx_param = { .dst_id = 13 };
static struct dw_dma_slave mrfld5_rx_param = { .src_id = 12 };
static struct dw_dma_slave mrfld6_tx_param = { .dst_id = 11 };
static struct dw_dma_slave mrfld6_rx_param = { .src_id = 10 };

static struct dw_dma_slave bsw0_tx_param = { .dst_id = 0 };
static struct dw_dma_slave bsw0_rx_param = { .src_id = 1 };
static struct dw_dma_slave bsw1_tx_param = { .dst_id = 6 };
static struct dw_dma_slave bsw1_rx_param = { .src_id = 7 };
static struct dw_dma_slave bsw2_tx_param = { .dst_id = 8 };
static struct dw_dma_slave bsw2_rx_param = { .src_id = 9 };

static struct dw_dma_slave lpt1_tx_param = { .dst_id = 0 };
static struct dw_dma_slave lpt1_rx_param = { .src_id = 1 };
static struct dw_dma_slave lpt0_tx_param = { .dst_id = 2 };
static struct dw_dma_slave lpt0_rx_param = { .src_id = 3 };

static void pxa2xx_spi_pci_clk_unregister(void *clk)
{
	clk_unregister(clk);
}

static int pxa2xx_spi_pci_clk_register(struct pci_dev *dev, struct ssp_device *ssp,
				       unsigned long rate)
{
	char buf[40];

	snprintf(buf, sizeof(buf), "pxa2xx-spi.%d", ssp->port_id);
	ssp->clk = clk_register_fixed_rate(&dev->dev, buf, NULL, 0, rate);
	if (IS_ERR(ssp->clk))
		return PTR_ERR(ssp->clk);

	return devm_add_action_or_reset(&dev->dev, pxa2xx_spi_pci_clk_unregister, ssp->clk);
}

static bool lpss_dma_filter(struct dma_chan *chan, void *param)
{
	struct dw_dma_slave *dws = param;

	if (dws->dma_dev != chan->device->dev)
		return false;

	chan->private = dws;
	return true;
}

static void lpss_dma_put_device(void *dma_dev)
{
	pci_dev_put(dma_dev);
}

static int lpss_spi_setup(struct pci_dev *dev, struct pxa2xx_spi_controller *c)
{
	struct ssp_device *ssp = &c->ssp;
	struct dw_dma_slave *tx, *rx;
	struct pci_dev *dma_dev;
	int ret;

	switch (dev->device) {
	case PCI_DEVICE_ID_INTEL_BYT:
		ssp->type = LPSS_BYT_SSP;
		ssp->port_id = 0;
		c->tx_param = &byt_tx_param;
		c->rx_param = &byt_rx_param;
		break;
	case PCI_DEVICE_ID_INTEL_BSW0:
		ssp->type = LPSS_BSW_SSP;
		ssp->port_id = 0;
		c->tx_param = &bsw0_tx_param;
		c->rx_param = &bsw0_rx_param;
		break;
	case PCI_DEVICE_ID_INTEL_BSW1:
		ssp->type = LPSS_BSW_SSP;
		ssp->port_id = 1;
		c->tx_param = &bsw1_tx_param;
		c->rx_param = &bsw1_rx_param;
		break;
	case PCI_DEVICE_ID_INTEL_BSW2:
		ssp->type = LPSS_BSW_SSP;
		ssp->port_id = 2;
		c->tx_param = &bsw2_tx_param;
		c->rx_param = &bsw2_rx_param;
		break;
	case PCI_DEVICE_ID_INTEL_LPT0_0:
	case PCI_DEVICE_ID_INTEL_LPT1_0:
		ssp->type = LPSS_LPT_SSP;
		ssp->port_id = 0;
		c->tx_param = &lpt0_tx_param;
		c->rx_param = &lpt0_rx_param;
		break;
	case PCI_DEVICE_ID_INTEL_LPT0_1:
	case PCI_DEVICE_ID_INTEL_LPT1_1:
		ssp->type = LPSS_LPT_SSP;
		ssp->port_id = 1;
		c->tx_param = &lpt1_tx_param;
		c->rx_param = &lpt1_rx_param;
		break;
	default:
		return -ENODEV;
	}

	c->num_chipselect = 1;

	ret = pxa2xx_spi_pci_clk_register(dev, ssp, 50000000);
	if (ret)
		return ret;

	dma_dev = pci_get_slot(dev->bus, PCI_DEVFN(PCI_SLOT(dev->devfn), 0));
	ret = devm_add_action_or_reset(&dev->dev, lpss_dma_put_device, dma_dev);
	if (ret)
		return ret;

	tx = c->tx_param;
	tx->dma_dev = &dma_dev->dev;
	tx->m_master = 0;
	tx->p_master = 1;

	rx = c->rx_param;
	rx->dma_dev = &dma_dev->dev;
	rx->m_master = 0;
	rx->p_master = 1;

	c->dma_filter = lpss_dma_filter;
	c->dma_burst_size = 1;
	c->enable_dma = 1;
	return 0;
}

/*
 * MacBook 12" (MacBook8,1) fix: LPSS private register context across S3.
 *
 * On Lynx/Wildcat Point the LPSS block keeps a private register window at
 * BAR0 + 0x800 (clock divider at +0x00, reset release at +0x04, ...).
 * Firmware programs it at boot. When the LPSS devices are enumerated via ACPI,
 * acpi_lpss.c saves/restores this context (LPSS_SAVE_CTX), but in PCI mode
 * nothing does: after S3 the window reads back as all zeros, the SSP is held
 * in reset with no clock, and every transfer times out (-110). On the
 * MacBook 12" this kills the applespi keyboard/touchpad after every resume.
 *
 * Save the same 9-register context acpi_lpss saves on suspend and restore it
 * (reset release last) before the core driver resumes.
 */
#define LPSS_LPT_PRIV_OFFSET	0x800
#define LPSS_LPT_PRIV_COUNT	9
#define LPSS_LPT_PRIV_RESETS	0x04
#define LPSS_LPT_RESETS_FUNC_IDMA	0x3

struct lpss_ctx {
	void __iomem *priv;
	u32 regs[LPSS_LPT_PRIV_COUNT];
	bool saved;
};

static void lpss_ctx_release(struct device *dev, void *res) { }

static struct lpss_ctx *lpss_ctx_get(struct device *dev)
{
	return devres_find(dev, lpss_ctx_release, NULL, NULL);
}

static int lpss_ctx_init(struct pci_dev *dev, struct ssp_device *ssp)
{
	struct lpss_ctx *ctx;

	if (ssp->type != LPSS_LPT_SSP)
		return 0;

	ctx = devres_alloc(lpss_ctx_release, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	ctx->priv = ssp->mmio_base + LPSS_LPT_PRIV_OFFSET;
	devres_add(&dev->dev, ctx);
	return 0;
}

static void lpss_ctx_save(struct device *dev)
{
	struct lpss_ctx *ctx = lpss_ctx_get(dev);
	int i;

	/*
	 * If already runtime-suspended (D3hot) MMIO is not readable; keep the
	 * context captured by lpss_pci_runtime_suspend().
	 */
	if (!ctx || pm_runtime_suspended(dev))
		return;
	for (i = 0; i < LPSS_LPT_PRIV_COUNT; i++)
		ctx->regs[i] = readl(ctx->priv + i * 4);
	ctx->saved = true;
}

static void lpss_ctx_restore(struct device *dev)
{
	struct lpss_ctx *ctx = lpss_ctx_get(dev);
	int i, r = LPSS_LPT_PRIV_RESETS / 4;

	if (!ctx || !ctx->saved)
		return;
	/* Context intact (e.g. s2idle, or runtime D3 that kept power): nothing to do. */
	if (readl(ctx->priv + LPSS_LPT_PRIV_RESETS) == ctx->regs[r])
		return;

	for (i = 0; i < LPSS_LPT_PRIV_COUNT; i++)
		if (i != r)
			writel(ctx->regs[i], ctx->priv + i * 4);
	writel(ctx->regs[r] | LPSS_LPT_RESETS_FUNC_IDMA,
	       ctx->priv + LPSS_LPT_PRIV_RESETS);
	dev_info(dev, "restored LPSS private context after power loss\n");
}

static int lpss_pci_suspend(struct device *dev)
{
	lpss_ctx_save(dev);
	return pxa2xx_spi_pm_ops.suspend(dev);
}

static int lpss_pci_resume(struct device *dev)
{
	lpss_ctx_restore(dev);
	return pxa2xx_spi_pm_ops.resume(dev);
}

static int lpss_pci_freeze(struct device *dev)
{
	lpss_ctx_save(dev);
	return pxa2xx_spi_pm_ops.freeze(dev);
}

static int lpss_pci_thaw(struct device *dev)
{
	lpss_ctx_restore(dev);
	return pxa2xx_spi_pm_ops.thaw(dev);
}

static int lpss_pci_poweroff(struct device *dev)
{
	lpss_ctx_save(dev);
	return pxa2xx_spi_pm_ops.poweroff(dev);
}

static int lpss_pci_restore(struct device *dev)
{
	lpss_ctx_restore(dev);
	return pxa2xx_spi_pm_ops.restore(dev);
}

static int lpss_pci_runtime_suspend(struct device *dev)
{
	lpss_ctx_save(dev);
	return pxa2xx_spi_pm_ops.runtime_suspend(dev);
}

static int lpss_pci_runtime_resume(struct device *dev)
{
	lpss_ctx_restore(dev);
	return pxa2xx_spi_pm_ops.runtime_resume(dev);
}

static const struct dev_pm_ops lpss_pci_pm_ops = {
	.suspend	= lpss_pci_suspend,
	.resume		= lpss_pci_resume,
	.freeze		= lpss_pci_freeze,
	.thaw		= lpss_pci_thaw,
	.poweroff	= lpss_pci_poweroff,
	.restore	= lpss_pci_restore,
	RUNTIME_PM_OPS(lpss_pci_runtime_suspend, lpss_pci_runtime_resume, NULL)
};

static const struct pxa_spi_info lpss_info_config = {
	.setup = lpss_spi_setup,
};

static int ce4100_spi_setup(struct pci_dev *dev, struct pxa2xx_spi_controller *c)
{
	struct ssp_device *ssp = &c->ssp;

	ssp->type = PXA25x_SSP;
	ssp->port_id = dev->devfn;
	c->num_chipselect = dev->devfn;

	return pxa2xx_spi_pci_clk_register(dev, ssp, 3686400);
}

static const struct pxa_spi_info ce4100_info_config = {
	.setup = ce4100_spi_setup,
};

static int mrfld_spi_setup(struct pci_dev *dev, struct pxa2xx_spi_controller *c)
{
	struct ssp_device *ssp = &c->ssp;
	struct dw_dma_slave *tx, *rx;
	struct pci_dev *dma_dev;
	int ret;

	ssp->type = MRFLD_SSP;

	switch (PCI_FUNC(dev->devfn)) {
	case 0:
		ssp->port_id = 3;
		c->num_chipselect = 1;
		c->tx_param = &mrfld3_tx_param;
		c->rx_param = &mrfld3_rx_param;
		break;
	case 1:
		ssp->port_id = 5;
		c->num_chipselect = 4;
		c->tx_param = &mrfld5_tx_param;
		c->rx_param = &mrfld5_rx_param;
		break;
	case 2:
		ssp->port_id = 6;
		c->num_chipselect = 1;
		c->tx_param = &mrfld6_tx_param;
		c->rx_param = &mrfld6_rx_param;
		break;
	default:
		return -ENODEV;
	}

	ret = pxa2xx_spi_pci_clk_register(dev, ssp, 25000000);
	if (ret)
		return ret;

	dma_dev = pci_get_slot(dev->bus, PCI_DEVFN(21, 0));
	ret = devm_add_action_or_reset(&dev->dev, lpss_dma_put_device, dma_dev);
	if (ret)
		return ret;

	tx = c->tx_param;
	tx->dma_dev = &dma_dev->dev;

	rx = c->rx_param;
	rx->dma_dev = &dma_dev->dev;

	c->dma_filter = lpss_dma_filter;
	c->dma_burst_size = 8;
	c->enable_dma = 1;
	return 0;
}

static const struct pxa_spi_info mrfld_info_config = {
	.setup = mrfld_spi_setup,
};

static int qrk_spi_setup(struct pci_dev *dev, struct pxa2xx_spi_controller *c)
{
	struct ssp_device *ssp = &c->ssp;

	ssp->type = QUARK_X1000_SSP;
	ssp->port_id = dev->devfn;
	c->num_chipselect = 1;

	return pxa2xx_spi_pci_clk_register(dev, ssp, 50000000);
}

static const struct pxa_spi_info qrk_info_config = {
	.setup = qrk_spi_setup,
};

static int pxa2xx_spi_pci_probe(struct pci_dev *dev,
		const struct pci_device_id *ent)
{
	const struct pxa_spi_info *info;
	int ret;
	struct pxa2xx_spi_controller *pdata;
	struct ssp_device *ssp;

	ret = pcim_enable_device(dev);
	if (ret)
		return ret;

	pdata = devm_kzalloc(&dev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	ssp = &pdata->ssp;
	ssp->dev = &dev->dev;
	ssp->phys_base = pci_resource_start(dev, 0);
	ssp->mmio_base = pcim_iomap_region(dev, 0, "PXA2xx SPI");
	if (IS_ERR(ssp->mmio_base))
		return PTR_ERR(ssp->mmio_base);

	info = (struct pxa_spi_info *)ent->driver_data;
	ret = info->setup(dev, pdata);
	if (ret)
		return ret;

	ret = lpss_ctx_init(dev, ssp);
	if (ret)
		return ret;

	pci_set_master(dev);

	ret = pci_alloc_irq_vectors(dev, 1, 1, PCI_IRQ_ALL_TYPES);
	if (ret < 0)
		return ret;
	ssp->irq = pci_irq_vector(dev, 0);

	ret = pxa2xx_spi_probe(&dev->dev, ssp, pdata);
	if (ret)
		return ret;

	pm_runtime_set_autosuspend_delay(&dev->dev, 50);
	pm_runtime_use_autosuspend(&dev->dev);
	pm_runtime_put_autosuspend(&dev->dev);
	pm_runtime_allow(&dev->dev);

	return 0;
}

static void pxa2xx_spi_pci_remove(struct pci_dev *dev)
{
	pm_runtime_forbid(&dev->dev);
	pm_runtime_get_noresume(&dev->dev);

	pxa2xx_spi_remove(&dev->dev);
}

static const struct pci_device_id pxa2xx_spi_pci_devices[] = {
	{ PCI_DEVICE_DATA(INTEL, QUARK_X1000, &qrk_info_config) },
	{ PCI_DEVICE_DATA(INTEL, BYT, &lpss_info_config) },
	{ PCI_DEVICE_DATA(INTEL, MRFLD, &mrfld_info_config) },
	{ PCI_DEVICE_DATA(INTEL, BSW0, &lpss_info_config) },
	{ PCI_DEVICE_DATA(INTEL, BSW1, &lpss_info_config) },
	{ PCI_DEVICE_DATA(INTEL, BSW2, &lpss_info_config) },
	{ PCI_DEVICE_DATA(INTEL, CE4100, &ce4100_info_config) },
	{ PCI_DEVICE_DATA(INTEL, LPT0_0, &lpss_info_config) },
	{ PCI_DEVICE_DATA(INTEL, LPT0_1, &lpss_info_config) },
	{ PCI_DEVICE_DATA(INTEL, LPT1_0, &lpss_info_config) },
	{ PCI_DEVICE_DATA(INTEL, LPT1_1, &lpss_info_config) },
	{ }
};
MODULE_DEVICE_TABLE(pci, pxa2xx_spi_pci_devices);

static struct pci_driver pxa2xx_spi_pci_driver = {
	.name           = "pxa2xx_spi_pci",
	.id_table       = pxa2xx_spi_pci_devices,
	.driver = {
		.pm	= pm_ptr(&lpss_pci_pm_ops),
	},
	.probe          = pxa2xx_spi_pci_probe,
	.remove         = pxa2xx_spi_pci_remove,
};

module_pci_driver(pxa2xx_spi_pci_driver);

MODULE_DESCRIPTION("CE4100/LPSS PCI-SPI glue code for PXA's driver (MacBook8,1 LPSS S3 context fix)");
MODULE_VERSION("0.1-mb12");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS("SPI_PXA2xx");
MODULE_AUTHOR("Sebastian Andrzej Siewior <bigeasy@linutronix.de>");
