#ifndef CANGURU_HOBBY_DEVICE_H
#define CANGURU_HOBBY_DEVICE_H

#include "canguru-msg-net.h"
#include "guru-device.h"

#define CANGURU_HOBBY_CHANNEL_COUNT 2
#define CANGURU_HOBBY_TX_FIFO_SIZE 32

struct canguru_hobby_device {
	/* must be the first member */
	struct guru_device guru_dev;

	struct net_device *netdev[CANGURU_HOBBY_CHANNEL_COUNT];
	struct guru_send_reply tx_reply_buff[CANGURU_HOBBY_CHANNEL_COUNT]
					    [CANGURU_HOBBY_TX_FIFO_SIZE];
	struct canguru_msg_net can_msg_net;
};

int canguru_hobby_dev_init(struct canguru_hobby_device *self,
			  struct usb_interface *intf, struct class *guru_class);

#endif // CANGURU_HOBBY_DEVICE_H
