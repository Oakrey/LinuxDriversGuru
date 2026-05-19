#include "canguru-lite-device.h"

#include "canguru-net.h"

#define CANGURU_TERMINATION_DISABLED CAN_TERMINATION_DISABLED
#define CANGURU_TERMINATION_ENABLED 120

static const u16 canguru_termination[] = { CANGURU_TERMINATION_DISABLED,
					   CANGURU_TERMINATION_ENABLED };

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
	net_conf.termination_list = canguru_termination;
	net_conf.termination_count = ARRAY_SIZE(canguru_termination);

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
