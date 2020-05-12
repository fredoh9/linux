// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
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

#define SOF_NOCODEC_CLIENT_SUSPEND_DELAY_MS 3000

struct sof_nocodec_client_data {
	struct snd_soc_component_driver sof_nocodec_component;
	char *component_drv_name;
};

static int sof_nocodec_bes_setup(struct virtbus_device *vdev,
				 struct snd_soc_dai_driver *dai_drv,
				 int num_drv, struct snd_soc_card *card)
{
	struct snd_soc_dai_link *links;
	struct snd_soc_dai_link_component *dlc;
	int i;

	if (!dai_drv || !card)
		return -EINVAL;

	/* create dummy BE dai_links */
	links = devm_kzalloc(&vdev->dev, sizeof(struct snd_soc_dai_link) *
			     num_drv, GFP_KERNEL);
	if (!links)
		return -ENOMEM;

	/* set sound card name */
	card->name = devm_kasprintf(&vdev->dev, GFP_KERNEL, "nocodec");

	/* set up BE dai_links */
	for (i = 0; i < num_drv; i++) {
		dlc = devm_kzalloc(&vdev->dev, 3 * sizeof(*dlc), GFP_KERNEL);
		if (!dlc)
			return -ENOMEM;

		links[i].name = devm_kasprintf(&vdev->dev, GFP_KERNEL,
					       "NoCodec-%d", i);
		if (!links[i].name)
			return -ENOMEM;

		links[i].cpus = &dlc[0];
		links[i].codecs = &dlc[1];
		links[i].platforms = &dlc[2];

		links[i].num_cpus = 1;
		links[i].num_codecs = 1;
		links[i].num_platforms = 1;

		links[i].id = i;
		links[i].no_pcm = 1;
		links[i].cpus->dai_name = dai_drv[i].name;
		links[i].platforms->name = dev_name(&vdev->dev);
		links[i].codecs->dai_name = "snd-soc-dummy-dai";
		links[i].codecs->name = "snd-soc-dummy";
		links[i].dpcm_playback = 1;
		links[i].dpcm_capture = 1;
	}

	card->dai_link = links;
	card->num_links = num_drv;

	return 0;
}

/* define client own platform driver name */
#define SOF_NOCODEC_PCM_DRV_NAME "sof-nocodec-component"

void snd_sof_nocodec_platform_drv(struct virtbus_device *vdev)
{
	struct sof_client_dev *cdev = virtbus_dev_to_sof_client_dev(vdev);
	struct sof_nocodec_client_data *nocodec_client_data = cdev->data;
	struct snd_soc_component_driver *pd =
				&nocodec_client_data->sof_nocodec_component;
	const char *drv_name = dev_name(&vdev->dev);

	/* platform driver name can be different per client */
	nocodec_client_data->component_drv_name =
		devm_kstrdup(&vdev->dev, SOF_NOCODEC_PCM_DRV_NAME, GFP_KERNEL);
	pd->name = nocodec_client_data->component_drv_name;
	pd->probe = sof_client_pcm_probe;
	pd->remove = sof_client_pcm_remove;
	pd->open = sof_client_pcm_open;
	pd->close = sof_client_pcm_close;
	pd->hw_params = sof_client_pcm_hw_params;
	pd->prepare = sof_client_pcm_prepare;
	pd->hw_free = sof_client_pcm_hw_free;
	pd->trigger = sof_client_pcm_trigger;
	pd->pointer = sof_client_pcm_pointer;

	pd->pcm_construct = sof_client_pcm_new;
	pd->ignore_machine = drv_name;
	pd->be_hw_params_fixup = sof_client_pcm_dai_link_fixup;
	pd->be_pcm_base = SOF_CLIENT_BE_PCM_BASE;
	pd->use_dai_pcm_id = true;
	pd->topology_name_prefix = "sof";

	 /* increment module refcount when a pcm is opened */
	pd->module_get_upon_open = 1;
}

static int sof_nocodec_client_probe(struct virtbus_device *vdev)
{
	struct sof_client_dev *cdev = virtbus_dev_to_sof_client_dev(vdev);
	struct snd_soc_card *card = &cdev->card;
	struct snd_soc_component_driver *plat_drv;
	struct snd_soc_dai_driver *dai_drv;
	struct sof_nocodec_client_data *nocodec_client_data;
	int num_dai_drv;
	int ret;

	/*
	 * The virtbus device has a usage count of 0 even before runtime PM
	 * is enabled. So, increment the usage count to let the device
	 * suspend after probe is complete.
	 */
	pm_runtime_get_noresume(&vdev->dev);

	/* allocate memory for client data */
	nocodec_client_data = devm_kzalloc(&vdev->dev,
					   sizeof(*nocodec_client_data),
					   GFP_KERNEL);
	if (!nocodec_client_data)
		return -ENOMEM;
	cdev->data = nocodec_client_data;

	/* set up platform component driver for nocodec */
	snd_sof_nocodec_platform_drv(vdev);
	plat_drv = &nocodec_client_data->sof_nocodec_component;

	/* get DAI drv using client API */
	dai_drv = sof_client_get_dai_drv(cdev);
	num_dai_drv = sof_client_get_num_dai_drv(cdev);

	/* now register audio DSP platform driver and dai */
	ret = devm_snd_soc_register_component(&vdev->dev, plat_drv, dai_drv,
					      num_dai_drv);
	if (ret < 0) {
		dev_err(&vdev->dev, "failed to register component, %d\n", ret);
		return ret;
	}

	ret = sof_nocodec_bes_setup(vdev, dai_drv, num_dai_drv, card);
	if (ret < 0) {
		dev_err(&vdev->dev, "Setup BE DAI links failed, %d\n", ret);
		return ret;
	}

	card->dev = &vdev->dev;

	/* Register nocodec sound card */
	ret = devm_snd_soc_register_card(&vdev->dev, card);
	if (ret < 0) {
		dev_err(&vdev->dev, "nocodec card register failed, %d\n", ret);
		return ret;
	}

	/* enable runtime PM */
	pm_runtime_set_autosuspend_delay(&vdev->dev,
					 SOF_NOCODEC_CLIENT_SUSPEND_DELAY_MS);
	pm_runtime_use_autosuspend(&vdev->dev);
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
	return sof_nocodec_client_cleanup(vdev);
}

static void sof_nocodec_client_shutdown(struct virtbus_device *vdev)
{
	sof_nocodec_client_cleanup(vdev);
}

static const struct virtbus_dev_id sof_nocodec_virtbus_id_table[] = {
	{"sof-nocodec-client"},
	{},
};

const char *nocodec_get_component_drv_name(struct sof_client_dev *cdev)
{
	struct sof_nocodec_client_data *data = cdev->data;

	return data->component_drv_name;
}

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
	.ops = {
		.get_component_drv_name = nocodec_get_component_drv_name,
	}
};

module_sof_client_driver(sof_nocodec_client_drv);

MODULE_DESCRIPTION("SOF Nocodec Client Driver");
MODULE_AUTHOR("Liam Girdwood");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_IMPORT_NS(SND_SOC_SOF_CLIENT);
MODULE_ALIAS("virtbus:sof-nocodec-client");
