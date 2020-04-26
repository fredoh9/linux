// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//

#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/virtual_bus.h>
#include <sound/sof/header.h>
#include "sof-client.h"
#include "sof-priv.h"
#include "ops.h"
#include "sof-audio.h"

#define SOF_NOCODEC_CLIENT_SUSPEND_DELAY_MS 3000

static struct snd_soc_card sof_nocodec_card = {
	.name = "nocodec", /* the sof- prefix is added by the core */
};

static int sof_nocodec_bes_setup(struct virtbus_device *vdev,
				 const struct snd_sof_dsp_ops *ops,
				 struct snd_soc_dai_link *links,
				 int link_num, struct snd_soc_card *card)
{
	struct snd_soc_dai_link_component *dlc;
	int i;

	if (!ops || !links || !card)
		return -EINVAL;

	dev_dbg(&vdev->dev, "%s: BE dai links %d dev_name=%s\n", __func__, link_num, dev_name(&vdev->dev));

	/* set up BE dai_links */
	for (i = 0; i < link_num; i++) {
		dlc = devm_kzalloc(&vdev->dev, 3 * sizeof(*dlc), GFP_KERNEL);
		if (!dlc)
			return -ENOMEM;

		links[i].name = devm_kasprintf(&vdev->dev, GFP_KERNEL,
					       "NoCodec-%d", i);
		if (!links[i].name)
			return -ENOMEM;

		//HACK: set dai widget stream name with dai link name
		//links[i].stream_name = links[i].name;
		dev_dbg(&vdev->dev, "%s: setup BE %d, %s, cpu dai name=%s\n", __func__, i, links[i].name, ops->drv[i].name);
		links[i].cpus = &dlc[0];
		links[i].codecs = &dlc[1];
		links[i].platforms = &dlc[2];

		links[i].num_cpus = 1;
		links[i].num_codecs = 1;
		links[i].num_platforms = 1;

		links[i].id = i;
		links[i].no_pcm = 1;
		links[i].cpus->dai_name = ops->drv[i].name;
		links[i].platforms->name = dev_name(&vdev->dev);	// this is sof-nocodec-client.2
		links[i].codecs->dai_name = "snd-soc-dummy-dai";
		links[i].codecs->name = "snd-soc-dummy";
		links[i].dpcm_playback = 1;
		links[i].dpcm_capture = 1;
	}

	card->dai_link = links;
	card->num_links = link_num;
	dev_dbg(&vdev->dev, "%s: BE dai setup %d DONE!\n", __func__, link_num);

	return 0;
}

static int sof_nocodec_setup(struct virtbus_device *vdev,
		      const struct snd_sof_dsp_ops *ops)
{
	struct snd_soc_dai_link *links;
	//struct sof_client_dev *cdev = virtbus_dev_to_sof_client_dev(vdev);

	dev_dbg(&vdev->dev, "%s: (internal function call) allocate links\n", __func__);

	/* create dummy BE dai_links */
	links = devm_kzalloc(&vdev->dev, sizeof(struct snd_soc_dai_link) *
			     ops->num_drv, GFP_KERNEL);
	if (!links)
		return -ENOMEM;

	return sof_nocodec_bes_setup(vdev, ops, links, ops->num_drv,
				     &sof_nocodec_card);
}

// Fred: static const is required?
static struct snd_soc_component_driver sof_nocodec_component = {
	.name = "sof-nocodec-component",
};


static int sof_nocodec_client_probe(struct virtbus_device *vdev)
{
	struct sof_client_dev *cdev = virtbus_dev_to_sof_client_dev(vdev);
	const struct sof_dev_desc *desc;
	struct snd_soc_card *card = &sof_nocodec_card;
	//struct sof_nocodec_client_data *nocodec_client_data;
	struct snd_soc_component_driver *plat_drv;
	struct snd_soc_dai_driver *dai_drv;
	int num_dai_drv;
	int ret;

	dev_dbg(&vdev->dev, "%s: nocodec client start!\n", __func__);

	/*
	 * The virtbus device has a usage count of 0 even before runtime PM
	 * is enabled. So, increment the usage count to let the device
	 * suspend after probe is complete.
	 */
	pm_runtime_get_noresume(&vdev->dev);


	/* set up platform component driver for nocodec*/
	plat_drv = sof_client_get_platform_drv(cdev);
	dev_dbg(&vdev->dev, "%s: update ignore_machine before=[%s] after=[%s]\n", __func__, plat_drv->ignore_machine, dev_name(&vdev->dev));
	plat_drv->ignore_machine = dev_name(&vdev->dev);

	dai_drv = sof_client_get_dai_drv(cdev);
	num_dai_drv = sof_client_get_num_dai_drv(cdev);
	dev_dbg(&vdev->dev, "%s: now register audio DSP platform driver and dai\n", __func__);
	dev_dbg(&vdev->dev, "%s: plat_drv=%p drv=%p num_drv=%d\n", __func__, plat_drv,
						      dai_drv,
						      num_dai_drv);
	/* now register audio DSP platform driver and dai */
	ret = devm_snd_soc_register_component(&vdev->dev, plat_drv,
					      dai_drv,
					      num_dai_drv);
	if (ret < 0) {
		dev_err(&vdev->dev, "%s: error: failed to register SOF probes DAI driver %d\n",
			__func__, ret);
		return ret;
	}

	desc = sof_client_get_platfom_data_desc(cdev);	// this is only for ops
	dev_dbg(&vdev->dev, "%s: to setup DAI links\n", __func__);
	ret = sof_nocodec_setup(vdev, desc->ops);
	if (ret < 0) {
		dev_err(&vdev->dev, "%s: sof_nocodec_setup failed, %d\n", __func__, ret);
		return ret;
	}

	/* Register nocodec sound card */
	dev_dbg(&vdev->dev, "%s: to register_card\n", __func__);
	card->dev = &vdev->dev;
	dev_set_drvdata(&vdev->dev, cdev->sdev); // HACK!!!, card expect sdev

	ret = devm_snd_soc_register_card(&vdev->dev, card);
	if (ret < 0) {
		dev_err(&vdev->dev, "%s: nocodec card register failed, %d\n", __func__, ret);
		return ret;
	}

	/*
	 * Override the drvdata for the device set by the core to point to the
	 * client device pointer. platform drv is expecting sdev.
	 */
	//dev_set_drvdata(&vdev->dev, cdev->sdev);

	/* enable runtime PM */
	pm_runtime_set_autosuspend_delay(&vdev->dev,
					 SOF_NOCODEC_CLIENT_SUSPEND_DELAY_MS);

	pm_runtime_use_autosuspend(&vdev->dev);

	//Fred: set_active is required
	pm_runtime_set_active(&vdev->dev);

	pm_runtime_enable(&vdev->dev);
	pm_runtime_mark_last_busy(&vdev->dev);
	pm_runtime_put_autosuspend(&vdev->dev);

	/* complete client device registration */
	complete(&cdev->probe_complete);

	return 0;
}

static int sof_nocodec_client_cleanup(struct virtbus_device *vdev)
{
	pm_runtime_disable(&vdev->dev);

	return 0;
}

static int sof_nocodec_client_remove(struct virtbus_device *vdev)
{
	dev_dbg(&vdev->dev, "%s: call cleanup\n", __func__);

	return sof_nocodec_client_cleanup(vdev);
}

static void sof_nocodec_client_shutdown(struct virtbus_device *vdev)
{
	dev_dbg(&vdev->dev, "%s: call cleanup\n", __func__);

	sof_nocodec_client_cleanup(vdev);
}

static const struct virtbus_dev_id sof_nocodec_virtbus_id_table[] = {
	{"sof-nocodec-client"},
	{},
};

static struct sof_client_drv sof_nocodec_client_drv = {
	.name = "sof-nocodec-client-drv",
	.type = SOF_CLIENT_AUDIO,
	.virtbus_drv = {
		.driver = {
			.name = "sof-nocodec-virtbus-drv",
		},
		.id_table = sof_nocodec_virtbus_id_table,
		.probe = sof_nocodec_client_probe,
		.remove = sof_nocodec_client_remove,
		.shutdown = sof_nocodec_client_shutdown,
	},
};

module_sof_client_driver(sof_nocodec_client_drv);

MODULE_DESCRIPTION("SOF Nocodec Client Driver");
MODULE_AUTHOR("Liam Girdwood");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_IMPORT_NS(SND_SOC_SOF_CLIENT);
MODULE_ALIAS("virtbus:sof-nocodec-client");
