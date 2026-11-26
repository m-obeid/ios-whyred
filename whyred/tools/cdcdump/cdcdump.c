// SPDX-License-Identifier: GPL-2.0
/*
 * Read-only dump of the SDM660 internal audio codec as Linux runs it, for the iOS port: LPASS digital
 * codec (0x152c0000), LPI TLMM PDM pads (0x15070000 + 0x8000), PM660L analog codec (SPMI SID 3,
 * 0xf000-0xf1ff) through its regmap. Load while arecord runs (the digital codec needs MCLK). Never writes.
 */
#include <linux/module.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>

static int match_name(struct device *dev, void *data)
{
	return strcmp(dev_name(dev), data) == 0;
}

static int __init cdcdump_init(void)
{
	void __iomem *b;
	struct device *dev;
	struct regmap *map;
	unsigned int o, v;

	b = ioremap(0x152c0000, 0x1000);
	if (b) {
		for (o = 0; o < 0x400; o += 4)
			pr_info("CDCDUMP D %03x %02x\n", o, readl_relaxed(b + o));
		iounmap(b);
	}
	b = ioremap(0x15078000, 0x1000);
	if (b) {
		for (o = 0; o < 0x80; o += 4)
			pr_info("CDCDUMP L %04x %x\n", 0x8000 + o, readl_relaxed(b + o));
		iounmap(b);
	}
	dev = bus_find_device(&platform_bus_type, NULL, (void *)"800f000.qcom,spmi:qcom,pm660l@3:anlg-cdc@f000",
			      match_name);
	map = dev ? dev_get_regmap(dev, NULL) : NULL;
	if (dev && !map && dev->parent)
		map = dev_get_regmap(dev->parent, NULL);
	pr_info("CDCDUMP dev %p map %p\n", dev, map);
	if (map) {
		regcache_cache_bypass(map, true);
		for (o = 0xf000; o < 0xf200; o++) {
			if (regmap_read(map, o, &v) == 0)
				pr_info("CDCDUMP A %04x %02x\n", o, v);
		}
		regcache_cache_bypass(map, false);
	}
	if (dev)
		put_device(dev);
	return -EAGAIN;		/* nothing to keep loaded */
}

module_init(cdcdump_init);
MODULE_LICENSE("GPL");
