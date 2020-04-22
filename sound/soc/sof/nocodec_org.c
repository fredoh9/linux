// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//

#include <linux/module.h>
#include <sound/sof.h>
#include "sof-priv.h"

static struct snd_soc_card sof_nocodec_card = {
	.name = "nocodec", /* the sof- prefix is added by the core */
};

static int sof_nocodec_bes_setup(struct device *dev,
				 const struct snd_sof_dsp_ops *ops,
				 struct snd_soc_dai_link *links,
				 int link_num, struct snd_soc_card *card)
{
	struct snd_soc_dai_link_component *dlc;
	int i;

	if (!ops || !links || !card)
		return -EINVAL;

	dev_dbg(dev, "%s: BE dai links %d\n", __func__, link_num);

	/* set up BE dai_links */
	for (i = 0; i < link_num; i++) {
		dlc = devm_kzalloc(dev, 3 * sizeof(*dlc), GFP_KERNEL);
		if (!dlc)
			return -ENOMEM;

		links[i].name = devm_kasprintf(dev, GFP_KERNEL,
					       "NoCodec-%d", i);
		if (!links[i].name)
			return -ENOMEM;

		dev_dbg(dev, "%s:setup BE %d, %s\n", __func__, i, links[i].name);
		links[i].cpus = &dlc[0];
		links[i].codecs = &dlc[1];
		links[i].platforms = &dlc[2];

		links[i].num_cpus = 1;
		links[i].num_codecs = 1;
		links[i].num_platforms = 1;

		links[i].id = i;
		links[i].no_pcm = 1;
		links[i].cpus->dai_name = ops->drv[i].name;
		links[i].platforms->name = dev_name(dev);
		links[i].codecs->dai_name = "snd-soc-dummy-dai";
		links[i].codecs->name = "snd-soc-dummy";
		links[i].dpcm_playback = 1;
		links[i].dpcm_capture = 1;
	}

	card->dai_link = links;
	card->num_links = link_num;
	dev_dbg(dev, "%s: BE dai setup %d DONE!\n", __func__, link_num);

	return 0;
}

int sof_nocodec_setup(struct device *dev,
		      const struct snd_sof_dsp_ops *ops)
{
	struct snd_soc_dai_link *links;

	dev_dbg(dev, "%s: (call from sof-audio) allocate links\n", __func__);

	/* create dummy BE dai_links */
	links = devm_kzalloc(dev, sizeof(struct snd_soc_dai_link) *
			     ops->num_drv, GFP_KERNEL);
	if (!links)
		return -ENOMEM;

	return sof_nocodec_bes_setup(dev, ops, links, ops->num_drv,
				     &sof_nocodec_card);
}
EXPORT_SYMBOL(sof_nocodec_setup);

static int sof_nocodec_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &sof_nocodec_card;

	dev_dbg(&pdev->dev, "%s: nocodec client allocation\n", __func__);
	card->dev = &pdev->dev;

	return devm_snd_soc_register_card(&pdev->dev, card);
}

static int sof_nocodec_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver sof_nocodec_audio = {
	.probe = sof_nocodec_probe,
	.remove = sof_nocodec_remove,
	.driver = {
		.name = "sof-nocodec",
		.pm = &snd_soc_pm_ops,
	},
};
module_platform_driver(sof_nocodec_audio)

MODULE_DESCRIPTION("ASoC sof nocodec");
MODULE_AUTHOR("Liam Girdwood");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("platform:sof-nocodec");
