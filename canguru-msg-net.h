#ifndef CANGURU_MSG_NET_H
#define CANGURU_MSG_NET_H

#include "guru-msg.h"

struct guru_device;

enum guru_msg_can_id {
	CAN_MSG_CONFIG_SET = 0x0C00,
	CAN_MSG_CONFIG_GET = 0x0C01,
	CAN_MSG_GLOBAL_FILTER_SET = 0x0C02,
	CAN_MSG_GLOBAL_FILTER_GET = 0x0C03,
	CAN_MSG_ENABLE_SET = 0x0C06,
	CAN_MSG_ENABLE_GET = 0x0C07,
	CAN_MSG_RECV_ENABLE_SET = 0x0C08,
	CAN_MSG_RECV_ENABLE_GET = 0x0C09,
	CAN_MSG_SEND = 0x0C0A,
	CAN_MSG_RECV = 0x0C0B,
	CAN_MSG_RESET = 0x0C0D,
	CAN_MSG_EVENT = 0x0C0E,
	CAN_MSG_STATUS_GET = 0x0C12,
	CAN_MSG_STATUS_CHANGED = 0x0C13,
	CAN_MSG_SEND_FINISHED = 0x0C14,
	CAN_MSG_EVENT_ENABLE = 0x0C15,
	CAN_MSG_TER_ENABLE,
	CAN_MSG_TIMING_SET,
	CAN_MSG_TIMING_GET
};

struct canguru_msg_net_conf {
	u8 iface_count;
	struct net_device **netdev;
	struct guru_device *guru_dev;
};

struct canguru_msg_net {
	u8 iface_count;
	struct net_device **netdev;
	struct guru_device *guru_dev;

	/* CanGuru answer */
	struct guru_msg msg_reset;
	struct guru_msg msg_set_conf;
	struct guru_msg msg_set_timing;
	struct guru_msg msg_set_global_filter;
	struct guru_msg msg_set_enable;
	struct guru_msg msg_set_ter;
	struct guru_msg msg_send;
	struct guru_msg msg_recv;
	struct guru_msg msg_send_finished;

	struct list_head send_reply_list;
};

int canguru_msg_net_init(struct canguru_msg_net *self,
			 const struct canguru_msg_net_conf *conf);
void canguru_wait_send_reply(struct canguru_msg_net *self,
			     struct guru_send_reply *reply);

#endif // CANGURU_MSG_NET_H
