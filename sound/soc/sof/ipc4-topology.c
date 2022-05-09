// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2022 Intel Corporation. All rights reserved.
//
//
#include <uapi/sound/sof/tokens.h>
#include <sound/pcm_params.h>
#include <sound/sof/ext_manifest4.h>
#include <sound/intel-nhlt.h>
#include "sof-priv.h"
#include "sof-audio.h"
#include "ipc4-priv.h"
#include "ipc4-topology.h"
#include "ops.h"

#define SOF_IPC4_GAIN_PARAM_ID  0
#define SOF_IPC4_TPLG_ABI_SIZE 6

static DEFINE_IDA(alh_group_ida);

static const struct sof_topology_token ipc4_sched_tokens[] = {
	{SOF_TKN_SCHED_LP_MODE, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_pipeline, lp_mode)}
};

static const struct sof_topology_token pipeline_tokens[] = {
	{SOF_TKN_SCHED_DYNAMIC_PIPELINE, SND_SOC_TPLG_TUPLE_TYPE_BOOL, get_token_u16,
		offsetof(struct snd_sof_widget, dynamic_pipeline_widget)},
};

static const struct sof_topology_token ipc4_comp_tokens[] = {
	{SOF_TKN_COMP_CPC, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_base_module_cfg, cpc)},
	{SOF_TKN_COMP_IS_PAGES, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_base_module_cfg, is_pages)},
};

static const struct sof_topology_token ipc4_audio_format_buffer_size_tokens[] = {
	{SOF_TKN_CAVS_AUDIO_FORMAT_IBS, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_base_module_cfg, ibs)},
	{SOF_TKN_CAVS_AUDIO_FORMAT_OBS, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_base_module_cfg, obs)},
};

static const struct sof_topology_token ipc4_in_audio_format_tokens[] = {
	{SOF_TKN_CAVS_AUDIO_FORMAT_IN_RATE, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_audio_format, sampling_frequency)},
	{SOF_TKN_CAVS_AUDIO_FORMAT_IN_BIT_DEPTH, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_audio_format, bit_depth)},
	{SOF_TKN_CAVS_AUDIO_FORMAT_IN_CH_MAP, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_audio_format, ch_map)},
	{SOF_TKN_CAVS_AUDIO_FORMAT_IN_CH_CFG, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_audio_format, ch_cfg)},
	{SOF_TKN_CAVS_AUDIO_FORMAT_IN_INTERLEAVING_STYLE, SND_SOC_TPLG_TUPLE_TYPE_WORD,
		get_token_u32, offsetof(struct sof_ipc4_audio_format, interleaving_style)},
	{SOF_TKN_CAVS_AUDIO_FORMAT_IN_FMT_CFG, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_audio_format, fmt_cfg)},
};

static const struct sof_topology_token ipc4_out_audio_format_tokens[] = {
	{SOF_TKN_CAVS_AUDIO_FORMAT_OUT_RATE, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_audio_format, sampling_frequency)},
	{SOF_TKN_CAVS_AUDIO_FORMAT_OUT_BIT_DEPTH, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_audio_format, bit_depth)},
	{SOF_TKN_CAVS_AUDIO_FORMAT_OUT_CH_MAP, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_audio_format, ch_map)},
	{SOF_TKN_CAVS_AUDIO_FORMAT_OUT_CH_CFG, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_audio_format, ch_cfg)},
	{SOF_TKN_CAVS_AUDIO_FORMAT_OUT_INTERLEAVING_STYLE, SND_SOC_TPLG_TUPLE_TYPE_WORD,
		get_token_u32, offsetof(struct sof_ipc4_audio_format, interleaving_style)},
	{SOF_TKN_CAVS_AUDIO_FORMAT_OUT_FMT_CFG, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_audio_format, fmt_cfg)},
};

static const struct sof_topology_token ipc4_copier_gateway_cfg_tokens[] = {
	{SOF_TKN_CAVS_AUDIO_FORMAT_DMA_BUFFER_SIZE, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32, 0},
};

static const struct sof_topology_token ipc4_copier_tokens[] = {
	{SOF_TKN_INTEL_COPIER_NODE_TYPE, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32, 0},
};

static const struct sof_topology_token ipc4_audio_fmt_num_tokens[] = {
	{SOF_TKN_COMP_NUM_AUDIO_FORMATS, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		0},
};

static const struct sof_topology_token dai_tokens[] = {
	{SOF_TKN_DAI_TYPE, SND_SOC_TPLG_TUPLE_TYPE_STRING, get_token_dai_type,
		offsetof(struct sof_ipc4_copier, dai_type)},
	{SOF_TKN_DAI_INDEX, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_copier, dai_index)},
};

/* Component extended tokens */
static const struct sof_topology_token comp_ext_tokens[] = {
	{SOF_TKN_COMP_UUID, SND_SOC_TPLG_TUPLE_TYPE_UUID, get_token_uuid,
		offsetof(struct snd_sof_widget, uuid)},
};

static const struct sof_topology_token gain_tokens[] = {
	{SOF_TKN_GAIN_RAMP_TYPE, SND_SOC_TPLG_TUPLE_TYPE_WORD,
		get_token_u32, offsetof(struct sof_ipc4_gain_data, curve_type)},
	{SOF_TKN_GAIN_RAMP_DURATION,
		SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_gain_data, curve_duration)},
	{SOF_TKN_GAIN_VAL, SND_SOC_TPLG_TUPLE_TYPE_WORD,
		get_token_u32, offsetof(struct sof_ipc4_gain_data, init_val)},
};

static const struct sof_token_info ipc4_token_list[SOF_TOKEN_COUNT] = {
	[SOF_DAI_TOKENS] = {"DAI tokens", dai_tokens, ARRAY_SIZE(dai_tokens)},
	[SOF_PIPELINE_TOKENS] = {"Pipeline tokens", pipeline_tokens, ARRAY_SIZE(pipeline_tokens)},
	[SOF_SCHED_TOKENS] = {"Scheduler tokens", ipc4_sched_tokens,
		ARRAY_SIZE(ipc4_sched_tokens)},
	[SOF_COMP_EXT_TOKENS] = {"Comp extended tokens", comp_ext_tokens,
		ARRAY_SIZE(comp_ext_tokens)},
	[SOF_COMP_TOKENS] = {"IPC4 Component tokens",
		ipc4_comp_tokens, ARRAY_SIZE(ipc4_comp_tokens)},
	[SOF_IN_AUDIO_FORMAT_TOKENS] = {"IPC4 Input Audio format tokens",
		ipc4_in_audio_format_tokens, ARRAY_SIZE(ipc4_in_audio_format_tokens)},
	[SOF_OUT_AUDIO_FORMAT_TOKENS] = {"IPC4 Output Audio format tokens",
		ipc4_out_audio_format_tokens, ARRAY_SIZE(ipc4_out_audio_format_tokens)},
	[SOF_AUDIO_FORMAT_BUFFER_SIZE_TOKENS] = {"IPC4 Audio format buffer size tokens",
		ipc4_audio_format_buffer_size_tokens,
		ARRAY_SIZE(ipc4_audio_format_buffer_size_tokens)},
	[SOF_COPIER_GATEWAY_CFG_TOKENS] = {"IPC4 Copier gateway config tokens",
		ipc4_copier_gateway_cfg_tokens, ARRAY_SIZE(ipc4_copier_gateway_cfg_tokens)},
	[SOF_COPIER_TOKENS] = {"IPC4 Copier tokens", ipc4_copier_tokens,
		ARRAY_SIZE(ipc4_copier_tokens)},
	[SOF_AUDIO_FMT_NUM_TOKENS] = {"IPC4 Audio format number tokens",
		ipc4_audio_fmt_num_tokens, ARRAY_SIZE(ipc4_audio_fmt_num_tokens)},
	[SOF_GAIN_TOKENS] = {"Gain tokens", gain_tokens, ARRAY_SIZE(gain_tokens)},
};

static void sof_ipc4_dbg_audio_format(struct device *dev,
				      struct sof_ipc4_audio_format *format,
				      size_t object_size, int num_format)
{
	struct sof_ipc4_audio_format *fmt;
	void *ptr = format;
	int i;

	for (i = 0; i < num_format; i++, ptr = (u8 *)ptr + object_size) {
		fmt = ptr;
		dev_dbg(dev,
			" #%d: %uKHz, %ubit (ch_map %#x ch_cfg %u interleaving_style %u fmt_cfg %#x)\n",
			i, fmt->sampling_frequency, fmt->bit_depth, fmt->ch_map,
			fmt->ch_cfg, fmt->interleaving_style, fmt->fmt_cfg);
	}
}

/**
 * sof_ipc4_get_audio_fmt - get available audio formats from swidget->tuples
 * @scomp: pointer to pointer to SOC component
 * @swidget: pointer to struct snd_sof_widget containing tuples
 * @available_fmt: pointer to struct sof_ipc4_available_audio_format being filling in
 * @has_out_format: true if available_fmt contains output format
 *
 * Return: 0 if successful
 */
static int sof_ipc4_get_audio_fmt(struct snd_soc_component *scomp,
				  struct snd_sof_widget *swidget,
				  struct sof_ipc4_available_audio_format *available_fmt,
				  bool has_out_format)
{
	struct sof_ipc4_base_module_cfg *base_config;
	struct sof_ipc4_audio_format *out_format;
	int audio_fmt_num = 0;
	int ret, i;

	ret = sof_update_ipc_object(scomp, &audio_fmt_num,
				    SOF_AUDIO_FMT_NUM_TOKENS, swidget->tuples,
				    swidget->num_tuples, sizeof(audio_fmt_num), 1);
	if (ret || audio_fmt_num <= 0) {
		dev_err(scomp->dev, "Invalid number of audio formats: %d\n", audio_fmt_num);
		return -EINVAL;
	}
	available_fmt->audio_fmt_num = audio_fmt_num;

	dev_dbg(scomp->dev, "Number of audio formats: %d\n", available_fmt->audio_fmt_num);

	base_config = kcalloc(available_fmt->audio_fmt_num, sizeof(*base_config), GFP_KERNEL);
	if (!base_config)
		return -ENOMEM;

	/* set cpc and is_pages for all base_cfg */
	for (i = 0; i < available_fmt->audio_fmt_num; i++) {
		ret = sof_update_ipc_object(scomp, &base_config[i],
					    SOF_COMP_TOKENS, swidget->tuples,
					    swidget->num_tuples, sizeof(*base_config), 1);
		if (ret) {
			dev_err(scomp->dev, "parse comp tokens failed %d\n", ret);
			goto err_in;
		}
	}

	/* copy the ibs/obs for each base_cfg */
	ret = sof_update_ipc_object(scomp, base_config,
				    SOF_AUDIO_FORMAT_BUFFER_SIZE_TOKENS, swidget->tuples,
				    swidget->num_tuples, sizeof(*base_config),
				    available_fmt->audio_fmt_num);
	if (ret) {
		dev_err(scomp->dev, "parse buffer size tokens failed %d\n", ret);
		goto err_in;
	}

	for (i = 0; i < available_fmt->audio_fmt_num; i++)
		dev_dbg(scomp->dev, "%d: ibs: %d obs: %d cpc: %d is_pages: %d\n", i,
			base_config[i].ibs, base_config[i].obs,
			base_config[i].cpc, base_config[i].is_pages);

	ret = sof_update_ipc_object(scomp, &base_config->audio_fmt,
				    SOF_IN_AUDIO_FORMAT_TOKENS, swidget->tuples,
				    swidget->num_tuples, sizeof(*base_config),
				    available_fmt->audio_fmt_num);
	if (ret) {
		dev_err(scomp->dev, "parse base_config audio_fmt tokens failed %d\n", ret);
		goto err_in;
	}

	dev_dbg(scomp->dev, "Get input audio formats for %s\n", swidget->widget->name);
	sof_ipc4_dbg_audio_format(scomp->dev, &base_config->audio_fmt,
				  sizeof(*base_config),
				  available_fmt->audio_fmt_num);

	available_fmt->base_config = base_config;

	if (!has_out_format)
		return 0;

	out_format = kcalloc(available_fmt->audio_fmt_num, sizeof(*out_format), GFP_KERNEL);
	if (!out_format) {
		ret = -ENOMEM;
		goto err_in;
	}

	ret = sof_update_ipc_object(scomp, out_format,
				    SOF_OUT_AUDIO_FORMAT_TOKENS, swidget->tuples,
				    swidget->num_tuples, sizeof(*out_format),
				    available_fmt->audio_fmt_num);

	if (ret) {
		dev_err(scomp->dev, "parse output audio_fmt tokens failed\n");
		goto err_out;
	}

	available_fmt->out_audio_fmt = out_format;
	dev_dbg(scomp->dev, "Get output audio formats for %s\n", swidget->widget->name);
	sof_ipc4_dbg_audio_format(scomp->dev, out_format, sizeof(*out_format),
				  available_fmt->audio_fmt_num);

	return 0;

err_out:
	kfree(out_format);
err_in:
	kfree(base_config);

	return ret;
}

static void sof_ipc4_widget_free_comp(struct snd_sof_widget *swidget)
{
	kfree(swidget->private);
}

static int sof_ipc4_widget_set_module_info(struct snd_sof_widget *swidget)
{
	struct snd_soc_component *scomp = swidget->scomp;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct sof_ipc4_fw_data *ipc4_data = sdev->private;
	struct sof_ipc4_fw_module *fw_modules = ipc4_data->fw_modules;
	int i;

	if (!fw_modules) {
		dev_err(sdev->dev, "no fw_module information\n");
		return -EINVAL;
	}

	/* set module info */
	for (i = 0; i < ipc4_data->num_fw_modules; i++) {
		if (guid_equal(&swidget->uuid, &fw_modules[i].man4_module_entry.uuid)) {
			swidget->module_info = &fw_modules[i];
			return 0;
		}
	}

	dev_err(sdev->dev, "failed to find module info for widget %s with UUID %pUL\n",
		swidget->widget->name, &swidget->uuid);
	return -EINVAL;
}

static int sof_ipc4_widget_setup_msg(struct snd_sof_widget *swidget, struct sof_ipc4_msg *msg)
{
	struct sof_ipc4_fw_module *fw_module;
	int ret;

	ret = sof_ipc4_widget_set_module_info(swidget);
	if (ret)
		return ret;

	fw_module = swidget->module_info;

	msg->primary = fw_module->man4_module_entry.id;
	msg->primary |= SOF_IPC4_MSG_TYPE_SET(SOF_IPC4_MOD_INIT_INSTANCE);
	msg->primary |= SOF_IPC4_MSG_DIR(SOF_IPC4_MSG_REQUEST);
	msg->primary |= SOF_IPC4_MSG_TARGET(SOF_IPC4_MODULE_MSG);

	msg->extension = SOF_IPC4_MOD_EXT_PPL_ID(swidget->pipeline_id);
	msg->extension |= SOF_IPC4_MOD_EXT_CORE_ID(swidget->core);

	return 0;
}

static int sof_ipc4_widget_setup_pcm(struct snd_sof_widget *swidget)
{
	struct sof_ipc4_available_audio_format *available_fmt;
	struct snd_soc_component *scomp = swidget->scomp;
	struct sof_ipc4_copier *ipc4_copier;
	int node_type = 0;
	int ret, i;

	ipc4_copier = kzalloc(sizeof(*ipc4_copier), GFP_KERNEL);
	if (!ipc4_copier)
		return -ENOMEM;

	swidget->private = ipc4_copier;
	available_fmt = &ipc4_copier->available_fmt;

	dev_dbg(scomp->dev, "Updating IPC structure for %s\n", swidget->widget->name);

	ret = sof_ipc4_get_audio_fmt(scomp, swidget, available_fmt, true);
	if (ret)
		goto free_copier;

	available_fmt->dma_buffer_size = kcalloc(available_fmt->audio_fmt_num, sizeof(u32),
						 GFP_KERNEL);
	if (!available_fmt->dma_buffer_size) {
		ret = -ENOMEM;
		goto free_copier;
	}

	ret = sof_update_ipc_object(scomp, available_fmt->dma_buffer_size,
				    SOF_COPIER_GATEWAY_CFG_TOKENS, swidget->tuples,
				    swidget->num_tuples, sizeof(u32),
				    available_fmt->audio_fmt_num);
	if (ret) {
		dev_err(scomp->dev, "Failed to parse dma buffer size in audio format for %s\n",
			swidget->widget->name);
		goto err;
	}

	dev_dbg(scomp->dev, "dma buffer size:\n");
	for (i = 0; i < available_fmt->audio_fmt_num; i++)
		dev_dbg(scomp->dev, "%d: %u\n", i,
			available_fmt->dma_buffer_size[i]);

	ret = sof_update_ipc_object(scomp, &node_type,
				    SOF_COPIER_TOKENS, swidget->tuples,
				    swidget->num_tuples, sizeof(node_type), 1);

	if (ret) {
		dev_err(scomp->dev, "parse host copier node type token failed %d\n",
			ret);
		goto err;
	}
	dev_dbg(scomp->dev, "host copier '%s' node_type %u\n", swidget->widget->name, node_type);

	ipc4_copier->data.gtw_cfg.node_id = SOF_IPC4_NODE_TYPE(node_type);
	ipc4_copier->gtw_attr = kzalloc(sizeof(*ipc4_copier->gtw_attr), GFP_KERNEL);
	if (!ipc4_copier->gtw_attr) {
		ret = -ENOMEM;
		goto err;
	}

	ipc4_copier->copier_config = (uint32_t *)ipc4_copier->gtw_attr;
	ipc4_copier->data.gtw_cfg.config_length =
		sizeof(struct sof_ipc4_gtw_attributes) >> 2;

	/* set up module info and message header */
	ret = sof_ipc4_widget_setup_msg(swidget, &ipc4_copier->msg);
	if (ret)
		goto free_gtw_attr;

	return 0;

free_gtw_attr:
	kfree(ipc4_copier->gtw_attr);
err:
	kfree(available_fmt->dma_buffer_size);
free_copier:
	kfree(ipc4_copier);
	return ret;
}

static void sof_ipc4_widget_free_comp_pcm(struct snd_sof_widget *swidget)
{
	struct sof_ipc4_copier *ipc4_copier = swidget->private;
	struct sof_ipc4_available_audio_format *available_fmt;

	if (!ipc4_copier)
		return;

	available_fmt = &ipc4_copier->available_fmt;
	kfree(available_fmt->dma_buffer_size);
	kfree(available_fmt->base_config);
	kfree(available_fmt->out_audio_fmt);
	kfree(ipc4_copier->gtw_attr);
	kfree(ipc4_copier);
	swidget->private = NULL;
}

static int sof_ipc4_widget_setup_comp_dai(struct snd_sof_widget *swidget)
{
	struct sof_ipc4_available_audio_format *available_fmt;
	struct snd_soc_component *scomp = swidget->scomp;
	struct snd_sof_dai *dai = swidget->private;
	struct sof_ipc4_copier *ipc4_copier;
	int node_type = 0;
	int ret, i;

	ipc4_copier = kzalloc(sizeof(*ipc4_copier), GFP_KERNEL);
	if (!ipc4_copier)
		return -ENOMEM;

	available_fmt = &ipc4_copier->available_fmt;

	dev_dbg(scomp->dev, "Updating IPC structure for %s\n", swidget->widget->name);

	ret = sof_ipc4_get_audio_fmt(scomp, swidget, available_fmt, true);
	if (ret)
		goto free_copier;

	available_fmt->dma_buffer_size = kcalloc(available_fmt->audio_fmt_num, sizeof(u32),
						 GFP_KERNEL);
	if (!available_fmt->dma_buffer_size) {
		ret = -ENOMEM;
		goto free_copier;
	}

	ret = sof_update_ipc_object(scomp, available_fmt->dma_buffer_size,
				    SOF_COPIER_GATEWAY_CFG_TOKENS, swidget->tuples,
				    swidget->num_tuples, sizeof(u32),
				    available_fmt->audio_fmt_num);
	if (ret) {
		dev_err(scomp->dev, "Failed to parse dma buffer size in audio format for %s\n",
			swidget->widget->name);
		goto err;
	}

	for (i = 0; i < available_fmt->audio_fmt_num; i++)
		dev_dbg(scomp->dev, "%d: dma buffer size: %u\n", i,
			available_fmt->dma_buffer_size[i]);

	ret = sof_update_ipc_object(scomp, &node_type,
				    SOF_COPIER_TOKENS, swidget->tuples,
				    swidget->num_tuples, sizeof(node_type), 1);
	if (ret) {
		dev_err(scomp->dev, "parse dai node type failed %d\n", ret);
		goto err;
	}

	ret = sof_update_ipc_object(scomp, ipc4_copier,
				    SOF_DAI_TOKENS, swidget->tuples,
				    swidget->num_tuples, sizeof(u32), 1);
	if (ret) {
		dev_err(scomp->dev, "parse dai copier node token failed %d\n", ret);
		goto err;
	}

	dev_dbg(scomp->dev, "dai %s node_type %u dai_type %u dai_index %d\n", swidget->widget->name,
		node_type, ipc4_copier->dai_type, ipc4_copier->dai_index);

	ipc4_copier->data.gtw_cfg.node_id = SOF_IPC4_NODE_TYPE(node_type);

	switch (ipc4_copier->dai_type) {
	case SOF_DAI_INTEL_ALH:
	{
		struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
		struct sof_ipc4_alh_configuration_blob *blob;
		struct snd_sof_widget *w;

		blob = kzalloc(sizeof(*blob), GFP_KERNEL);
		if (!blob) {
			ret = -ENOMEM;
			goto err;
		}

		list_for_each_entry(w, &sdev->widget_list, list) {
			if (w->widget->sname &&
			    strcmp(w->widget->sname, swidget->widget->sname))
				continue;

			blob->alh_cfg.count++;
		}
		/* Set blob->alh_cfg.count = 0 if the widget is not aggregated */
		if (blob->alh_cfg.count == 1)
			blob->alh_cfg.count = 0;

		ipc4_copier->copier_config = (uint32_t *)blob;
		ipc4_copier->data.gtw_cfg.config_length = sizeof(*blob) >> 2;
		break;
	}
	case SOF_DAI_INTEL_SSP:
		/* set SSP DAI index as the node_id */
		ipc4_copier->data.gtw_cfg.node_id |=
			SOF_IPC4_NODE_INDEX_INTEL_SSP(ipc4_copier->dai_index);
		break;
	case SOF_DAI_INTEL_DMIC:
		/* set DMIC DAI index as the node_id */
		ipc4_copier->data.gtw_cfg.node_id |=
			SOF_IPC4_NODE_INDEX_INTEL_DMIC(ipc4_copier->dai_index);
		break;
	default:
		ipc4_copier->gtw_attr = kzalloc(sizeof(*ipc4_copier->gtw_attr), GFP_KERNEL);
		if (!ipc4_copier->gtw_attr) {
			ret = -ENOMEM;
			goto err;
		}

		ipc4_copier->copier_config = (uint32_t *)ipc4_copier->gtw_attr;
		ipc4_copier->data.gtw_cfg.config_length =
			sizeof(struct sof_ipc4_gtw_attributes) >> 2;
		break;
	}

	dai->scomp = scomp;
	dai->private = ipc4_copier;

	/* set up module info and message header */
	ret = sof_ipc4_widget_setup_msg(swidget, &ipc4_copier->msg);
	if (ret)
		goto free_copier_config;

	return 0;

free_copier_config:
	kfree(ipc4_copier->copier_config);
err:
	kfree(available_fmt->dma_buffer_size);
free_copier:
	kfree(ipc4_copier);
	return ret;
}

static void sof_ipc4_widget_free_comp_dai(struct snd_sof_widget *swidget)
{
	struct sof_ipc4_available_audio_format *available_fmt;
	struct snd_sof_dai *dai = swidget->private;
	struct sof_ipc4_copier *ipc4_copier;

	if (!dai)
		return;

	ipc4_copier = dai->private;
	available_fmt = &ipc4_copier->available_fmt;

	kfree(available_fmt->dma_buffer_size);
	kfree(available_fmt->base_config);
	kfree(available_fmt->out_audio_fmt);
	if (ipc4_copier->dai_type != SOF_DAI_INTEL_SSP &&
	    ipc4_copier->dai_type != SOF_DAI_INTEL_DMIC)
		kfree(ipc4_copier->copier_config);
	kfree(dai->private);
	kfree(dai);
	swidget->private = NULL;
}

static int sof_ipc4_widget_setup_comp_pipeline(struct snd_sof_widget *swidget)
{
	struct snd_soc_component *scomp = swidget->scomp;
	struct sof_ipc4_pipeline *pipeline;
	int ret;

	pipeline = kzalloc(sizeof(*pipeline), GFP_KERNEL);
	if (!pipeline)
		return -ENOMEM;

	ret = sof_update_ipc_object(scomp, pipeline, SOF_SCHED_TOKENS, swidget->tuples,
				    swidget->num_tuples, sizeof(*pipeline), 1);
	if (ret) {
		dev_err(scomp->dev, "parsing scheduler tokens failed\n");
		goto err;
	}

	/* parse one set of pipeline tokens */
	ret = sof_update_ipc_object(scomp, swidget, SOF_PIPELINE_TOKENS, swidget->tuples,
				    swidget->num_tuples, sizeof(*swidget), 1);
	if (ret) {
		dev_err(scomp->dev, "parsing pipeline tokens failed\n");
		goto err;
	}

	/* TODO: Get priority from topology */
	pipeline->priority = 0;

	dev_dbg(scomp->dev, "pipeline '%s': id %d pri %d lp mode %d\n",
		swidget->widget->name, swidget->pipeline_id,
		pipeline->priority, pipeline->lp_mode);

	swidget->private = pipeline;

	pipeline->msg.primary = SOF_IPC4_GLB_PIPE_PRIORITY(pipeline->priority);
	pipeline->msg.primary |= SOF_IPC4_GLB_PIPE_INSTANCE_ID(swidget->pipeline_id);
	pipeline->msg.primary |= SOF_IPC4_MSG_TYPE_SET(SOF_IPC4_GLB_CREATE_PIPELINE);
	pipeline->msg.primary |= SOF_IPC4_MSG_DIR(SOF_IPC4_MSG_REQUEST);
	pipeline->msg.primary |= SOF_IPC4_MSG_TARGET(SOF_IPC4_FW_GEN_MSG);

	pipeline->msg.extension = pipeline->lp_mode;
	pipeline->state = SOF_IPC4_PIPE_UNINITIALIZED;

	return 0;
err:
	kfree(pipeline);
	return ret;
}

static int sof_ipc4_widget_setup_comp_pga(struct snd_sof_widget *swidget)
{
	struct snd_soc_component *scomp = swidget->scomp;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct sof_ipc4_fw_module *fw_module;
	struct snd_sof_control *scontrol;
	struct sof_ipc4_gain *gain;
	int ret;

	gain = kzalloc(sizeof(*gain), GFP_KERNEL);
	if (!gain)
		return -ENOMEM;

	swidget->private = gain;

	gain->data.channels = SOF_IPC4_GAIN_ALL_CHANNELS_MASK;
	gain->data.init_val = SOF_IPC4_VOL_ZERO_DB;

	/* The out_audio_fmt in topology is ignored as it is not required to be sent to the FW */
	ret = sof_ipc4_get_audio_fmt(scomp, swidget, &gain->available_fmt, false);
	if (ret)
		goto err;

	ret = sof_update_ipc_object(scomp, &gain->data, SOF_GAIN_TOKENS, swidget->tuples,
				    swidget->num_tuples, sizeof(gain->data), 1);
	if (ret) {
		dev_err(scomp->dev, "Parsing gain tokens failed\n");
		goto err;
	}

	dev_dbg(scomp->dev,
		"pga widget %s: ramp type: %d, ramp duration %d, initial gain value: %#x, cpc %d\n",
		swidget->widget->name, gain->data.curve_type, gain->data.curve_duration,
		gain->data.init_val, gain->base_config.cpc);

	ret = sof_ipc4_widget_setup_msg(swidget, &gain->msg);
	if (ret)
		goto err;

	fw_module = swidget->module_info;

	/* update module ID for all kcontrols for this widget */
	list_for_each_entry(scontrol, &sdev->kcontrol_list, list)
		if (scontrol->comp_id == swidget->comp_id) {
			struct sof_ipc4_control_data *cdata = scontrol->ipc_control_data;
			struct sof_ipc4_msg *msg = &cdata->msg;

			msg->primary |= fw_module->man4_module_entry.id;
		}

	return 0;
err:
	kfree(gain);
	return ret;
}

static int sof_ipc4_widget_setup_comp_mixer(struct snd_sof_widget *swidget)
{
	struct snd_soc_component *scomp = swidget->scomp;
	struct sof_ipc4_mixer *mixer;
	int ret;

	dev_dbg(scomp->dev, "Updating IPC structure for %s\n", swidget->widget->name);

	mixer = kzalloc(sizeof(*mixer), GFP_KERNEL);
	if (!mixer)
		return -ENOMEM;

	swidget->private = mixer;

	/* The out_audio_fmt in topology is ignored as it is not required to be sent to the FW */
	ret = sof_ipc4_get_audio_fmt(scomp, swidget, &mixer->available_fmt, false);
	if (ret)
		goto err;

	ret = sof_ipc4_widget_setup_msg(swidget, &mixer->msg);
	if (ret)
		goto err;

	return 0;
err:
	kfree(mixer);
	return ret;
}

static void
sof_ipc4_update_pipeline_mem_usage(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget,
				   struct sof_ipc4_base_module_cfg *base_config)
{
	struct sof_ipc4_fw_module *fw_module = swidget->module_info;
	struct snd_sof_widget *pipe_widget;
	struct sof_ipc4_pipeline *pipeline;
	int task_mem, queue_mem;
	int ibs, bss, total;

	ibs = base_config->ibs;
	bss = base_config->is_pages;

	task_mem = SOF_IPC4_PIPELINE_OBJECT_SIZE;
	task_mem += SOF_IPC4_MODULE_INSTANCE_LIST_ITEM_SIZE + bss;

	if (fw_module->man4_module_entry.type & SOF_IPC4_MODULE_LL) {
		task_mem += SOF_IPC4_FW_ROUNDUP(SOF_IPC4_LL_TASK_OBJECT_SIZE);
		task_mem += SOF_IPC4_FW_MAX_QUEUE_COUNT * SOF_IPC4_MODULE_INSTANCE_LIST_ITEM_SIZE;
		task_mem += SOF_IPC4_LL_TASK_LIST_ITEM_SIZE;
	} else {
		task_mem += SOF_IPC4_FW_ROUNDUP(SOF_IPC4_DP_TASK_OBJECT_SIZE);
		task_mem += SOF_IPC4_DP_TASK_LIST_SIZE;
	}

	ibs = SOF_IPC4_FW_ROUNDUP(ibs);
	queue_mem = SOF_IPC4_FW_MAX_QUEUE_COUNT * (SOF_IPC4_DATA_QUEUE_OBJECT_SIZE +  ibs);

	total = SOF_IPC4_FW_PAGE(task_mem + queue_mem);

	pipe_widget = swidget->pipe_widget;
	pipeline = pipe_widget->private;
	pipeline->mem_usage += total;
}

static int sof_ipc4_widget_assign_instance_id(struct snd_sof_dev *sdev,
					      struct snd_sof_widget *swidget)
{
	struct sof_ipc4_fw_module *fw_module = swidget->module_info;
	int max_instances = fw_module->man4_module_entry.instance_max_count;

	swidget->instance_id = ida_alloc_max(&fw_module->m_ida, max_instances, GFP_KERNEL);
	if (swidget->instance_id < 0) {
		dev_err(sdev->dev, "failed to assign instance id for widget %s",
			swidget->widget->name);
		return swidget->instance_id;
	}

	return 0;
}

static int sof_ipc4_init_audio_fmt(struct snd_sof_dev *sdev,
				   struct snd_sof_widget *swidget,
				   struct sof_ipc4_base_module_cfg *base_config,
				   struct sof_ipc4_audio_format *out_format,
				   struct snd_pcm_hw_params *params,
				   struct sof_ipc4_available_audio_format *available_fmt,
				   size_t object_offset)
{
	void *ptr = available_fmt->ref_audio_fmt;
	u32 valid_bits;
	u32 channels;
	u32 rate;
	int sample_valid_bits;
	int i;

	if (!ptr) {
		dev_err(sdev->dev, "no reference formats for %s\n", swidget->widget->name);
		return -EINVAL;
	}

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		sample_valid_bits = 16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		sample_valid_bits = 24;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		sample_valid_bits = 32;
		break;
	default:
		dev_err(sdev->dev, "invalid pcm frame format %d\n", params_format(params));
		return -EINVAL;
	}

	if (!available_fmt->audio_fmt_num) {
		dev_err(sdev->dev, "no formats available for %s\n", swidget->widget->name);
		return -EINVAL;
	}

	/*
	 * Search supported audio formats to match rate, channels ,and
	 * sample_valid_bytes from runtime params
	 */
	for (i = 0; i < available_fmt->audio_fmt_num; i++, ptr = (u8 *)ptr + object_offset) {
		struct sof_ipc4_audio_format *fmt = ptr;

		rate = fmt->sampling_frequency;
		channels = SOF_IPC4_AUDIO_FORMAT_CFG_CHANNELS_COUNT(fmt->fmt_cfg);
		valid_bits = SOF_IPC4_AUDIO_FORMAT_CFG_V_BIT_DEPTH(fmt->fmt_cfg);
		if (params_rate(params) == rate && params_channels(params) == channels &&
		    sample_valid_bits == valid_bits) {
			dev_dbg(sdev->dev, "%s: matching audio format index for %uHz, %ubit, %u channels: %d\n",
				__func__, rate, valid_bits, channels, i);

			/* copy ibs/obs and input format */
			memcpy(base_config, &available_fmt->base_config[i],
			       sizeof(struct sof_ipc4_base_module_cfg));

			/* copy output format */
			if (out_format)
				memcpy(out_format, &available_fmt->out_audio_fmt[i],
				       sizeof(struct sof_ipc4_audio_format));
			break;
		}
	}

	if (i == available_fmt->audio_fmt_num) {
		dev_err(sdev->dev, "%s: Unsupported audio format: %uHz, %ubit, %u channels\n",
			__func__, params_rate(params), sample_valid_bits, params_channels(params));
		return -EINVAL;
	}

	dev_dbg(sdev->dev, "Init input audio formats for %s\n", swidget->widget->name);
	sof_ipc4_dbg_audio_format(sdev->dev, &base_config->audio_fmt,
				  sizeof(*base_config), 1);
	if (out_format) {
		dev_dbg(sdev->dev, "Init output audio formats for %s\n", swidget->widget->name);
		sof_ipc4_dbg_audio_format(sdev->dev, out_format,
					  sizeof(*out_format), 1);
	}

	/* Return the index of the matched format */
	return i;
}

static void sof_ipc4_unprepare_copier_module(struct snd_sof_widget *swidget)
{
	struct sof_ipc4_fw_module *fw_module = swidget->module_info;
	struct sof_ipc4_copier *ipc4_copier = NULL;
	struct snd_sof_widget *pipe_widget;
	struct sof_ipc4_pipeline *pipeline;

	/* reset pipeline memory usage */
	pipe_widget = swidget->pipe_widget;
	pipeline = pipe_widget->private;
	pipeline->mem_usage = 0;

	if (WIDGET_IS_AIF(swidget->id)) {
		ipc4_copier = swidget->private;
	} else if (WIDGET_IS_DAI(swidget->id)) {
		struct snd_sof_dai *dai = swidget->private;

		ipc4_copier = dai->private;
		if (ipc4_copier->dai_type == SOF_DAI_INTEL_ALH) {
			struct sof_ipc4_alh_configuration_blob *blob;
			unsigned int group_id;

			blob = (struct sof_ipc4_alh_configuration_blob *)ipc4_copier->copier_config;
			if (blob->alh_cfg.count > 1) {
				group_id = SOF_IPC4_NODE_INDEX(ipc4_copier->data.gtw_cfg.node_id) -
					   ALH_MULTI_GTW_BASE;
				ida_free(&alh_group_ida, group_id);
			}
		}
	}

	if (ipc4_copier) {
		kfree(ipc4_copier->ipc_config_data);
		ipc4_copier->ipc_config_data = NULL;
		ipc4_copier->ipc_config_size = 0;
	}

	ida_free(&fw_module->m_ida, swidget->instance_id);
}

#if IS_ENABLED(CONFIG_ACPI) && IS_ENABLED(CONFIG_SND_INTEL_NHLT)
static int snd_sof_get_hw_config_params(struct snd_sof_dev *sdev, struct snd_sof_dai *dai,
					int *sample_rate, int *channel_count, int *bit_depth)
{
	struct snd_soc_tplg_hw_config *hw_config;
	struct snd_sof_dai_link *slink;
	bool dai_link_found = false;
	bool hw_cfg_found = false;
	int i;

	/* get current hw_config from link */
	list_for_each_entry(slink, &sdev->dai_link_list, list) {
		if (!strcmp(slink->link->name, dai->name)) {
			dai_link_found = true;
			break;
		}
	}

	if (!dai_link_found) {
		dev_err(sdev->dev, "%s: no DAI link found for DAI %s\n", __func__, dai->name);
		return -EINVAL;
	}

	for (i = 0; i < slink->num_hw_configs; i++) {
		hw_config = &slink->hw_configs[i];
		if (dai->current_config == le32_to_cpu(hw_config->id)) {
			hw_cfg_found = true;
			break;
		}
	}

	if (!hw_cfg_found) {
		dev_err(sdev->dev, "%s: no matching hw_config found for DAI %s\n", __func__,
			dai->name);
		return -EINVAL;
	}

	*bit_depth = le32_to_cpu(hw_config->tdm_slot_width);
	*channel_count = le32_to_cpu(hw_config->tdm_slots);
	*sample_rate = le32_to_cpu(hw_config->fsync_rate);

	dev_dbg(sdev->dev, "%s: sample rate: %d sample width: %d channels: %d\n",
		__func__, *sample_rate, *bit_depth, *channel_count);

	return 0;
}

static uint32_t __maybe_unused mtl_windows_dmic_16[] = {
0x00000001,0xffff3210,0xffff3210,0xffffffff,0xffffffff,0x00000003,0x00000003,0x00110844,
0x00110844,0x00000003,0x0000c001,0x0b001800,0x00000000,0x00000e03,0x00000000,0x00000000,
0x00000000,0x00000000,0x00000031,0x00010076,0x00000000,0x00000000,0x00000000,0x00000000,
0x00000000,0x00000000,0x00000031,0x000501e8,0x00000000,0x00000000,0x00000000,0x00000000,
0x00000000,0x00000000,0x40000049,0x40400181,0x4080036d,0x40c00453,0x41000278,0x414fff19,
0x418ffded,0x41c00037,0x42000245,0x4240008c,0x428ffd9b,0x42cffe9d,0x4300026c,0x43400267,
0x438ffdb1,0x43cffc25,0x4400016f,0x44400521,0x448ffff3,0x44cff9c4,0x450ffdf9,0x454006bf,
0x4580048e,0x45cff973,0x460ff896,0x46400572,0x46800a69,0x46cffccf,0x470ff2d1,0x474fffa4,
0x47800f25,0x47c004fc,0x480ff013,0x484ff593,0x48800f33,0x48c01068,0x490ff375,0x494fe999,
0x498007a2,0x49c01b90,0x4a0fff65,0x4a4fe0c2,0x4a8ff799,0x4ac02092,0x4b0012c4,0x4b4fe103,
0x4b8fe22c,0x4bc019f2,0x4c0028a3,0x4c4feef5,0x4c8fce29,0x4cc00484,0x4d003840,0x4d400b2c,
0x4d8fc55e,0x4dcfe2e9,0x4e0037f5,0x4e402ff8,0x4e8fd092,0x4ecfbde1,0x4f0020dd,0x4f405218,
0x4f8ff44f,0x4fcfa2b2,0x500ff04d,0x50406008,0x50802d78,0x50cfa580,0x510fb354,0x51404a82,
0x518069a2,0x51cfd000,0x520f7ef5,0x52400b5c,0x52808ecf,0x52c02152,0x530f70dc,0x534fad96,
0x53807f06,0x53c082c7,0x540fa30a,0x544f538d,0x548028c1,0x54c0c814,0x55001b21,0x554f3223,
0x558f9741,0x55c0b6f2,0x5600b687,0x564f8102,0x568f0843,0x56c02534,0x57011c19,0x5740507e,
0x578eee24,0x57cf2cfd,0x5800c91c,0x58414771,0x588fc601,0x58ce79ba,0x590f6e6b,0x59415ebe,
0x59817000,0x59cf5b76,0x5a0df777,0x5a4f51ac,0x5a81cf09,0x5ac23653,0x5b0fce37,0x5b4d3da8,
0x5b8d65b8,0x5bc050ef,0x5c038e37,0x5c44e2b2,0x5c8411a5,0x5cc259a3,0x5d00f248,0x5d403f0e,
0x5d800839,0x8000001b,0x80400018,0x8080000a,0x80cfffd7,0x810fff67,0x814ffea2,0x818ffd76,
0x81cffbdd,0x820ff9e2,0x824ff7a8,0x828ff567,0x82cff36c,0x830ff20b,0x834ff197,0x838ff24a,
0x83cff43b,0x840ff751,0x844ffb3f,0x848fff88,0x84c00392,0x850006c0,0x8540088b,0x858008a2,
0x85c006fd,0x860003e9,0x864ffffc,0x868ffbff,0x86cff8c8,0x870ff70f,0x874ff745,0x878ff975,
0x87cffd3d,0x880001da,0x8840064c,0x8880098f,0x88c00ad0,0x890009a4,0x89400627,0x898000ff,
0x89cffb43,0x8a0ff63c,0x8a4ff326,0x8a8ff2dd,0x8acff5a5,0x8b0ffb11,0x8b40020c,0x8b80090e,
0x8bc00e72,0x8c0010d3,0x8c400f69,0x8c800a42,0x8cc0024f,0x8d0ff93e,0x8d4ff120,0x8d8febf1,
0x8dcfeb25,0x8e0fef3d,0x8e4ff79d,0x8e800296,0x8ec00dbd,0x8f00166d,0x8f401a67,0x8f80185f,
0x8fc01061,0x900003e4,0x904ff58f,0x908fe8ab,0x90cfe066,0x910fdf12,0x914fe583,0x918ff2c3,
0x91c00427,0x920015d6,0x92402399,0x928029d9,0x92c02684,0x930019aa,0x934005a6,0x938feeb6,
0x93cfda21,0x940fcd09,0x944fcb2c,0x948fd5e8,0x94cfebb1,0x95000838,0x95402531,0x95803baf,
0x95c045b6,0x96003fc0,0x964029c6,0x96800799,0x96cfe04c,0x970fbcdb,0x974fa63a,0x978fa339,
0x97cfb6af,0x980fde6d,0x98401333,0x988049d5,0x98c0755a,0x990089c4,0x99407eed,0x998052d5,
0x99c00ae7,0x9a0fb3cc,0x9a4f5fb8,0x9a8f2367,0x9acf1259,0x9b0f3aea,0x9b4fa313,0x9b804687,
0x9bc1168b,0x9c01fbbc,0x9c42d96f,0x9c839233,0x9cc40c98,0x9d04376b,0x9d440c98,0x9d839233,
0x9dc2d96f,0x9e01fbbc,0x9e41168b,0x9e804687,0x9ecfa313,0x9f0f3aea,0x9f4f1259,0x9f8f2367,
0x9fcf5fb8,0xa00fb3cc,0xa0400ae7,0xa08052d5,0xa0c07eed,0xa10089c4,0xa140755a,0xa18049d5,
0xa1c01333,0xa20fde6d,0xa24fb6af,0xa28fa339,0xa2cfa63a,0xa30fbcdb,0xa34fe04c,0xa3800799,
0xa3c029c6,0xa4003fc0,0xa44045b6,0xa4803baf,0xa4c02531,0xa5000838,0xa54febb1,0xa58fd5e8,
0xa5cfcb2c,0xa60fcd09,0xa64fda21,0xa68feeb6,0xa6c005a6,0xa70019aa,0xa7402684,0xa78029d9,
0xa7c02399,0xa80015d6,0xa8400427,0xa88ff2c3,0xa8cfe583,0xa90fdf12,0xa94fe066,0xa98fe8ab,
0xa9cff58f,0xaa0003e4,0xaa401061,0xaa80185f,0xaac01a67,0xab00166d,0xab400dbd,0xab800296,
0xabcff79d,0xac0fef3d,0xac4feb25,0xac8febf1,0xaccff120,0xad0ff93e,0xad40024f,0xad800a42,
0xadc00f69,0xae0010d3,0xae400e72,0xae80090e,0xaec0020c,0xaf0ffb11,0xaf4ff5a5,0xaf8ff2dd,
0xafcff326,0xb00ff63c,0xb04ffb43,0xb08000ff,0xb0c00627,0xb10009a4,0xb1400ad0,0xb180098f,
0xb1c0064c,0xb20001da,0xb24ffd3d,0xb28ff975,0xb2cff745,0xb30ff70f,0xb34ff8c8,0xb38ffbff,
0xb3cffffc,0xb40003e9,0xb44006fd,0xb48008a2,0xb4c0088b,0xb50006c0,0xb5400392,0xb58fff88,
0xb5cffb3f,0xb60ff751,0xb64ff43b,0xb68ff24a,0xb6cff197,0xb70ff20b,0xb74ff36c,0xb78ff567,
0xb7cff7a8,0xb80ff9e2,0xb84ffbdd,0xb88ffd76,0xb8cffea2,0xb90fff67,0xb94fffd7,0xb980000a,
0xb9c00018,0xba00001b,0x0000c001,0x0b001800,0x00000000,0x00000e03,0x00000000,0x00000000,
0x00000000,0x00000000,0x00000031,0x00010076,0x00000000,0x00000000,0x00000000,0x00000000,
0x00000000,0x00000000,0x00000031,0x000501e8,0x00000000,0x00000000,0x00000000,0x00000000,
0x00000000,0x00000000,0x40000049,0x40400181,0x4080036d,0x40c00453,0x41000278,0x414fff19,
0x418ffded,0x41c00037,0x42000245,0x4240008c,0x428ffd9b,0x42cffe9d,0x4300026c,0x43400267,
0x438ffdb1,0x43cffc25,0x4400016f,0x44400521,0x448ffff3,0x44cff9c4,0x450ffdf9,0x454006bf,
0x4580048e,0x45cff973,0x460ff896,0x46400572,0x46800a69,0x46cffccf,0x470ff2d1,0x474fffa4,
0x47800f25,0x47c004fc,0x480ff013,0x484ff593,0x48800f33,0x48c01068,0x490ff375,0x494fe999,
0x498007a2,0x49c01b90,0x4a0fff65,0x4a4fe0c2,0x4a8ff799,0x4ac02092,0x4b0012c4,0x4b4fe103,
0x4b8fe22c,0x4bc019f2,0x4c0028a3,0x4c4feef5,0x4c8fce29,0x4cc00484,0x4d003840,0x4d400b2c,
0x4d8fc55e,0x4dcfe2e9,0x4e0037f5,0x4e402ff8,0x4e8fd092,0x4ecfbde1,0x4f0020dd,0x4f405218,
0x4f8ff44f,0x4fcfa2b2,0x500ff04d,0x50406008,0x50802d78,0x50cfa580,0x510fb354,0x51404a82,
0x518069a2,0x51cfd000,0x520f7ef5,0x52400b5c,0x52808ecf,0x52c02152,0x530f70dc,0x534fad96,
0x53807f06,0x53c082c7,0x540fa30a,0x544f538d,0x548028c1,0x54c0c814,0x55001b21,0x554f3223,
0x558f9741,0x55c0b6f2,0x5600b687,0x564f8102,0x568f0843,0x56c02534,0x57011c19,0x5740507e,
0x578eee24,0x57cf2cfd,0x5800c91c,0x58414771,0x588fc601,0x58ce79ba,0x590f6e6b,0x59415ebe,
0x59817000,0x59cf5b76,0x5a0df777,0x5a4f51ac,0x5a81cf09,0x5ac23653,0x5b0fce37,0x5b4d3da8,
0x5b8d65b8,0x5bc050ef,0x5c038e37,0x5c44e2b2,0x5c8411a5,0x5cc259a3,0x5d00f248,0x5d403f0e,
0x5d800839,0x8000001b,0x80400018,0x8080000a,0x80cfffd7,0x810fff67,0x814ffea2,0x818ffd76,
0x81cffbdd,0x820ff9e2,0x824ff7a8,0x828ff567,0x82cff36c,0x830ff20b,0x834ff197,0x838ff24a,
0x83cff43b,0x840ff751,0x844ffb3f,0x848fff88,0x84c00392,0x850006c0,0x8540088b,0x858008a2,
0x85c006fd,0x860003e9,0x864ffffc,0x868ffbff,0x86cff8c8,0x870ff70f,0x874ff745,0x878ff975,
0x87cffd3d,0x880001da,0x8840064c,0x8880098f,0x88c00ad0,0x890009a4,0x89400627,0x898000ff,
0x89cffb43,0x8a0ff63c,0x8a4ff326,0x8a8ff2dd,0x8acff5a5,0x8b0ffb11,0x8b40020c,0x8b80090e,
0x8bc00e72,0x8c0010d3,0x8c400f69,0x8c800a42,0x8cc0024f,0x8d0ff93e,0x8d4ff120,0x8d8febf1,
0x8dcfeb25,0x8e0fef3d,0x8e4ff79d,0x8e800296,0x8ec00dbd,0x8f00166d,0x8f401a67,0x8f80185f,
0x8fc01061,0x900003e4,0x904ff58f,0x908fe8ab,0x90cfe066,0x910fdf12,0x914fe583,0x918ff2c3,
0x91c00427,0x920015d6,0x92402399,0x928029d9,0x92c02684,0x930019aa,0x934005a6,0x938feeb6,
0x93cfda21,0x940fcd09,0x944fcb2c,0x948fd5e8,0x94cfebb1,0x95000838,0x95402531,0x95803baf,
0x95c045b6,0x96003fc0,0x964029c6,0x96800799,0x96cfe04c,0x970fbcdb,0x974fa63a,0x978fa339,
0x97cfb6af,0x980fde6d,0x98401333,0x988049d5,0x98c0755a,0x990089c4,0x99407eed,0x998052d5,
0x99c00ae7,0x9a0fb3cc,0x9a4f5fb8,0x9a8f2367,0x9acf1259,0x9b0f3aea,0x9b4fa313,0x9b804687,
0x9bc1168b,0x9c01fbbc,0x9c42d96f,0x9c839233,0x9cc40c98,0x9d04376b,0x9d440c98,0x9d839233,
0x9dc2d96f,0x9e01fbbc,0x9e41168b,0x9e804687,0x9ecfa313,0x9f0f3aea,0x9f4f1259,0x9f8f2367,
0x9fcf5fb8,0xa00fb3cc,0xa0400ae7,0xa08052d5,0xa0c07eed,0xa10089c4,0xa140755a,0xa18049d5,
0xa1c01333,0xa20fde6d,0xa24fb6af,0xa28fa339,0xa2cfa63a,0xa30fbcdb,0xa34fe04c,0xa3800799,
0xa3c029c6,0xa4003fc0,0xa44045b6,0xa4803baf,0xa4c02531,0xa5000838,0xa54febb1,0xa58fd5e8,
0xa5cfcb2c,0xa60fcd09,0xa64fda21,0xa68feeb6,0xa6c005a6,0xa70019aa,0xa7402684,0xa78029d9,
0xa7c02399,0xa80015d6,0xa8400427,0xa88ff2c3,0xa8cfe583,0xa90fdf12,0xa94fe066,0xa98fe8ab,
0xa9cff58f,0xaa0003e4,0xaa401061,0xaa80185f,0xaac01a67,0xab00166d,0xab400dbd,0xab800296,
0xabcff79d,0xac0fef3d,0xac4feb25,0xac8febf1,0xaccff120,0xad0ff93e,0xad40024f,0xad800a42,
0xadc00f69,0xae0010d3,0xae400e72,0xae80090e,0xaec0020c,0xaf0ffb11,0xaf4ff5a5,0xaf8ff2dd,
0xafcff326,0xb00ff63c,0xb04ffb43,0xb08000ff,0xb0c00627,0xb10009a4,0xb1400ad0,0xb180098f,
0xb1c0064c,0xb20001da,0xb24ffd3d,0xb28ff975,0xb2cff745,0xb30ff70f,0xb34ff8c8,0xb38ffbff,
0xb3cffffc,0xb40003e9,0xb44006fd,0xb48008a2,0xb4c0088b,0xb50006c0,0xb5400392,0xb58fff88,
0xb5cffb3f,0xb60ff751,0xb64ff43b,0xb68ff24a,0xb6cff197,0xb70ff20b,0xb74ff36c,0xb78ff567,
0xb7cff7a8,0xb80ff9e2,0xb84ffbdd,0xb88ffd76,0xb8cffea2,0xb90fff67,0xb94fffd7,0xb980000a,
0xb9c00018,0xba00001b
};

static uint32_t __maybe_unused mtl_windows_dmic_32[] = {
0x00000001,0xffff3210,0xffff3210,0xffffffff,0xffffffff,0x00000003,0x00000003,0x00190844,
0x00110844,0x00000003,0x0000c001,0x0b001800,0x00000000,0x00000e03,0x00000000,0x00000000,
0x00000000,0x00000000,0x00000031,0x00010076,0x00000000,0x00000000,0x00000000,0x00000000,
0x00000000,0x00000000,0x00000031,0x000501e8,0x00000000,0x00000000,0x00000000,0x00000000,
0x00000000,0x00000000,0x40000049,0x40400181,0x4080036d,0x40c00453,0x41000278,0x414fff19,
0x418ffded,0x41c00037,0x42000245,0x4240008c,0x428ffd9b,0x42cffe9d,0x4300026c,0x43400267,
0x438ffdb1,0x43cffc25,0x4400016f,0x44400521,0x448ffff3,0x44cff9c4,0x450ffdf9,0x454006bf,
0x4580048e,0x45cff973,0x460ff896,0x46400572,0x46800a69,0x46cffccf,0x470ff2d1,0x474fffa4,
0x47800f25,0x47c004fc,0x480ff013,0x484ff593,0x48800f33,0x48c01068,0x490ff375,0x494fe999,
0x498007a2,0x49c01b90,0x4a0fff65,0x4a4fe0c2,0x4a8ff799,0x4ac02092,0x4b0012c4,0x4b4fe103,
0x4b8fe22c,0x4bc019f2,0x4c0028a3,0x4c4feef5,0x4c8fce29,0x4cc00484,0x4d003840,0x4d400b2c,
0x4d8fc55e,0x4dcfe2e9,0x4e0037f5,0x4e402ff8,0x4e8fd092,0x4ecfbde1,0x4f0020dd,0x4f405218,
0x4f8ff44f,0x4fcfa2b2,0x500ff04d,0x50406008,0x50802d78,0x50cfa580,0x510fb354,0x51404a82,
0x518069a2,0x51cfd000,0x520f7ef5,0x52400b5c,0x52808ecf,0x52c02152,0x530f70dc,0x534fad96,
0x53807f06,0x53c082c7,0x540fa30a,0x544f538d,0x548028c1,0x54c0c814,0x55001b21,0x554f3223,
0x558f9741,0x55c0b6f2,0x5600b687,0x564f8102,0x568f0843,0x56c02534,0x57011c19,0x5740507e,
0x578eee24,0x57cf2cfd,0x5800c91c,0x58414771,0x588fc601,0x58ce79ba,0x590f6e6b,0x59415ebe,
0x59817000,0x59cf5b76,0x5a0df777,0x5a4f51ac,0x5a81cf09,0x5ac23653,0x5b0fce37,0x5b4d3da8,
0x5b8d65b8,0x5bc050ef,0x5c038e37,0x5c44e2b2,0x5c8411a5,0x5cc259a3,0x5d00f248,0x5d403f0e,
0x5d800839,0x8000001b,0x80400018,0x8080000a,0x80cfffd7,0x810fff67,0x814ffea2,0x818ffd76,
0x81cffbdd,0x820ff9e2,0x824ff7a8,0x828ff567,0x82cff36c,0x830ff20b,0x834ff197,0x838ff24a,
0x83cff43b,0x840ff751,0x844ffb3f,0x848fff88,0x84c00392,0x850006c0,0x8540088b,0x858008a2,
0x85c006fd,0x860003e9,0x864ffffc,0x868ffbff,0x86cff8c8,0x870ff70f,0x874ff745,0x878ff975,
0x87cffd3d,0x880001da,0x8840064c,0x8880098f,0x88c00ad0,0x890009a4,0x89400627,0x898000ff,
0x89cffb43,0x8a0ff63c,0x8a4ff326,0x8a8ff2dd,0x8acff5a5,0x8b0ffb11,0x8b40020c,0x8b80090e,
0x8bc00e72,0x8c0010d3,0x8c400f69,0x8c800a42,0x8cc0024f,0x8d0ff93e,0x8d4ff120,0x8d8febf1,
0x8dcfeb25,0x8e0fef3d,0x8e4ff79d,0x8e800296,0x8ec00dbd,0x8f00166d,0x8f401a67,0x8f80185f,
0x8fc01061,0x900003e4,0x904ff58f,0x908fe8ab,0x90cfe066,0x910fdf12,0x914fe583,0x918ff2c3,
0x91c00427,0x920015d6,0x92402399,0x928029d9,0x92c02684,0x930019aa,0x934005a6,0x938feeb6,
0x93cfda21,0x940fcd09,0x944fcb2c,0x948fd5e8,0x94cfebb1,0x95000838,0x95402531,0x95803baf,
0x95c045b6,0x96003fc0,0x964029c6,0x96800799,0x96cfe04c,0x970fbcdb,0x974fa63a,0x978fa339,
0x97cfb6af,0x980fde6d,0x98401333,0x988049d5,0x98c0755a,0x990089c4,0x99407eed,0x998052d5,
0x99c00ae7,0x9a0fb3cc,0x9a4f5fb8,0x9a8f2367,0x9acf1259,0x9b0f3aea,0x9b4fa313,0x9b804687,
0x9bc1168b,0x9c01fbbc,0x9c42d96f,0x9c839233,0x9cc40c98,0x9d04376b,0x9d440c98,0x9d839233,
0x9dc2d96f,0x9e01fbbc,0x9e41168b,0x9e804687,0x9ecfa313,0x9f0f3aea,0x9f4f1259,0x9f8f2367,
0x9fcf5fb8,0xa00fb3cc,0xa0400ae7,0xa08052d5,0xa0c07eed,0xa10089c4,0xa140755a,0xa18049d5,
0xa1c01333,0xa20fde6d,0xa24fb6af,0xa28fa339,0xa2cfa63a,0xa30fbcdb,0xa34fe04c,0xa3800799,
0xa3c029c6,0xa4003fc0,0xa44045b6,0xa4803baf,0xa4c02531,0xa5000838,0xa54febb1,0xa58fd5e8,
0xa5cfcb2c,0xa60fcd09,0xa64fda21,0xa68feeb6,0xa6c005a6,0xa70019aa,0xa7402684,0xa78029d9,
0xa7c02399,0xa80015d6,0xa8400427,0xa88ff2c3,0xa8cfe583,0xa90fdf12,0xa94fe066,0xa98fe8ab,
0xa9cff58f,0xaa0003e4,0xaa401061,0xaa80185f,0xaac01a67,0xab00166d,0xab400dbd,0xab800296,
0xabcff79d,0xac0fef3d,0xac4feb25,0xac8febf1,0xaccff120,0xad0ff93e,0xad40024f,0xad800a42,
0xadc00f69,0xae0010d3,0xae400e72,0xae80090e,0xaec0020c,0xaf0ffb11,0xaf4ff5a5,0xaf8ff2dd,
0xafcff326,0xb00ff63c,0xb04ffb43,0xb08000ff,0xb0c00627,0xb10009a4,0xb1400ad0,0xb180098f,
0xb1c0064c,0xb20001da,0xb24ffd3d,0xb28ff975,0xb2cff745,0xb30ff70f,0xb34ff8c8,0xb38ffbff,
0xb3cffffc,0xb40003e9,0xb44006fd,0xb48008a2,0xb4c0088b,0xb50006c0,0xb5400392,0xb58fff88,
0xb5cffb3f,0xb60ff751,0xb64ff43b,0xb68ff24a,0xb6cff197,0xb70ff20b,0xb74ff36c,0xb78ff567,
0xb7cff7a8,0xb80ff9e2,0xb84ffbdd,0xb88ffd76,0xb8cffea2,0xb90fff67,0xb94fffd7,0xb980000a,
0xb9c00018,0xba00001b,0x0000c001,0x0b001800,0x00000000,0x00000e03,0x00000000,0x00000000,
0x00000000,0x00000000,0x00000031,0x00010076,0x00000000,0x00000000,0x00000000,0x00000000,
0x00000000,0x00000000,0x00000031,0x000501e8,0x00000000,0x00000000,0x00000000,0x00000000,
0x00000000,0x00000000,0x40000049,0x40400181,0x4080036d,0x40c00453,0x41000278,0x414fff19,
0x418ffded,0x41c00037,0x42000245,0x4240008c,0x428ffd9b,0x42cffe9d,0x4300026c,0x43400267,
0x438ffdb1,0x43cffc25,0x4400016f,0x44400521,0x448ffff3,0x44cff9c4,0x450ffdf9,0x454006bf,
0x4580048e,0x45cff973,0x460ff896,0x46400572,0x46800a69,0x46cffccf,0x470ff2d1,0x474fffa4,
0x47800f25,0x47c004fc,0x480ff013,0x484ff593,0x48800f33,0x48c01068,0x490ff375,0x494fe999,
0x498007a2,0x49c01b90,0x4a0fff65,0x4a4fe0c2,0x4a8ff799,0x4ac02092,0x4b0012c4,0x4b4fe103,
0x4b8fe22c,0x4bc019f2,0x4c0028a3,0x4c4feef5,0x4c8fce29,0x4cc00484,0x4d003840,0x4d400b2c,
0x4d8fc55e,0x4dcfe2e9,0x4e0037f5,0x4e402ff8,0x4e8fd092,0x4ecfbde1,0x4f0020dd,0x4f405218,
0x4f8ff44f,0x4fcfa2b2,0x500ff04d,0x50406008,0x50802d78,0x50cfa580,0x510fb354,0x51404a82,
0x518069a2,0x51cfd000,0x520f7ef5,0x52400b5c,0x52808ecf,0x52c02152,0x530f70dc,0x534fad96,
0x53807f06,0x53c082c7,0x540fa30a,0x544f538d,0x548028c1,0x54c0c814,0x55001b21,0x554f3223,
0x558f9741,0x55c0b6f2,0x5600b687,0x564f8102,0x568f0843,0x56c02534,0x57011c19,0x5740507e,
0x578eee24,0x57cf2cfd,0x5800c91c,0x58414771,0x588fc601,0x58ce79ba,0x590f6e6b,0x59415ebe,
0x59817000,0x59cf5b76,0x5a0df777,0x5a4f51ac,0x5a81cf09,0x5ac23653,0x5b0fce37,0x5b4d3da8,
0x5b8d65b8,0x5bc050ef,0x5c038e37,0x5c44e2b2,0x5c8411a5,0x5cc259a3,0x5d00f248,0x5d403f0e,
0x5d800839,0x8000001b,0x80400018,0x8080000a,0x80cfffd7,0x810fff67,0x814ffea2,0x818ffd76,
0x81cffbdd,0x820ff9e2,0x824ff7a8,0x828ff567,0x82cff36c,0x830ff20b,0x834ff197,0x838ff24a,
0x83cff43b,0x840ff751,0x844ffb3f,0x848fff88,0x84c00392,0x850006c0,0x8540088b,0x858008a2,
0x85c006fd,0x860003e9,0x864ffffc,0x868ffbff,0x86cff8c8,0x870ff70f,0x874ff745,0x878ff975,
0x87cffd3d,0x880001da,0x8840064c,0x8880098f,0x88c00ad0,0x890009a4,0x89400627,0x898000ff,
0x89cffb43,0x8a0ff63c,0x8a4ff326,0x8a8ff2dd,0x8acff5a5,0x8b0ffb11,0x8b40020c,0x8b80090e,
0x8bc00e72,0x8c0010d3,0x8c400f69,0x8c800a42,0x8cc0024f,0x8d0ff93e,0x8d4ff120,0x8d8febf1,
0x8dcfeb25,0x8e0fef3d,0x8e4ff79d,0x8e800296,0x8ec00dbd,0x8f00166d,0x8f401a67,0x8f80185f,
0x8fc01061,0x900003e4,0x904ff58f,0x908fe8ab,0x90cfe066,0x910fdf12,0x914fe583,0x918ff2c3,
0x91c00427,0x920015d6,0x92402399,0x928029d9,0x92c02684,0x930019aa,0x934005a6,0x938feeb6,
0x93cfda21,0x940fcd09,0x944fcb2c,0x948fd5e8,0x94cfebb1,0x95000838,0x95402531,0x95803baf,
0x95c045b6,0x96003fc0,0x964029c6,0x96800799,0x96cfe04c,0x970fbcdb,0x974fa63a,0x978fa339,
0x97cfb6af,0x980fde6d,0x98401333,0x988049d5,0x98c0755a,0x990089c4,0x99407eed,0x998052d5,
0x99c00ae7,0x9a0fb3cc,0x9a4f5fb8,0x9a8f2367,0x9acf1259,0x9b0f3aea,0x9b4fa313,0x9b804687,
0x9bc1168b,0x9c01fbbc,0x9c42d96f,0x9c839233,0x9cc40c98,0x9d04376b,0x9d440c98,0x9d839233,
0x9dc2d96f,0x9e01fbbc,0x9e41168b,0x9e804687,0x9ecfa313,0x9f0f3aea,0x9f4f1259,0x9f8f2367,
0x9fcf5fb8,0xa00fb3cc,0xa0400ae7,0xa08052d5,0xa0c07eed,0xa10089c4,0xa140755a,0xa18049d5,
0xa1c01333,0xa20fde6d,0xa24fb6af,0xa28fa339,0xa2cfa63a,0xa30fbcdb,0xa34fe04c,0xa3800799,
0xa3c029c6,0xa4003fc0,0xa44045b6,0xa4803baf,0xa4c02531,0xa5000838,0xa54febb1,0xa58fd5e8,
0xa5cfcb2c,0xa60fcd09,0xa64fda21,0xa68feeb6,0xa6c005a6,0xa70019aa,0xa7402684,0xa78029d9,
0xa7c02399,0xa80015d6,0xa8400427,0xa88ff2c3,0xa8cfe583,0xa90fdf12,0xa94fe066,0xa98fe8ab,
0xa9cff58f,0xaa0003e4,0xaa401061,0xaa80185f,0xaac01a67,0xab00166d,0xab400dbd,0xab800296,
0xabcff79d,0xac0fef3d,0xac4feb25,0xac8febf1,0xaccff120,0xad0ff93e,0xad40024f,0xad800a42,
0xadc00f69,0xae0010d3,0xae400e72,0xae80090e,0xaec0020c,0xaf0ffb11,0xaf4ff5a5,0xaf8ff2dd,
0xafcff326,0xb00ff63c,0xb04ffb43,0xb08000ff,0xb0c00627,0xb10009a4,0xb1400ad0,0xb180098f,
0xb1c0064c,0xb20001da,0xb24ffd3d,0xb28ff975,0xb2cff745,0xb30ff70f,0xb34ff8c8,0xb38ffbff,
0xb3cffffc,0xb40003e9,0xb44006fd,0xb48008a2,0xb4c0088b,0xb50006c0,0xb5400392,0xb58fff88,
0xb5cffb3f,0xb60ff751,0xb64ff43b,0xb68ff24a,0xb6cff197,0xb70ff20b,0xb74ff36c,0xb78ff567,
0xb7cff7a8,0xb80ff9e2,0xb84ffbdd,0xb88ffd76,0xb8cffea2,0xb90fff67,0xb94fffd7,0xb980000a,
0xb9c00018,0xba00001b
};

static int snd_sof_get_nhlt_endpoint_data(struct snd_sof_dev *sdev, struct snd_sof_dai *dai,
					  struct snd_pcm_hw_params *params, u32 dai_index,
					  u32 linktype, u8 dir, u32 **dst, u32 *len)
{
	struct sof_ipc4_fw_data *ipc4_data = sdev->private;
	struct nhlt_specific_cfg *cfg;
	int sample_rate, channel_count;
	int bit_depth, ret;
	u32 nhlt_type;

	/* convert to NHLT type */
	switch (linktype) {
	case SOF_DAI_INTEL_DMIC:
		nhlt_type = NHLT_LINK_DMIC;
		bit_depth = params_width(params);
		channel_count = params_channels(params);
		sample_rate = params_rate(params);
#if 0 /* HACK for DMIC blob*/
		*len = sizeof(mtl_windows_dmic_32) >> 2;
		*dst = mtl_windows_dmic_32;
		pr_err("bard: using hardcode dmic blob\n");
		return 0;
#else
		pr_err("Fred: disabled bard dmic blob\n");
#endif
		break;
	case SOF_DAI_INTEL_SSP:
		nhlt_type = NHLT_LINK_SSP;
		ret = snd_sof_get_hw_config_params(sdev, dai, &sample_rate, &channel_count,
						   &bit_depth);
		break;
	default:
		return 0;
	}

	if (ret < 0)
		return ret;

	dev_dbg(sdev->dev, "%s: dai index %d nhlt type %d direction %d\n",
		__func__, dai_index, nhlt_type, dir);

	/* find NHLT blob with matching params */
	cfg = intel_nhlt_get_endpoint_blob(sdev->dev, ipc4_data->nhlt, dai_index, nhlt_type,
					   bit_depth, bit_depth, channel_count, sample_rate,
					   dir, 0);

	if (!cfg) {
		dev_err(sdev->dev,
			"no matching blob for sample rate: %d sample width: %d channels: %d\n",
			sample_rate, bit_depth, channel_count);
		return -EINVAL;
	}

	/* config length should be in dwords */
	*len = cfg->size >> 2;
	*dst = (u32 *)cfg->caps;

	return 0;
}
#else
static int snd_sof_get_nhlt_endpoint_data(struct snd_sof_dev *sdev, struct snd_sof_dai *dai,
					  struct snd_pcm_hw_params *params, u32 dai_index,
					  u32 linktype, u8 dir, u32 **dst, u32 *len)
{
	return 0;
}
#endif

static int
sof_ipc4_prepare_copier_module(struct snd_sof_widget *swidget,
			       struct snd_pcm_hw_params *fe_params,
			       struct snd_sof_platform_stream_params *platform_params,
			       struct snd_pcm_hw_params *pipeline_params, int dir)
{
	struct sof_ipc4_available_audio_format *available_fmt;
	struct snd_soc_component *scomp = swidget->scomp;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct sof_ipc4_copier_data *copier_data;
	struct snd_pcm_hw_params *ref_params;
	struct sof_ipc4_copier *ipc4_copier;
	struct snd_sof_dai *dai;
	struct snd_mask *fmt;
	int out_sample_valid_bits;
	size_t ref_audio_fmt_size;
	void **ipc_config_data;
	int *ipc_config_size;
	u32 **data;
	int ipc_size, ret;

	dev_dbg(sdev->dev, "%s: copier %s, type %d", __func__, swidget->widget->name, swidget->id);

	switch (swidget->id) {
	case snd_soc_dapm_aif_in:
	case snd_soc_dapm_aif_out:
	{
		struct sof_ipc4_gtw_attributes *gtw_attr;
		struct snd_sof_widget *pipe_widget;
		struct sof_ipc4_pipeline *pipeline;

		pipe_widget = swidget->pipe_widget;
		pipeline = pipe_widget->private;
		ipc4_copier = (struct sof_ipc4_copier *)swidget->private;
		gtw_attr = ipc4_copier->gtw_attr;
		copier_data = &ipc4_copier->data;
		available_fmt = &ipc4_copier->available_fmt;

		/*
		 * base_config->audio_fmt and out_audio_fmt represent the input and output audio
		 * formats. Use the input format as the reference to match pcm params for playback
		 * and the output format as reference for capture.
		 */
		if (dir == SNDRV_PCM_STREAM_PLAYBACK) {
			available_fmt->ref_audio_fmt = &available_fmt->base_config->audio_fmt;
			ref_audio_fmt_size = sizeof(struct sof_ipc4_base_module_cfg);
		} else {
			available_fmt->ref_audio_fmt = available_fmt->out_audio_fmt;
			ref_audio_fmt_size = sizeof(struct sof_ipc4_audio_format);
		}
		copier_data->gtw_cfg.node_id &= ~SOF_IPC4_NODE_INDEX_MASK;
		copier_data->gtw_cfg.node_id |=
			SOF_IPC4_NODE_INDEX(platform_params->stream_tag - 1);

		/* set gateway attributes */
		gtw_attr->lp_buffer_alloc = pipeline->lp_mode;
		ref_params = fe_params;
		break;
	}
	case snd_soc_dapm_dai_in:
	case snd_soc_dapm_dai_out:
	{
		dai = swidget->private;

		ipc4_copier = (struct sof_ipc4_copier *)dai->private;
		copier_data = &ipc4_copier->data;
		available_fmt = &ipc4_copier->available_fmt;
		if (dir == SNDRV_PCM_STREAM_CAPTURE) {
			available_fmt->ref_audio_fmt = available_fmt->out_audio_fmt;
			ref_audio_fmt_size = sizeof(struct sof_ipc4_audio_format);

			/*
			 * modify the input params for the dai copier as it only supports
			 * 32-bit always
			 */
			fmt = hw_param_mask(pipeline_params, SNDRV_PCM_HW_PARAM_FORMAT);
			snd_mask_none(fmt);
			snd_mask_set_format(fmt, SNDRV_PCM_FORMAT_S32_LE);
		} else {
			available_fmt->ref_audio_fmt = &available_fmt->base_config->audio_fmt;
			ref_audio_fmt_size = sizeof(struct sof_ipc4_base_module_cfg);
		}

		ref_params = pipeline_params;

		ret = snd_sof_get_nhlt_endpoint_data(sdev, dai, fe_params, ipc4_copier->dai_index,
						     ipc4_copier->dai_type, dir,
						     &ipc4_copier->copier_config,
						     &copier_data->gtw_cfg.config_length);
		if (ret < 0)
			return ret;

		break;
	}
	default:
		dev_err(sdev->dev, "unsupported type %d for copier %s",
			swidget->id, swidget->widget->name);
		return -EINVAL;
	}

	/* set input and output audio formats */
	ret = sof_ipc4_init_audio_fmt(sdev, swidget, &copier_data->base_config,
				      &copier_data->out_format, ref_params,
				      available_fmt, ref_audio_fmt_size);
	if (ret < 0)
		return ret;

	switch (swidget->id) {
	case snd_soc_dapm_dai_in:
	case snd_soc_dapm_dai_out:
	{
		/*
		 * Only SOF_DAI_INTEL_ALH needs copier_data to set blob.
		 * That's why only ALH dai's blob is set after sof_ipc4_init_audio_fmt
		 */
		if (ipc4_copier->dai_type == SOF_DAI_INTEL_ALH) {
			struct sof_ipc4_alh_configuration_blob *blob;
			struct sof_ipc4_copier_data *alh_data;
			struct sof_ipc4_copier *alh_copier;
			struct snd_sof_widget *w;
			u32 ch_mask = 0;
			u32 ch_map;
			int i;

			blob = (struct sof_ipc4_alh_configuration_blob *)ipc4_copier->copier_config;

			blob->gw_attr.lp_buffer_alloc = 0;

			/* Get channel_mask from ch_map */
			ch_map = copier_data->base_config.audio_fmt.ch_map;
			for (i = 0; ch_map; i++) {
				if ((ch_map & 0xf) != 0xf)
					ch_mask |= BIT(i);
				ch_map >>= 4;
			}

			/*
			 * Set each gtw_cfg.node_id to blob->alh_cfg.mapping[]
			 * for all widgets with the same stream name
			 */
			i = 0;
			list_for_each_entry(w, &sdev->widget_list, list) {
				if (w->widget->sname &&
				    strcmp(w->widget->sname, swidget->widget->sname))
					continue;

				dai = w->private;
				alh_copier = (struct sof_ipc4_copier *)dai->private;
				alh_data = &alh_copier->data;
				blob->alh_cfg.mapping[i].alh_id = alh_data->gtw_cfg.node_id;
				blob->alh_cfg.mapping[i].channel_mask = ch_mask;
				i++;
			}
			if (blob->alh_cfg.count > 1) {
				int group_id;

				group_id = ida_alloc_max(&alh_group_ida, ALH_MULTI_GTW_COUNT,
							 GFP_KERNEL);

				if (group_id < 0)
					return group_id;

				/* add multi-gateway base */
				group_id += ALH_MULTI_GTW_BASE;
				copier_data->gtw_cfg.node_id &= ~SOF_IPC4_NODE_INDEX_MASK;
				copier_data->gtw_cfg.node_id |= SOF_IPC4_NODE_INDEX(group_id);
			}
		}
	}
	}

	/* modify the input params for the next widget */
	fmt = hw_param_mask(pipeline_params, SNDRV_PCM_HW_PARAM_FORMAT);
	out_sample_valid_bits =
		SOF_IPC4_AUDIO_FORMAT_CFG_V_BIT_DEPTH(copier_data->out_format.fmt_cfg);
	snd_mask_none(fmt);
	switch (out_sample_valid_bits) {
	case 16:
		snd_mask_set_format(fmt, SNDRV_PCM_FORMAT_S16_LE);
		break;
	case 24:
		snd_mask_set_format(fmt, SNDRV_PCM_FORMAT_S24_LE);
		break;
	case 32:
		snd_mask_set_format(fmt, SNDRV_PCM_FORMAT_S32_LE);
		break;
	default:
		dev_err(sdev->dev, "invalid sample frame format %d\n",
			params_format(pipeline_params));
		return -EINVAL;
	}

	/* set the gateway dma_buffer_size using the matched ID returned above */
	copier_data->gtw_cfg.dma_buffer_size = available_fmt->dma_buffer_size[ret];

	data = &ipc4_copier->copier_config;
	ipc_config_size = &ipc4_copier->ipc_config_size;
	ipc_config_data = &ipc4_copier->ipc_config_data;

	/* config_length is DWORD based */
	ipc_size = sizeof(*copier_data) + copier_data->gtw_cfg.config_length * 4;

	dev_dbg(sdev->dev, "copier %s, IPC size is %d", swidget->widget->name, ipc_size);

	*ipc_config_data = kzalloc(ipc_size, GFP_KERNEL);
	if (!*ipc_config_data)
		return -ENOMEM;

	*ipc_config_size = ipc_size;

	/* copy IPC data */
	memcpy(*ipc_config_data, (void *)copier_data, sizeof(*copier_data));
	if (copier_data->gtw_cfg.config_length)
		memcpy(*ipc_config_data + sizeof(*copier_data),
		       *data, copier_data->gtw_cfg.config_length * 4);

	/* update pipeline memory usage */
	sof_ipc4_update_pipeline_mem_usage(sdev, swidget, &copier_data->base_config);

	/* assign instance ID */
	return sof_ipc4_widget_assign_instance_id(sdev, swidget);
}

static void sof_ipc4_unprepare_generic_module(struct snd_sof_widget *swidget)
{
	struct sof_ipc4_fw_module *fw_module = swidget->module_info;

	ida_free(&fw_module->m_ida, swidget->instance_id);
}

static int sof_ipc4_prepare_gain_module(struct snd_sof_widget *swidget,
					struct snd_pcm_hw_params *fe_params,
					struct snd_sof_platform_stream_params *platform_params,
					struct snd_pcm_hw_params *pipeline_params, int dir)
{
	struct snd_soc_component *scomp = swidget->scomp;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct sof_ipc4_gain *gain = swidget->private;
	int ret;

	gain->available_fmt.ref_audio_fmt = &gain->available_fmt.base_config->audio_fmt;

	/* output format is not required to be sent to the FW for gain */
	ret = sof_ipc4_init_audio_fmt(sdev, swidget, &gain->base_config,
				      NULL, pipeline_params, &gain->available_fmt,
				      sizeof(gain->base_config));
	if (ret < 0)
		return ret;

	/* update pipeline memory usage */
	sof_ipc4_update_pipeline_mem_usage(sdev, swidget, &gain->base_config);

	/* assign instance ID */
	return sof_ipc4_widget_assign_instance_id(sdev, swidget);
}

static int sof_ipc4_prepare_mixer_module(struct snd_sof_widget *swidget,
					 struct snd_pcm_hw_params *fe_params,
					 struct snd_sof_platform_stream_params *platform_params,
					 struct snd_pcm_hw_params *pipeline_params, int dir)
{
	struct snd_soc_component *scomp = swidget->scomp;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct sof_ipc4_mixer *mixer = swidget->private;
	int ret;

	/* only 32bit is supported by mixer */
	mixer->available_fmt.ref_audio_fmt = &mixer->available_fmt.base_config->audio_fmt;

	/* output format is not required to be sent to the FW for mixer */
	ret = sof_ipc4_init_audio_fmt(sdev, swidget, &mixer->base_config,
				      NULL, pipeline_params, &mixer->available_fmt,
				      sizeof(mixer->base_config));
	if (ret < 0)
		return ret;

	/* update pipeline memory usage */
	sof_ipc4_update_pipeline_mem_usage(sdev, swidget, &mixer->base_config);

	/* assign instance ID */
	return sof_ipc4_widget_assign_instance_id(sdev, swidget);
}

static int sof_ipc4_control_load_volume(struct snd_sof_dev *sdev, struct snd_sof_control *scontrol)
{
	struct sof_ipc4_control_data *control_data;
	struct sof_ipc4_msg *msg;
	int i;

	scontrol->size = struct_size(control_data, chanv, scontrol->num_channels);

	/* scontrol->ipc_control_data will be freed in sof_control_unload */
	scontrol->ipc_control_data = kzalloc(scontrol->size, GFP_KERNEL);
	if (!scontrol->ipc_control_data)
		return -ENOMEM;

	control_data = scontrol->ipc_control_data;
	control_data->index = scontrol->index;

	msg = &control_data->msg;
	msg->primary = SOF_IPC4_MSG_TYPE_SET(SOF_IPC4_MOD_LARGE_CONFIG_SET);
	msg->primary |= SOF_IPC4_MSG_DIR(SOF_IPC4_MSG_REQUEST);
	msg->primary |= SOF_IPC4_MSG_TARGET(SOF_IPC4_MODULE_MSG);

	msg->extension = SOF_IPC4_MOD_EXT_MSG_PARAM_ID(SOF_IPC4_GAIN_PARAM_ID);

	/* set default volume values to 0dB in control */
	for (i = 0; i < scontrol->num_channels; i++) {
		control_data->chanv[i].channel = i;
		control_data->chanv[i].value = SOF_IPC4_VOL_ZERO_DB;
	}

	return 0;
}

static int sof_ipc4_control_setup(struct snd_sof_dev *sdev, struct snd_sof_control *scontrol)
{
	switch (scontrol->info_type) {
	case SND_SOC_TPLG_CTL_VOLSW:
	case SND_SOC_TPLG_CTL_VOLSW_SX:
	case SND_SOC_TPLG_CTL_VOLSW_XR_SX:
		return sof_ipc4_control_load_volume(sdev, scontrol);
	default:
		break;
	}

	return 0;
}

static int sof_ipc4_widget_setup(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget)
{
	struct snd_sof_widget *pipe_widget = swidget->pipe_widget;
	struct sof_ipc4_pipeline *pipeline;
	struct sof_ipc4_msg *msg;
	void *ipc_data = NULL;
	u32 ipc_size = 0;
	int ret;

	dev_dbg(sdev->dev, "Create widget %s instance %d - pipe %d - core %d\n",
		swidget->widget->name, swidget->instance_id, swidget->pipeline_id, swidget->core);

	switch (swidget->id) {
	case snd_soc_dapm_scheduler:
		pipeline = swidget->private;

		dev_dbg(sdev->dev, "pipeline: %d memory pages: %d\n", swidget->pipeline_id,
			pipeline->mem_usage);

		msg = &pipeline->msg;
		msg->primary |= pipeline->mem_usage;
		break;
	case snd_soc_dapm_aif_in:
	case snd_soc_dapm_aif_out:
	{
		struct sof_ipc4_copier *ipc4_copier = swidget->private;

		ipc_size = ipc4_copier->ipc_config_size;
		ipc_data = ipc4_copier->ipc_config_data;

		msg = &ipc4_copier->msg;
		break;
	}
	case snd_soc_dapm_dai_in:
	case snd_soc_dapm_dai_out:
	{
		struct snd_sof_dai *dai = swidget->private;
		struct sof_ipc4_copier *ipc4_copier = dai->private;

		ipc_size = ipc4_copier->ipc_config_size;
		ipc_data = ipc4_copier->ipc_config_data;

		msg = &ipc4_copier->msg;
		break;
	}
	case snd_soc_dapm_pga:
	{
		struct sof_ipc4_gain *gain = swidget->private;

		ipc_size = sizeof(struct sof_ipc4_base_module_cfg) +
			   sizeof(struct sof_ipc4_gain_data);
		ipc_data = gain;

		msg = &gain->msg;
		break;
	}
	case snd_soc_dapm_mixer:
	{
		struct sof_ipc4_mixer *mixer = swidget->private;

		ipc_size = sizeof(mixer->base_config);
		ipc_data = &mixer->base_config;

		msg = &mixer->msg;
		break;
	}
	default:
		dev_err(sdev->dev, "widget type %d not supported", swidget->id);
		return -EINVAL;
	}

	if (swidget->id != snd_soc_dapm_scheduler) {
		pipeline = pipe_widget->private;
		msg->primary &= ~SOF_IPC4_MOD_INSTANCE_MASK;
		msg->primary |= SOF_IPC4_MOD_INSTANCE(swidget->instance_id);

		msg->extension &= ~SOF_IPC4_MOD_EXT_PARAM_SIZE_MASK;
		msg->extension |= ipc_size >> 2;
		msg->extension &= ~SOF_IPC4_MOD_EXT_DOMAIN_MASK;
		msg->extension |= SOF_IPC4_MOD_EXT_DOMAIN(pipeline->lp_mode);
	}

	msg->data_size = ipc_size;
	msg->data_ptr = ipc_data;

	ret = sof_ipc_tx_message(sdev->ipc, msg, ipc_size, NULL, 0);
	if (ret < 0)
		dev_err(sdev->dev, "failed to create module %s\n", swidget->widget->name);

	return ret;
}

static int sof_ipc4_widget_free(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget)
{
	int ret = 0;

	/* freeing a pipeline frees all the widgets associated with it */
	if (swidget->id == snd_soc_dapm_scheduler) {
		struct sof_ipc4_pipeline *pipeline = swidget->private;
		struct sof_ipc4_msg msg = {{ 0 }};
		u32 header;

		header = SOF_IPC4_GLB_PIPE_INSTANCE_ID(swidget->pipeline_id);
		header |= SOF_IPC4_MSG_TYPE_SET(SOF_IPC4_GLB_DELETE_PIPELINE);
		header |= SOF_IPC4_MSG_DIR(SOF_IPC4_MSG_REQUEST);
		header |= SOF_IPC4_MSG_TARGET(SOF_IPC4_FW_GEN_MSG);

		msg.primary = header;

		ret = sof_ipc_tx_message(sdev->ipc, &msg, 0, NULL, 0);
		if (ret < 0)
			dev_err(sdev->dev, "failed to free pipeline widget %s\n",
				swidget->widget->name);

		pipeline->mem_usage = 0;
		pipeline->state = SOF_IPC4_PIPE_UNINITIALIZED;
	}

	return ret;
}

static int sof_ipc4_route_setup(struct snd_sof_dev *sdev, struct snd_sof_route *sroute)
{
	struct snd_sof_widget *src_widget = sroute->src_widget;
	struct snd_sof_widget *sink_widget = sroute->sink_widget;
	struct sof_ipc4_fw_module *src_fw_module = src_widget->module_info;
	struct sof_ipc4_fw_module *sink_fw_module = sink_widget->module_info;
	struct sof_ipc4_msg msg = {{ 0 }};
	u32 header, extension;
	int src_queue = 0;
	int dst_queue = 0;
	int ret;

	dev_dbg(sdev->dev, "%s: bind %s -> %s\n", __func__,
		src_widget->widget->name, sink_widget->widget->name);

	header = src_fw_module->man4_module_entry.id;
	header |= SOF_IPC4_MOD_INSTANCE(src_widget->instance_id);
	header |= SOF_IPC4_MSG_TYPE_SET(SOF_IPC4_MOD_BIND);
	header |= SOF_IPC4_MSG_DIR(SOF_IPC4_MSG_REQUEST);
	header |= SOF_IPC4_MSG_TARGET(SOF_IPC4_MODULE_MSG);

	extension = sink_fw_module->man4_module_entry.id;
	extension |= SOF_IPC4_MOD_EXT_DST_MOD_INSTANCE(sink_widget->instance_id);
	extension |= SOF_IPC4_MOD_EXT_DST_MOD_QUEUE_ID(dst_queue);
	extension |= SOF_IPC4_MOD_EXT_SRC_MOD_QUEUE_ID(src_queue);

	msg.primary = header;
	msg.extension = extension;

	ret = sof_ipc_tx_message(sdev->ipc, &msg, 0, NULL, 0);
	if (ret < 0)
		dev_err(sdev->dev, "%s: failed to bind modules %s -> %s\n",
			__func__, src_widget->widget->name, sink_widget->widget->name);

	return ret;
}

static int sof_ipc4_route_free(struct snd_sof_dev *sdev, struct snd_sof_route *sroute)
{
	struct snd_sof_widget *src_widget = sroute->src_widget;
	struct snd_sof_widget *sink_widget = sroute->sink_widget;
	struct sof_ipc4_fw_module *src_fw_module = src_widget->module_info;
	struct sof_ipc4_fw_module *sink_fw_module = sink_widget->module_info;
	struct sof_ipc4_msg msg = {{ 0 }};
	u32 header, extension;
	int src_queue = 0;
	int dst_queue = 0;
	int ret;

	dev_dbg(sdev->dev, "%s: unbind modules %s -> %s\n", __func__,
		src_widget->widget->name, sink_widget->widget->name);

	header = src_fw_module->man4_module_entry.id;
	header |= SOF_IPC4_MOD_INSTANCE(src_widget->instance_id);
	header |= SOF_IPC4_MSG_TYPE_SET(SOF_IPC4_MOD_UNBIND);
	header |= SOF_IPC4_MSG_DIR(SOF_IPC4_MSG_REQUEST);
	header |= SOF_IPC4_MSG_TARGET(SOF_IPC4_MODULE_MSG);

	extension = sink_fw_module->man4_module_entry.id;
	extension |= SOF_IPC4_MOD_EXT_DST_MOD_INSTANCE(sink_widget->instance_id);
	extension |= SOF_IPC4_MOD_EXT_DST_MOD_QUEUE_ID(dst_queue);
	extension |= SOF_IPC4_MOD_EXT_SRC_MOD_QUEUE_ID(src_queue);

	msg.primary = header;
	msg.extension = extension;

	ret = sof_ipc_tx_message(sdev->ipc, &msg, 0, NULL, 0);
	if (ret < 0)
		dev_err(sdev->dev, "failed to unbind modules %s -> %s\n",
			src_widget->widget->name, sink_widget->widget->name);

	return ret;
}

static int sof_ipc4_dai_config(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget,
			       unsigned int flags, struct snd_sof_dai_config_data *data)
{
	struct snd_sof_widget *pipe_widget = swidget->pipe_widget;
	struct sof_ipc4_pipeline *pipeline = pipe_widget->private;
	struct snd_sof_dai *dai = swidget->private;
	struct sof_ipc4_gtw_attributes *gtw_attr;
	struct sof_ipc4_copier_data *copier_data;
	struct sof_ipc4_copier *ipc4_copier;

	if (!dai || !dai->private) {
		dev_err(sdev->dev, "Invalid DAI or DAI private data for %s\n",
			swidget->widget->name);
		return -EINVAL;
	}

	ipc4_copier = (struct sof_ipc4_copier *)dai->private;
	copier_data = &ipc4_copier->data;

	if (!data)
		return 0;

	switch (ipc4_copier->dai_type) {
	case SOF_DAI_INTEL_HDA:
		gtw_attr = ipc4_copier->gtw_attr;
		gtw_attr->lp_buffer_alloc = pipeline->lp_mode;
		fallthrough;
	case SOF_DAI_INTEL_ALH:
		copier_data->gtw_cfg.node_id &= ~SOF_IPC4_NODE_INDEX_MASK;
		copier_data->gtw_cfg.node_id |= SOF_IPC4_NODE_INDEX(data->dai_data);
		break;
	case SOF_DAI_INTEL_DMIC:
	case SOF_DAI_INTEL_SSP:
		/* nothing to do for SSP/DMIC */
		break;
	default:
		dev_err(sdev->dev, "%s: unsupported dai type %d\n", __func__,
			ipc4_copier->dai_type);
		return -EINVAL;
	}

	return 0;
}

static int sof_ipc4_parse_manifest(struct snd_soc_component *scomp, int index,
				   struct snd_soc_tplg_manifest *man)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct sof_ipc4_fw_data *ipc4_data = sdev->private;
	struct sof_manifest_tlv *manifest_tlv;
	struct sof_manifest *manifest;
	u32 size = le32_to_cpu(man->priv.size);
	u8 *man_ptr = man->priv.data;
	u32 len_check;
	int i;

	if (!size || size < SOF_IPC4_TPLG_ABI_SIZE) {
		dev_err(scomp->dev, "%s: Invalid topology ABI size: %u\n",
			__func__, size);
		return -EINVAL;
	}

	manifest = (struct sof_manifest *)man_ptr;

	dev_info(scomp->dev,
		 "Topology: ABI %d:%d:%d Kernel ABI %u:%u:%u\n",
		  le16_to_cpu(manifest->abi_major), le16_to_cpu(manifest->abi_minor),
		  le16_to_cpu(manifest->abi_patch),
		  SOF_ABI_MAJOR, SOF_ABI_MINOR, SOF_ABI_PATCH);

	/* TODO: Add ABI compatibility check */

	/* no more data after the ABI version */
	if (size <= SOF_IPC4_TPLG_ABI_SIZE)
		return 0;

	manifest_tlv = manifest->items;
	len_check = sizeof(struct sof_manifest);
	for (i = 0; i < le16_to_cpu(manifest->count); i++) {
		len_check += sizeof(struct sof_manifest_tlv) + le32_to_cpu(manifest_tlv->size);
		if (len_check > size)
			return -EINVAL;

		switch (le32_to_cpu(manifest_tlv->type)) {
		case SOF_MANIFEST_DATA_TYPE_NHLT:
			/* no NHLT in BIOS, so use the one from topology manifest */
			if (ipc4_data->nhlt)
				break;
			ipc4_data->nhlt = devm_kmemdup(sdev->dev, manifest_tlv->data,
						       le32_to_cpu(manifest_tlv->size), GFP_KERNEL);
			if (!ipc4_data->nhlt)
				return -ENOMEM;
			break;
		default:
			dev_warn(scomp->dev, "Skipping unknown manifest data type %d\n",
				 manifest_tlv->type);
			break;
		}
		man_ptr += sizeof(struct sof_manifest_tlv) + le32_to_cpu(manifest_tlv->size);
		manifest_tlv = (struct sof_manifest_tlv *)man_ptr;
	}

	return 0;
}

static int sof_ipc4_dai_get_clk(struct snd_sof_dev *sdev, struct snd_sof_dai *dai, int clk_type)
{
	struct sof_ipc4_copier *ipc4_copier = dai->private;
	struct snd_soc_tplg_hw_config *hw_config;
	struct snd_sof_dai_link *slink;
	bool dai_link_found = false;
	bool hw_cfg_found = false;
	int i;

	if (!ipc4_copier)
		return 0;

	list_for_each_entry(slink, &sdev->dai_link_list, list) {
		if (!strcmp(slink->link->name, dai->name)) {
			dai_link_found = true;
			break;
		}
	}

	if (!dai_link_found) {
		dev_err(sdev->dev, "no DAI link found for DAI %s\n", dai->name);
		return -EINVAL;
	}

	for (i = 0; i < slink->num_hw_configs; i++) {
		hw_config = &slink->hw_configs[i];
		if (dai->current_config == le32_to_cpu(hw_config->id)) {
			hw_cfg_found = true;
			break;
		}
	}

	if (!hw_cfg_found) {
		dev_err(sdev->dev, "no matching hw_config found for DAI %s\n", dai->name);
		return -EINVAL;
	}

	switch (ipc4_copier->dai_type) {
	case SOF_DAI_INTEL_SSP:
		switch (clk_type) {
		case SOF_DAI_CLK_INTEL_SSP_MCLK:
			return le32_to_cpu(hw_config->mclk_rate);
		case SOF_DAI_CLK_INTEL_SSP_BCLK:
			return le32_to_cpu(hw_config->bclk_rate);
		default:
			dev_err(sdev->dev, "Invalid clk type for SSP %d\n", clk_type);
			break;
		}
		break;
	default:
		dev_err(sdev->dev, "DAI type %d not supported yet!\n", ipc4_copier->dai_type);
		break;
	}

	return -EINVAL;
}

static enum sof_tokens host_token_list[] = {
	SOF_COMP_TOKENS,
	SOF_AUDIO_FMT_NUM_TOKENS,
	SOF_AUDIO_FORMAT_BUFFER_SIZE_TOKENS,
	SOF_IN_AUDIO_FORMAT_TOKENS,
	SOF_OUT_AUDIO_FORMAT_TOKENS,
	SOF_COPIER_GATEWAY_CFG_TOKENS,
	SOF_COPIER_TOKENS,
	SOF_COMP_EXT_TOKENS,
};

static enum sof_tokens pipeline_token_list[] = {
	SOF_SCHED_TOKENS,
	SOF_PIPELINE_TOKENS,
};

static enum sof_tokens dai_token_list[] = {
	SOF_COMP_TOKENS,
	SOF_AUDIO_FMT_NUM_TOKENS,
	SOF_AUDIO_FORMAT_BUFFER_SIZE_TOKENS,
	SOF_IN_AUDIO_FORMAT_TOKENS,
	SOF_OUT_AUDIO_FORMAT_TOKENS,
	SOF_COPIER_GATEWAY_CFG_TOKENS,
	SOF_COPIER_TOKENS,
	SOF_DAI_TOKENS,
	SOF_COMP_EXT_TOKENS,
};

static enum sof_tokens pga_token_list[] = {
	SOF_COMP_TOKENS,
	SOF_GAIN_TOKENS,
	SOF_AUDIO_FMT_NUM_TOKENS,
	SOF_AUDIO_FORMAT_BUFFER_SIZE_TOKENS,
	SOF_IN_AUDIO_FORMAT_TOKENS,
	SOF_COMP_EXT_TOKENS,
};

static enum sof_tokens mixer_token_list[] = {
	SOF_COMP_TOKENS,
	SOF_AUDIO_FMT_NUM_TOKENS,
	SOF_IN_AUDIO_FORMAT_TOKENS,
	SOF_AUDIO_FORMAT_BUFFER_SIZE_TOKENS,
	SOF_COMP_EXT_TOKENS,
};

static const struct sof_ipc_tplg_widget_ops tplg_ipc4_widget_ops[SND_SOC_DAPM_TYPE_COUNT] = {
	[snd_soc_dapm_aif_in] =  {sof_ipc4_widget_setup_pcm, sof_ipc4_widget_free_comp_pcm,
				  host_token_list, ARRAY_SIZE(host_token_list), NULL,
				  sof_ipc4_prepare_copier_module,
				  sof_ipc4_unprepare_copier_module},
	[snd_soc_dapm_aif_out] = {sof_ipc4_widget_setup_pcm, sof_ipc4_widget_free_comp_pcm,
				  host_token_list, ARRAY_SIZE(host_token_list), NULL,
				  sof_ipc4_prepare_copier_module,
				  sof_ipc4_unprepare_copier_module},
	[snd_soc_dapm_dai_in] = {sof_ipc4_widget_setup_comp_dai, sof_ipc4_widget_free_comp_dai,
				 dai_token_list, ARRAY_SIZE(dai_token_list), NULL,
				 sof_ipc4_prepare_copier_module,
				 sof_ipc4_unprepare_copier_module},
	[snd_soc_dapm_dai_out] = {sof_ipc4_widget_setup_comp_dai, sof_ipc4_widget_free_comp_dai,
				  dai_token_list, ARRAY_SIZE(dai_token_list), NULL,
				  sof_ipc4_prepare_copier_module,
				  sof_ipc4_unprepare_copier_module},
	[snd_soc_dapm_scheduler] = {sof_ipc4_widget_setup_comp_pipeline, sof_ipc4_widget_free_comp,
				    pipeline_token_list, ARRAY_SIZE(pipeline_token_list), NULL,
				    NULL, NULL},
	[snd_soc_dapm_pga] = {sof_ipc4_widget_setup_comp_pga, sof_ipc4_widget_free_comp,
			      pga_token_list, ARRAY_SIZE(pga_token_list), NULL,
			      sof_ipc4_prepare_gain_module,
			      sof_ipc4_unprepare_generic_module},
	[snd_soc_dapm_mixer] = {sof_ipc4_widget_setup_comp_mixer, sof_ipc4_widget_free_comp,
				mixer_token_list, ARRAY_SIZE(mixer_token_list),
				NULL, sof_ipc4_prepare_mixer_module,
				sof_ipc4_unprepare_generic_module},
};

const struct sof_ipc_tplg_ops ipc4_tplg_ops = {
	.widget = tplg_ipc4_widget_ops,
	.token_list = ipc4_token_list,
	.control_setup = sof_ipc4_control_setup,
	.control = &tplg_ipc4_control_ops,
	.widget_setup = sof_ipc4_widget_setup,
	.widget_free = sof_ipc4_widget_free,
	.route_setup = sof_ipc4_route_setup,
	.route_free = sof_ipc4_route_free,
	.dai_config = sof_ipc4_dai_config,
	.parse_manifest = sof_ipc4_parse_manifest,
	.dai_get_clk = sof_ipc4_dai_get_clk,
};
