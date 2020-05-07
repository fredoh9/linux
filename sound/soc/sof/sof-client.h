/* SPDX-License-Identifier: (GPL-2.0-only) */

#ifndef __SOUND_SOC_SOF_CLIENT_H
#define __SOUND_SOC_SOF_CLIENT_H

#include <linux/completion.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/device/driver.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/virtual_bus.h>
#include <sound/soc.h>

#define SOF_CLIENT_PROBE_TIMEOUT_MS 2000

struct snd_sof_dev;

enum sof_client_type {
	SOF_CLIENT_AUDIO,
	SOF_CLIENT_IPC,
};

/* SOF client device */
struct sof_client_dev {
	struct virtbus_device vdev;
	struct snd_sof_dev *sdev;
	struct list_head list;	/* item in SOF core client drv list */
	struct completion probe_complete;
	struct snd_soc_card card;
	char *drv_name;		/* platform drv name */
	void *data;
};

/* client-specific ops, all optional */
struct sof_client_ops {
	int (*client_ipc_rx)(struct sof_client_dev *cdev, u32 msg_cmd);
};

struct sof_client_drv {
	const char *name;
	enum sof_client_type type;
	const struct sof_client_ops ops;
	struct virtbus_driver virtbus_drv;
};

#define virtbus_dev_to_sof_client_dev(virtbus_dev) \
	container_of(virtbus_dev, struct sof_client_dev, vdev)

static inline int sof_client_drv_register(struct sof_client_drv *drv)
{
	return virtbus_register_driver(&drv->virtbus_drv);
}

static inline void sof_client_drv_unregister(struct sof_client_drv *drv)
{
	virtbus_unregister_driver(&drv->virtbus_drv);
}

int sof_client_dev_register(struct snd_sof_dev *sdev,
			    const char *name);

static inline void sof_client_dev_unregister(struct sof_client_dev *cdev)
{
	virtbus_unregister_device(&cdev->vdev);
}

int sof_client_ipc_tx_message(struct sof_client_dev *cdev, u32 header,
			      void *msg_data, size_t msg_bytes,
			      void *reply_data, size_t reply_bytes);

struct dentry *sof_client_get_debugfs_root(struct sof_client_dev *cdev);

/* SOF client host PCM ops */
int sof_client_pcm_hw_params(struct snd_soc_component *component,
			     struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params);
int sof_client_pcm_hw_free(struct snd_soc_component *component,
			   struct snd_pcm_substream *substream);
int sof_client_pcm_prepare(struct snd_soc_component *component,
			   struct snd_pcm_substream *substream);
int sof_client_pcm_trigger(struct snd_soc_component *component,
			   struct snd_pcm_substream *substream, int cmd);
snd_pcm_uframes_t sof_client_pcm_pointer(struct snd_soc_component *component,
					 struct snd_pcm_substream *substream);
int sof_client_pcm_open(struct snd_soc_component *component,
			struct snd_pcm_substream *substream);
int sof_client_pcm_close(struct snd_soc_component *component,
			 struct snd_pcm_substream *substream);
int sof_client_pcm_new(struct snd_soc_component *component,
		       struct snd_soc_pcm_runtime *rtd);
int sof_client_pcm_dai_link_fixup(struct snd_soc_pcm_runtime *rtd,
				  struct snd_pcm_hw_params *params);
int sof_client_pcm_probe(struct snd_soc_component *component);
void sof_client_pcm_remove(struct snd_soc_component *component);

struct snd_soc_dai_driver *sof_client_get_dai_drv(struct sof_client_dev *cdev);
int sof_client_get_num_dai_drv(struct sof_client_dev *cdev);

/**
 * module_sof_client_driver() - Helper macro for registering an SOF Client
 * driver
 * @__sof_client_driver: SOF client driver struct
 *
 * Helper macro for SOF client drivers which do not do anything special in
 * module init/exit. This eliminates a lot of boilerplate. Each module may only
 * use this macro once, and calling it replaces module_init() and module_exit()
 */
#define module_sof_client_driver(__sof_client_driver) \
	module_driver(__sof_client_driver, sof_client_drv_register, \
			sof_client_drv_unregister)

#endif
