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

#if 0
static struct snd_soc_component_driver *gcmpnt_drv;
static struct snd_soc_dai_driver *gdai_drv;
static int gnum_dai;

int sof_nocodec_save_component_setup(struct snd_soc_component_driver *cmpnt_drv,
			 struct snd_soc_dai_driver *dai_drv, int num_dai)
{
	printk(">>> Fred: save component/dia drivers %p, %p, %d", cmpnt_drv, dai_drv, num_dai);
	gcmpnt_drv = cmpnt_drv;
	gdai_drv = dai_drv;
	gnum_dai = num_dai;
	return 0;
}
EXPORT_SYMBOL(sof_nocodec_save_component_setup);
#endif
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
		dev_dbg(&vdev->dev, "%s: setup BE %d, %s\n", __func__, i, links[i].name);
		links[i].cpus = &dlc[0];
		links[i].codecs = &dlc[1];
		links[i].platforms = &dlc[2];

		links[i].num_cpus = 1;
		links[i].num_codecs = 1;
		links[i].num_platforms = 1;

		links[i].id = i;
		links[i].no_pcm = 1;
		links[i].cpus->dai_name = ops->drv[i].name;
		links[i].platforms->name = dev_name(&vdev->dev);
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

	dev_dbg(&vdev->dev, "%s: (internal function call) allocate links\n", __func__);

	/* create dummy BE dai_links */
	links = devm_kzalloc(&vdev->dev, sizeof(struct snd_soc_dai_link) *
			     ops->num_drv, GFP_KERNEL);
	if (!links)
		return -ENOMEM;

	return sof_nocodec_bes_setup(vdev, ops, links, ops->num_drv,
				     &sof_nocodec_card);
}

/*
static const struct snd_soc_component_driver sof_nocodec_component = {
	.name = "sof-nocodec-component",
	.module_get_upon_open = 1,
};
*/
/* Fred: TODO: export in somewhere */
int sof_pcm_hw_params(struct snd_soc_component *component,
			     struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params);
int sof_pcm_hw_free(struct snd_soc_component *component,
			   struct snd_pcm_substream *substream);
int sof_pcm_prepare(struct snd_soc_component *component,
			   struct snd_pcm_substream *substream);
int sof_pcm_trigger(struct snd_soc_component *component,
			   struct snd_pcm_substream *substream, int cmd);
snd_pcm_uframes_t sof_pcm_pointer(struct snd_soc_component *component,
					 struct snd_pcm_substream *substream);
int sof_pcm_open(struct snd_soc_component *component,
			struct snd_pcm_substream *substream);
int sof_pcm_close(struct snd_soc_component *component,
			 struct snd_pcm_substream *substream);
int sof_pcm_new(struct snd_soc_component *component,
		       struct snd_soc_pcm_runtime *rtd);
int sof_pcm_dai_link_fixup(struct snd_soc_pcm_runtime *rtd,
				  struct snd_pcm_hw_params *params);
int sof_pcm_probe(struct snd_soc_component *component);
void sof_pcm_remove(struct snd_soc_component *component);


void snd_sof_nocodec_platform_drv(struct virtbus_device *vdev)
{
	struct sof_client_dev *cdev = virtbus_dev_to_sof_client_dev(vdev);
	struct snd_sof_dev *sdev = cdev->sdev;
	struct snd_soc_component_driver *pd = &sdev->plat_drv;
	struct snd_sof_pdata *plat_data = sdev->pdata;
	const char *drv_name;

	//drv_name = plat_data->machine->drv_name;
	drv_name = dev_name(&vdev->dev);
	dev_dbg(sdev->dev, "%s: drv_name=%s \n", __func__, drv_name);

	pd->name = "sof-audio-component";
	pd->probe = sof_pcm_probe;
	pd->remove = sof_pcm_remove;
	pd->open = sof_pcm_open;
	pd->close = sof_pcm_close;
	pd->hw_params = sof_pcm_hw_params;
	pd->prepare = sof_pcm_prepare;
	pd->hw_free = sof_pcm_hw_free;
	pd->trigger = sof_pcm_trigger;
	pd->pointer = sof_pcm_pointer;

	pd->pcm_construct = sof_pcm_new;
	pd->ignore_machine = drv_name;
	pd->be_hw_params_fixup = sof_pcm_dai_link_fixup;
	pd->be_pcm_base = SOF_BE_PCM_BASE;
	pd->use_dai_pcm_id = true;
	pd->topology_name_prefix = "sof";

	 /* increment module refcount when a pcm is opened */
	pd->module_get_upon_open = 1;
}

static int sof_nocodec_client_probe(struct virtbus_device *vdev)
{
	struct sof_client_dev *cdev = virtbus_dev_to_sof_client_dev(vdev);
	struct snd_sof_dev *sdev = cdev->sdev;
	struct snd_sof_pdata *sof_pdata = sdev->pdata;
	const struct sof_dev_desc *desc = sof_pdata->desc;
	struct snd_soc_card *card = &sof_nocodec_card;
	//struct snd_soc_acpi_mach *mach;
	//struct sof_nocodec_client_data *nocodec_client_data;
	int ret;

	dev_dbg(&vdev->dev, "%s: nocodec client start!\n", __func__);

	/*
	 * The virtbus device has a usage count of 0 even before runtime PM
	 * is enabled. So, increment the usage count to let the device
	 * suspend after probe is complete.
	 */
	pm_runtime_get_noresume(&vdev->dev);

	/* set up platform component driver for nocodec*/
	dev_dbg(&vdev->dev, "%s: set up platform component driver for nocodec\n", __func__);
	snd_sof_nocodec_platform_drv(vdev);

	/* now register audio DSP platform driver and dai */
	dev_dbg(&vdev->dev, "%s: now register audio DSP platform driver and dai\n", __func__);
	dev_dbg(&vdev->dev, "%s: plat_drv=%p drv=%p num_drv=%d\n", __func__, &sdev->plat_drv,
					      sof_ops(sdev)->drv,
					      sof_ops(sdev)->num_drv);
	ret = devm_snd_soc_register_component(sdev->dev, &sdev->plat_drv,
					      sof_ops(sdev)->drv,
					      sof_ops(sdev)->num_drv);
	if (ret < 0) {
		dev_err(&vdev->dev, "%s: error: failed to register SOF probes DAI driver %d\n",
			__func__, ret);
	}	return ret;

//TODO:
// Current DEV name is sof-nocodec-client.2
// but component driver name(component->driver) is sof-nocodec
//----------------
	dev_dbg(&vdev->dev, "%s: to setup DAI links\n", __func__);
	ret = sof_nocodec_setup(vdev, desc->ops);
	if (ret < 0) {
		dev_err(&vdev->dev, "%s: sof_nocodec_setup failed, %d\n", __func__, ret);
		return ret;
	}
//----------------

	/* Register nocodec sound card */
	dev_dbg(&vdev->dev, "%s: to register_card\n", __func__);
	card->dev = &vdev->dev;

	ret = devm_snd_soc_register_card(&vdev->dev, card);
	if (ret < 0) {
		dev_err(&vdev->dev, "%s: nocodec card register failed, %d\n", __func__, ret);
		return ret;
	}

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
	//complete(&cdev->probe_complete);

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
/*
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
*/

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
//MODULE_ALIAS("platform:sof-nocodec");
