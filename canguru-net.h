#ifndef CANGURU_NET_H
#define CANGURU_NET_H

#include <linux/can/dev.h>

#include "guru-msg.h"

struct guru_device;
struct canguru_msg_net;

struct canguru_channel_conf {
	struct guru_device *guru_dev;
	struct canguru_msg_net *msg;
	struct guru_send_reply *tx_reply_buff;
	u8 tx_reply_max_count;
	u8 channel_idx;
	const u16 *termination_list;
	u8 termination_count;
};

struct canguru_priv {
	/* must be the first member */
	struct can_priv can;

	struct guru_device *guru_dev;
	struct canguru_msg_net *canguru_msg;
	u8 channel_idx;

	/* tx transfer */
	struct guru_send_reply *reply;
	u8 reply_tail;
	u8 reply_head;
	u8 reply_max_count;
	spinlock_t tx_lock;
};

int canguru_net_create(struct net_device **new_dev,
		       const struct canguru_channel_conf *conf);
void canguru_net_destroy(struct net_device **netdev);
void canguru_net_recv(struct net_device *netdev, u8 *data);
void canguru_net_tx_finished(struct net_device *netdev, u32 fifo_mask);

#endif // CANGURU_NET_H
