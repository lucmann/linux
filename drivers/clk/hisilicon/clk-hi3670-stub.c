// SPDX-License-Identifier: GPL-2.0
/*
 * Hi3670 (HiKey970) stub clock driver
 *
 * The GPU (and other DVFS clocks) of the Kirin 970 are controlled by the
 * on-SoC MCU firmware (LPMCU). The Linux kernel votes a target frequency by
 * writing to shared registers and reads back the current frequency from
 * another register bank. This is a straight port of the vendor 4.9 driver
 * clk-kirin970-stub.c (compatible "hisilicon,kirin970-stub-clk").
 *
 * Copyright (c) 2018 Hisilicon Limited.
 * Author: Kevin Wangtao <kevin.wangtao@hisilicon.com>
 */

#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <dt-bindings/clock/hi3670-clock.h>

#define MHZ	1000000

struct hi3670_stub_clk {
	unsigned int id;
	struct device *dev;
	struct clk_hw hw;
	const char *clk_name;
	unsigned long rate;
	unsigned int convert_ratio;
	unsigned int clk_get_offset;
	unsigned int clk_set_offset;
	unsigned int clk_get_mask;
	unsigned int clk_set_mask;
};

struct hi3670_stub_clk_data {
	struct hi3670_stub_clk *clk_table;
	unsigned int clk_num;
	void __iomem *freq_get_base;
	void __iomem *freq_set_base;
};

static struct hi3670_stub_clk hi3670_stub_clk[HI3670_CLK_STUB_NUM] = {
	[HI3670_CLK_STUB_CLUSTER0] = {
		.id = HI3670_CLK_STUB_CLUSTER0,
		.clk_name = "cpu-cluster.0",
		.convert_ratio = 16,
		.clk_get_offset = 0x70,
		.clk_set_offset = 0x280,
		.clk_get_mask = 0xFFFF,
		.clk_set_mask = 0xFF,
	},
	[HI3670_CLK_STUB_CLUSTER1] = {
		.id = HI3670_CLK_STUB_CLUSTER1,
		.clk_name = "cpu-cluster.1",
		.convert_ratio = 16,
		.clk_get_offset = 0x74,
		.clk_set_offset = 0x270,
		.clk_get_mask = 0xFFFF,
		.clk_set_mask = 0xFF,
	},
	[HI3670_CLK_STUB_GPU] = {
		.id = HI3670_CLK_STUB_GPU,
		.clk_name = "clk-g3d",
		.convert_ratio = 16,
		.clk_get_offset = 0x78,
		.clk_set_offset = 0x290,
		.clk_get_mask = 0xFFFF,
		.clk_set_mask = 0xFF,
	},
	[HI3670_CLK_STUB_DDR] = {
		.id = HI3670_CLK_STUB_DDR,
		.clk_name = "clk-ddrc",
		.clk_get_offset = 0x7C,
		.clk_get_mask = 0xFFFF,
	},
	[HI3670_CLK_STUB_DDR_VOTE] = {
		.id = HI3670_CLK_STUB_DDR_VOTE,
		.clk_name = "clk-ddrc-vote",
		.convert_ratio = 16,
		.clk_set_offset = 0x2B0,
		.clk_set_mask = 0xFF,
	},
	[HI3670_CLK_STUB_DDR_LIMIT] = {
		.id = HI3670_CLK_STUB_DDR_LIMIT,
		.clk_name = "clk-ddrc-limit",
		.convert_ratio = 16,
		.clk_set_offset = 0x2A0,
		.clk_set_mask = 0xFF,
	},
};

static struct hi3670_stub_clk_data hi3670_stub_clk_data = {
	.clk_table = hi3670_stub_clk,
	.clk_num = HI3670_CLK_STUB_NUM,
};

#define to_stub_clk(_hw) container_of(_hw, struct hi3670_stub_clk, hw)

static unsigned long hi3670_stub_clk_recalc_rate(struct clk_hw *hw,
						 unsigned long parent_rate)
{
	struct hi3670_stub_clk *stub_clk = to_stub_clk(hw);
	struct device *dev = stub_clk->dev;
	struct hi3670_stub_clk_data *plat_data = dev_get_drvdata(dev);
	unsigned long rate;
	int shift;

	if (stub_clk->clk_get_mask) {
		shift = ffs(stub_clk->clk_get_mask) - 1;
		rate = readl(plat_data->freq_get_base + stub_clk->clk_get_offset);
		rate &= stub_clk->clk_get_mask;
		stub_clk->rate = (rate >> shift) * MHZ;
	}

	pr_debug("get rate%d;%ld\n", stub_clk->id, stub_clk->rate);

	return stub_clk->rate;
}

static int hi3670_stub_clk_determine_rate(struct clk_hw *hw,
					  struct clk_rate_request *req)
{
	pr_debug("%s: enter %ld\n", __func__, req->rate);
	return 0;
}

static int hi3670_stub_clk_set_rate(struct clk_hw *hw, unsigned long rate,
				    unsigned long parent_rate)
{
	struct hi3670_stub_clk *stub_clk = to_stub_clk(hw);
	struct device *dev = stub_clk->dev;
	struct hi3670_stub_clk_data *plat_data = dev_get_drvdata(dev);
	unsigned int freq_cfg, mask;
	int shift;

	if (stub_clk->clk_set_mask) {
		freq_cfg = rate / MHZ;
		if (stub_clk->convert_ratio)
			freq_cfg /= stub_clk->convert_ratio;

		shift = ffs(stub_clk->clk_set_mask) - 1;
		mask = stub_clk->clk_set_mask >> shift;

		if (freq_cfg > mask)
			freq_cfg = mask;

		freq_cfg <<= shift;
		freq_cfg |= stub_clk->clk_set_mask << 16;
		writel(freq_cfg,
		       plat_data->freq_set_base + stub_clk->clk_set_offset);
	}

	pr_debug("set rate%d;%ld\n", stub_clk->id, rate);

	stub_clk->rate = rate;

	return 0;
}

static const struct clk_ops hi3670_stub_clk_ops = {
	.recalc_rate = hi3670_stub_clk_recalc_rate,
	.determine_rate = hi3670_stub_clk_determine_rate,
	.set_rate = hi3670_stub_clk_set_rate,
};

static struct clk_hw *hi3670_stub_clk_hw_get(struct of_phandle_args *clkspec,
					     void *data)
{
	struct hi3670_stub_clk *clk_table = data;
	unsigned int idx = clkspec->args[0];

	if (idx >= HI3670_CLK_STUB_NUM) {
		pr_err("%s: invalid index %u\n", __func__, idx);
		return ERR_PTR(-EINVAL);
	}

	return &clk_table[idx].hw;
}

static int hi3670_stub_clk_probe(struct platform_device *pdev)
{
	struct hi3670_stub_clk_data *plat_data;
	const struct of_device_id *match;
	struct device *dev = &pdev->dev;
	unsigned int idx;
	int ret;

	match = of_match_device(dev->driver->of_match_table, dev);
	if (!match)
		return -EINVAL;

	plat_data = (struct hi3670_stub_clk_data *)match->data;

	plat_data->freq_get_base =
		devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(plat_data->freq_get_base)) {
		dev_err(dev, "failed to map freq get registers\n");
		return PTR_ERR(plat_data->freq_get_base);
	}

	plat_data->freq_set_base =
		devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(plat_data->freq_set_base)) {
		dev_err(dev, "failed to map freq set registers\n");
		return PTR_ERR(plat_data->freq_set_base);
	}

	platform_set_drvdata(pdev, plat_data);

	for (idx = 0; idx < plat_data->clk_num; idx++) {
		struct hi3670_stub_clk *stub_clk = &plat_data->clk_table[idx];

		stub_clk->dev = dev;
		stub_clk->hw.init = CLK_HW_INIT_NO_PARENT(stub_clk->clk_name,
							  &hi3670_stub_clk_ops,
							  CLK_GET_RATE_NOCACHE);
		ret = devm_clk_hw_register(dev, &stub_clk->hw);
		if (ret)
			return ret;
	}

	return devm_of_clk_add_hw_provider(dev, hi3670_stub_clk_hw_get,
					   plat_data->clk_table);
}

static const struct of_device_id hi3670_stub_clk_of_match[] = {
	{ .compatible = "hisilicon,hi3670-stub-clk",
	  .data = &hi3670_stub_clk_data },
	{}
};
MODULE_DEVICE_TABLE(of, hi3670_stub_clk_of_match);

static struct platform_driver hi3670_stub_clk_driver = {
	.driver = {
		.name = "hi3670-stub-clk",
		.of_match_table = hi3670_stub_clk_of_match,
	},
	.probe = hi3670_stub_clk_probe,
};

static int __init hi3670_stub_clk_init(void)
{
	return platform_driver_register(&hi3670_stub_clk_driver);
}
subsys_initcall(hi3670_stub_clk_init);

MODULE_DESCRIPTION("Hi3670 stub clock driver");
MODULE_LICENSE("GPL v2");
