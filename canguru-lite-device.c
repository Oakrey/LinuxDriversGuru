#include "canguru-lite-device.h"

#include "canguru-net.h"

static void canguru_dev_deinit(struct canguru_lite_device *self);

int canguru_lite_dev_init(struct canguru_lite_device *self,
			  struct usb_interface *intf, struct class *guru_class)
{
	struct canguru_channel_conf net_conf;
	const struct canguru_msg_net_conf msg_conf = {
		.guru_dev = &self->guru_dev,
		.netdev = self->netdev,
		.iface_count = CANGURU_LITE_CHANNEL_COUNT
	};
	int idx;
	int err;

	self->guru_dev.dev_data = self;
	self->guru_dev.dev_deinit = (void (*)(void *))canguru_dev_deinit;

	for (idx = 0; idx < CANGURU_LITE_CHANNEL_COUNT; idx++) {
		self->netdev[idx] = NULL;
	}

	err = guru_dev_init(&self->guru_dev, intf, guru_class);
	if (err != 0) {
		return err;
	}

	net_conf.guru_dev = &self->guru_dev;
	net_conf.msg = &self->can_msg_net;
	net_conf.tx_reply_max_count = CANGURU_LITE_TX_FIFO_SIZE;

	for (idx = 0; idx < CANGURU_LITE_CHANNEL_COUNT; idx++) {
		net_conf.channel_idx = idx;
		net_conf.tx_reply_buff = self->tx_reply_buff[idx];
		canguru_net_create(&self->netdev[idx], &net_conf);
	}

	err = canguru_msg_net_init(&self->can_msg_net, &msg_conf);
	if (err != 0) {
		return err;
	}

	return 0;
}

static void canguru_dev_deinit(struct canguru_lite_device *self)
{
	int idx;

	for (idx = 0; idx < CANGURU_LITE_CHANNEL_COUNT; idx++) {
		canguru_net_destroy(&self->netdev[idx]);
	}
}
