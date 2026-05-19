#include "canguru-net.h"

#include "guru-device.h"
#include "canguru-msg-net.h"

#define CANGURU_CAN_CLK 80000000
#define CANGURU_100US_TO_1NS 100000
#define CAN_STD_ID_MASK 0x7ffU
#define CAN_EXT_ID_MASK 0x1fffffffU

static const struct can_bittiming_const canguru_bittiming_arb = {
	.name = "canguru_arb",
	.tseg1_min = 2,
	.tseg1_max = 256,
	.tseg2_min = 2,
	.tseg2_max = 128,
	.sjw_max = 128,
	.brp_min = 1,
	.brp_max = 512,
	.brp_inc = 1,
};

static const struct can_bittiming_const canguru_bittiming_data = {
	.name = "canguru_data",
	.tseg1_min = 1,
	.tseg1_max = 32,
	.tseg2_min = 1,
	.tseg2_max = 16,
	.sjw_max = 16,
	.brp_min = 1,
	.brp_max = 32,
	.brp_inc = 1,
};

enum can_conf_mode {
	CAN_NORMAL = 0,
	CAN_BUS_MONITORING,
	CAN_RESTRICTED_OPERATION
};

struct __packed can_msg_header {
	u32 can_id : 29;
	u32 can_dlc : 4;
	bool extended_id : 1;
	bool remote_frame : 1;
	bool esi : 1;
	bool canfd : 1;
	bool bitrate_switch : 1;
	u32 reserved : 2;
};

struct __packed can_recv_header {
	u8 iface;
	u32 time_stamp;
	struct can_msg_header can_header;
};

struct __packed can_recv_msg {
	struct can_recv_header recv_header;
	u8 data[CANFD_MAX_DLEN];
};

struct __packed can_send_header {
	u8 iface;
	struct can_msg_header can_header;
};

struct __packed can_send_msg {
	struct can_send_header send_header;
	u8 data[CANFD_MAX_DLEN];
};

struct __packed can_enable_msg {
	u8 iface;
	bool enable;
};

struct __packed can_conf_msg {
	u8 iface;
	u32 baudRateA; /* Baud rate of Arbitration phase in bps. */
	u32 baudRateD; /* Baud rate of Data phase in bps. */
	u8 canFdEnable; /* Enable or Disable CANFD. */
	enum can_conf_mode canMode : 8;
};

struct can_timing {
	uint32_t samplePoint;
	uint32_t idealTqNum;
	uint32_t syncJumpWidth;
};

struct __packed can_timing_msg {
	u8 iface;
	struct can_timing timingNominal;
	struct can_timing timingData;
};

struct __packed can_global_filter_msg {
	u8 iface;
	bool stdDefaultAllow; /* All standard frames are by default accepted */
	bool extDefaultAllow; /* All extended frames are by default accepted */
	bool stdRemoteReject; /* Reject all standard remote frames */
	bool extRemoteReject; /* Reject all extended remote frames */
};

static void canguru_init_can_priv(struct canguru_priv *priv,
				  const struct canguru_channel_conf *conf);
static void canguru_set_conf(struct canguru_priv *priv,
			     const struct canguru_channel_conf *conf);
static int get_tx_fifo_free(struct canguru_priv *priv);
static int pull_tx_fifo_idx(struct canguru_priv *priv);
static int push_tx_fifo_idx(struct canguru_priv *priv);
static void reset_tx_fifo(struct canguru_priv *priv);

static int canguru_ops_open(struct net_device *netdev);
static int canguru_ops_close(struct net_device *netdev);
static netdev_tx_t canguru_start_xmit(struct sk_buff *skb,
				      struct net_device *netdev);

static int canguru_set_termination(struct net_device *netdev, u16 term);
static int canguru_set_mode(struct net_device *netdev, enum can_mode mode);
static int canguru_set_bittiming_nominal(struct net_device *netdev);
static int canguru_set_bittiming_data(struct net_device *netdev);
static int canguru_write_bittiming(struct canguru_priv *priv);
static int canguru_set_can_conf(struct canguru_priv *priv,
				const struct can_conf_msg *value);
static int canguru_set_can_timing(struct canguru_priv *priv,
				  const struct can_timing_msg *value);
static int canguru_set_can_enable(struct canguru_priv *priv, bool enable);
static int canguru_set_can_ter(struct canguru_priv *priv, bool enable);
static int canguru_can_reset(struct canguru_priv *priv);
static int canguru_set_global_filter(struct canguru_priv *priv);
static int canguru_can_fd_send(struct canguru_priv *priv,
			       struct canfd_frame *frame);
static int canguru_can_classic_send(struct canguru_priv *priv,
				    struct can_frame *frame);
static void canguru_send_callback(struct canguru_priv *priv,
				  enum guru_status status);

static inline struct canguru_priv *canguru_get_priv(struct net_device *netdev)
{
	return (struct canguru_priv *)netdev_priv(netdev);
}

static inline int mask_bits_count(struct canguru_priv *priv, u32 mask)
{
	int idx;
	int counter = 0;
	u32 counter_mask = 1;

	for (idx = 0; idx < priv->reply_max_count; idx++) {
		if ((counter_mask & mask) != 0) {
			counter++;
		}
		counter_mask <<= 1U;
	}
	return counter;
}

static const struct net_device_ops canguru_netdev_ops = {
	.ndo_open = canguru_ops_open,
	.ndo_stop = canguru_ops_close,
	.ndo_start_xmit = canguru_start_xmit
};
static const struct ethtool_ops canguru_ethtool_ops = {
	.get_ts_info = ethtool_op_get_ts_info,
};

/* CanGuru net public methods implementation */

int canguru_net_create(struct net_device **new_dev,
		       const struct canguru_channel_conf *conf)
{
	struct net_device *netdev;
	struct canguru_priv *priv;
	int err;

	netdev = alloc_candev(sizeof(struct canguru_priv), GURU_MAX_URBS);
	if (netdev == NULL) {
		dev_err(conf->guru_dev->dev, "Couldn't alloc candev\n");
		*new_dev = NULL;
		return -ENOMEM;
	}

	SET_NETDEV_DEV(netdev, conf->guru_dev->dev);
	priv = canguru_get_priv(netdev);
	spin_lock_init(&priv->tx_lock);
	canguru_set_conf(priv, conf);
	canguru_init_can_priv(priv, conf);
	err = canguru_can_reset(priv);
	if (err != 0) {
		dev_err(conf->guru_dev->dev, "Unable to CAN reset (%d).\n",
			err);
		goto err;
	}

	err = canguru_set_global_filter(priv);
	if (err != 0) {
		dev_err(conf->guru_dev->dev,
			"Unable to set CAN global filter (%d).\n", err);
		goto err;
	}

	/* IFF_ECHO must be set if can_put_echo_skb and can_get_echo_skb is used */
	netdev->flags |= IFF_ECHO;
	netdev->netdev_ops = &canguru_netdev_ops;
	netdev->ethtool_ops = &canguru_ethtool_ops;
	err = register_candev(netdev);
	if (err != 0) {
		dev_err(conf->guru_dev->dev, "Unable to register CAN (%d).\n",
			err);
		goto err;
	}
	*new_dev = netdev;
	return 0;
err:
	free_candev(netdev);
	*new_dev = NULL;
	return err;
}

void canguru_net_destroy(struct net_device **netdev)
{
	if (*netdev == NULL) {
		return;
	}

	unregister_candev(*netdev);
	free_candev(*netdev);
	*netdev = NULL;
}

void canguru_net_recv(struct net_device *netdev, u8 *data)
{
	struct canguru_priv *priv = canguru_get_priv(netdev);
	struct can_recv_msg *msg = (struct can_recv_msg *)data;
	struct canfd_frame *frame_fd;
	struct can_frame *frame_cc;
	struct sk_buff *skb;
	u8 dataLen;
	struct skb_shared_hwtstamps *hwts;
	u64 time_stamp;

	if (msg->recv_header.can_header.canfd == true) {
		dataLen = can_fd_dlc2len(msg->recv_header.can_header.can_dlc);
		skb = alloc_canfd_skb(netdev, &frame_fd);
		frame_fd->can_id = msg->recv_header.can_header.can_id;
		frame_fd->len = dataLen;

		if (msg->recv_header.can_header.esi == true) {
			frame_fd->flags |= CANFD_ESI;
		}
		if (msg->recv_header.can_header.bitrate_switch == true) {
			frame_fd->flags |= CANFD_BRS;
		}
		if (msg->recv_header.can_header.remote_frame == true) {
			frame_fd->can_id |= CAN_RTR_FLAG;
		}
		if (msg->recv_header.can_header.extended_id == true) {
			frame_fd->can_id |= CAN_EFF_FLAG;
		}
		memcpy(frame_fd->data, msg->data, dataLen);

	} else {
		dataLen = can_cc_dlc2len(msg->recv_header.can_header.can_dlc);
		skb = alloc_can_skb(netdev, &frame_cc);
		frame_cc->can_id = msg->recv_header.can_header.can_id;
		can_frame_set_cc_len(frame_cc,
				     msg->recv_header.can_header.can_dlc,
				     priv->can.ctrlmode);

		if (msg->recv_header.can_header.remote_frame == true) {
			frame_cc->can_id |= CAN_RTR_FLAG;
		}
		if (msg->recv_header.can_header.extended_id == true) {
			frame_cc->can_id |= CAN_EFF_FLAG;
		}
		memcpy(frame_cc->data, msg->data, dataLen);
	}

	netdev->stats.rx_packets++;
	netdev->stats.rx_bytes += dataLen;
	hwts = skb_hwtstamps(skb);
	time_stamp = msg->recv_header.time_stamp;
	time_stamp *= CANGURU_100US_TO_1NS;
	hwts->hwtstamp = ns_to_ktime(time_stamp);
	netif_rx(skb);
}

void canguru_net_tx_finished(struct net_device *netdev, u32 fifo_mask)
{
	struct canguru_priv *priv = canguru_get_priv(netdev);
	const int pkt_count = mask_bits_count(priv, fifo_mask);
	unsigned int byte_count;
	unsigned int frame_len = 0;
	unsigned int total_frames_len = 0;
	unsigned long flags;
	int reply_idx;
	int pkt_idx;

	spin_lock_irqsave(&priv->tx_lock, flags);
	for (pkt_idx = 0; pkt_idx < pkt_count; pkt_idx++) {
		reply_idx = push_tx_fifo_idx(priv);
		if (reply_idx < 0) {
			dev_err(priv->guru_dev->dev,
				"err finished tx fifo idx (mask): %d(0x%x)",
				reply_idx, fifo_mask);
			spin_unlock_irqrestore(&priv->tx_lock, flags);
			return;
		}
		byte_count = can_get_echo_skb(netdev, reply_idx, &frame_len);
		netdev->stats.tx_packets++;
		netdev->stats.tx_bytes += byte_count;
		total_frames_len += frame_len;
	}
	netdev_completed_queue(netdev, pkt_count, total_frames_len);
	if (netif_queue_stopped(netdev))
		netif_wake_queue(netdev);
	spin_unlock_irqrestore(&priv->tx_lock, flags);
}

/* CanGuru net private methods implementation */

static void canguru_init_can_priv(struct canguru_priv *priv,
				  const struct canguru_channel_conf *conf)
{
	priv->can.state = CAN_STATE_STOPPED;
	priv->can.clock.freq = CANGURU_CAN_CLK;
	priv->can.termination_const = conf->termination_list;
	priv->can.termination_const_cnt = conf->termination_count;
	priv->can.bittiming_const = &canguru_bittiming_arb;
	priv->can.fd.data_bittiming_const = &canguru_bittiming_data;
	priv->can.ctrlmode_supported = CAN_CTRLMODE_LISTENONLY |
				       CAN_CTRLMODE_CC_LEN8_DLC |
				       CAN_CTRLMODE_FD;

	priv->can.do_set_termination = canguru_set_termination;
	priv->can.do_set_mode = canguru_set_mode;
	priv->can.do_set_bittiming = canguru_set_bittiming_nominal;
	priv->can.fd.do_set_data_bittiming = canguru_set_bittiming_data;
}

static void canguru_set_conf(struct canguru_priv *priv,
			     const struct canguru_channel_conf *conf)
{
	u8 idx;

	priv->guru_dev = conf->guru_dev;
	priv->canguru_msg = conf->msg;
	priv->reply = conf->tx_reply_buff;
	priv->reply_max_count = conf->tx_reply_max_count;
	reset_tx_fifo(priv);
	priv->channel_idx = conf->channel_idx;
	for (idx = 0; idx < priv->reply_max_count; idx++) {
		priv->reply[idx].user_data = priv;
		priv->reply[idx].status_callback = (void (*)(
			void *, enum guru_status))canguru_send_callback;
	}
}

static int get_tx_fifo_free(struct canguru_priv *priv)
{
	int free_items;

	free_items = priv->reply_max_count + priv->reply_tail;
	free_items -= priv->reply_head;
	free_items -= 1;

	if (free_items >= priv->reply_max_count) {
		free_items -= priv->reply_max_count;
	}
	return free_items;
}

static int pull_tx_fifo_idx(struct canguru_priv *priv)
{
	int idx;
	u8 next_head;

	if (get_tx_fifo_free(priv) <= 0)
		return -ENOMEM;

	next_head = priv->reply_head;
	idx = next_head;
	next_head++;
	if (next_head >= priv->reply_max_count)
		next_head = 0;
	priv->reply_head = next_head;

	return idx;
}

static int push_tx_fifo_idx(struct canguru_priv *priv)
{
	u8 next_tail;
	int idx;

	if (get_tx_fifo_free(priv) >= priv->reply_max_count - 1)
		return -EPERM;

	next_tail = priv->reply_tail;
	idx = next_tail;
	next_tail++;
	if (next_tail >= priv->reply_max_count)
		next_tail = 0;
	priv->reply_tail = next_tail;
	return idx;
}

static void reset_tx_fifo(struct canguru_priv *priv)
{
	priv->reply_head = 0;
	priv->reply_tail = 0;
}

/**
 * canguru_ops_open() - Enable the network device.
 * @netdev: CAN network device.
 *
 * Called when the network transitions to the up state. Allocate the
 * URB resources if needed and open the channel.
 */
static int canguru_ops_open(struct net_device *netdev)
{
	struct canguru_priv *priv = canguru_get_priv(netdev);
	int err;

	err = open_candev(netdev);
	if (err != 0) {
		return err;
	}

	priv->can.state = CAN_STATE_ERROR_ACTIVE;
	netif_start_queue(netdev);
	canguru_set_can_enable(priv, true);

	return 0;
}

/**
 * canguru_ops_close() - Disable the network device.
 * @netdev: CAN network device.
 *
 * Called when the network transitions to the down state. If all the
 * channels of the device are closed, free the URB resources which are
 * not needed anymore.
 */
static int canguru_ops_close(struct net_device *netdev)
{
	struct canguru_priv *priv = canguru_get_priv(netdev);

	canguru_set_can_enable(priv, false);
	priv->can.state = CAN_STATE_STOPPED;
	netif_stop_queue(netdev);
	close_candev(netdev);
	return 0;
}

/**
 * canguru_start_xmit() - Transmit an skb.
 * @skb: socket buffer of a CAN message.
 * @netdev: CAN network device.
 *
 * Called when a packet needs to be transmitted.
 */
static netdev_tx_t canguru_start_xmit(struct sk_buff *skb,
				      struct net_device *netdev)
{
	struct canguru_priv *priv = canguru_get_priv(netdev);
	unsigned long flags;
	u32 frame_len;
	int reply_idx = 0;
	int err;

	if (can_dev_dropped_skb(netdev, skb))
		return NETDEV_TX_OK;

	spin_lock_irqsave(&priv->tx_lock, flags);
	reply_idx = pull_tx_fifo_idx(priv);
	if (reply_idx < 0) {
		spin_unlock_irqrestore(&priv->tx_lock, flags);
		return NETDEV_TX_BUSY;
	}

	if (get_tx_fifo_free(priv) == 0)
		netif_stop_queue(netdev);
	spin_unlock_irqrestore(&priv->tx_lock, flags);

	if (can_is_canfd_skb(skb) == true)
		err = canguru_can_fd_send(priv,
					  (struct canfd_frame *)skb->data);
	else
		err = canguru_can_classic_send(priv,
					       (struct can_frame *)skb->data);

	if (err != 0)
		goto send_err;

	frame_len = can_skb_get_frame_len(skb);
	err = can_put_echo_skb(skb, netdev, reply_idx, frame_len);
	if (err != 0)
		goto fail;

	canguru_wait_send_reply(priv->canguru_msg, &priv->reply[reply_idx]);
	netdev_sent_queue(netdev, frame_len);
	return NETDEV_TX_OK;
send_err:
	netdev->stats.tx_dropped++;
	dev_kfree_skb(skb);
	if (netif_queue_stopped(netdev))
		netif_wake_queue(netdev);
fail:
	dev_warn(priv->guru_dev->dev, "Unable to send CAN frame.\n");
	return NETDEV_TX_OK;
}

static int canguru_set_termination(struct net_device *netdev, u16 term)
{
	struct canguru_priv *priv = canguru_get_priv(netdev);
	bool terEnable = term != 0;

	return canguru_set_can_ter(priv, terEnable);
}

static int canguru_set_mode(__maybe_unused struct net_device *netdev,
			    __maybe_unused enum can_mode mode)
{
	return 0;
}

static int canguru_set_bittiming_nominal(struct net_device *netdev)
{
	struct canguru_priv *priv = canguru_get_priv(netdev);
	int err;

	if ((priv->can.ctrlmode & CAN_CTRLMODE_FD) != 0) {
		return 0;
	}

	err = canguru_write_bittiming(priv);
	return err;
}

static int canguru_set_bittiming_data(struct net_device *netdev)
{
	struct canguru_priv *priv = canguru_get_priv(netdev);
	int err;

	err = canguru_write_bittiming(priv);
	return err;
}

static int canguru_write_bittiming(struct canguru_priv *priv)
{
	const u32 prescaler = priv->can.bittiming.brp + 1;
	const u32 prescalerData = priv->can.fd.data_bittiming.brp + 1;
	const struct can_conf_msg conf = {
		.iface = priv->channel_idx,
		.baudRateA = priv->can.bittiming.bitrate,
		.baudRateD = priv->can.fd.data_bittiming.bitrate,
		.canFdEnable = (priv->can.ctrlmode & CAN_CTRLMODE_FD) != 0,
		.canMode = (priv->can.ctrlmode & CAN_CTRLMODE_LISTENONLY) != 0 ?
				   CAN_BUS_MONITORING :
				   CAN_NORMAL
	};
	struct can_timing_msg timingConf = {
		.iface = priv->channel_idx,
		.timingNominal = { .samplePoint =
					   priv->can.bittiming.sample_point,
				   .idealTqNum = 0,
				   .syncJumpWidth = priv->can.bittiming.sjw },
		.timingData = { .samplePoint =
					priv->can.fd.data_bittiming.sample_point,
				.idealTqNum = 0,
				.syncJumpWidth =
					priv->can.fd.data_bittiming.sjw }
	};
	int err;

	if (priv->can.bittiming.bitrate != 0) {
		timingConf.timingNominal.idealTqNum =
			CANGURU_CAN_CLK / prescaler /
			priv->can.bittiming.bitrate;
	}
	if (priv->can.fd.data_bittiming.bitrate != 0) {
		timingConf.timingData.idealTqNum =
			CANGURU_CAN_CLK / prescalerData /
			priv->can.fd.data_bittiming.bitrate;
	}
	err = canguru_set_can_conf(priv, &conf);
	if (err != 0) {
		dev_warn(priv->guru_dev->dev, "Unable to set config CAN bus\n");
		return err;
	}

	err = canguru_set_can_timing(priv, &timingConf);
	if (err != 0) {
		dev_warn(priv->guru_dev->dev, "Unable to set timing CAN bus\n");
		return err;
	}
	return 0;
}

static int canguru_set_can_conf(struct canguru_priv *priv,
				const struct can_conf_msg *value)
{
	int err;

	err = guru_send_data(priv->guru_dev, CAN_MSG_CONFIG_SET, (u8 *)value,
			     sizeof(struct can_conf_msg));
	if (err != 0) {
		return err;
	}

	return 0;
}

static int canguru_set_can_timing(struct canguru_priv *priv,
				  const struct can_timing_msg *value)
{
	int err;

	err = guru_send_data(priv->guru_dev, CAN_MSG_TIMING_SET, (u8 *)value,
			     sizeof(struct can_timing_msg));
	if (err != 0) {
		return err;
	}

	return 0;
}

static int canguru_set_can_enable(struct canguru_priv *priv, bool enable)
{
	const struct can_enable_msg msg = { .iface = priv->channel_idx,
					    .enable = enable };

	return guru_send_data(priv->guru_dev, CAN_MSG_ENABLE_SET, (u8 *)&msg,
			      sizeof(struct can_enable_msg));
}

static int canguru_set_can_ter(struct canguru_priv *priv, bool enable)
{
	const struct can_enable_msg msg = { .iface = priv->channel_idx,
					    .enable = enable };

	return guru_send_data(priv->guru_dev, CAN_MSG_TER_ENABLE, (u8 *)&msg,
			      sizeof(struct can_enable_msg));
}

static int canguru_can_reset(struct canguru_priv *priv)
{
	return guru_send_data(priv->guru_dev, CAN_MSG_RESET, &priv->channel_idx,
			      sizeof(u8));
}

static int canguru_set_global_filter(struct canguru_priv *priv)
{
	const struct can_global_filter_msg msg = { .iface = priv->channel_idx,
						   .stdDefaultAllow = true,
						   .extDefaultAllow = true,
						   .stdRemoteReject = false,
						   .extRemoteReject = false };

	return guru_send_data(priv->guru_dev, CAN_MSG_GLOBAL_FILTER_SET,
			      (u8 *)&msg, sizeof(struct can_global_filter_msg));
}

static int canguru_can_fd_send(struct canguru_priv *priv,
			       struct canfd_frame *frame)
{
	struct can_send_msg msg = { 0 };

	msg.send_header.iface = priv->channel_idx;
	msg.send_header.can_header.canfd = true;
	msg.send_header.can_header.esi = (frame->flags & CANFD_ESI) != 0;
	msg.send_header.can_header.bitrate_switch =
		(frame->flags & CANFD_BRS) != 0;
	msg.send_header.can_header.can_dlc = can_fd_len2dlc(frame->len);

	msg.send_header.can_header.remote_frame =
		(frame->can_id & CAN_RTR_FLAG) != 0;
	if ((frame->can_id & CAN_EFF_FLAG) != 0) {
		msg.send_header.can_header.extended_id = true;
		msg.send_header.can_header.can_id = frame->can_id &
						    CAN_EXT_ID_MASK;
	} else {
		msg.send_header.can_header.can_id = frame->can_id &
						    CAN_STD_ID_MASK;
	}

	memcpy(msg.data, frame->data, frame->len);
	return guru_send_data(priv->guru_dev, CAN_MSG_SEND,
			      (const uint8_t *)&msg,
			      sizeof(struct can_send_header) + frame->len);
}

static int canguru_can_classic_send(struct canguru_priv *priv,
				    struct can_frame *frame)
{
	struct can_send_msg msg = { 0 };

	msg.send_header.iface = priv->channel_idx;
	msg.send_header.can_header.can_dlc =
		can_get_cc_dlc(frame, priv->can.ctrlmode);

	msg.send_header.can_header.remote_frame =
		(frame->can_id & CAN_RTR_FLAG) != 0;
	if ((frame->can_id & CAN_EFF_FLAG) != 0) {
		msg.send_header.can_header.extended_id = true;
		msg.send_header.can_header.can_id = frame->can_id &
						    CAN_EXT_ID_MASK;
	} else {
		msg.send_header.can_header.can_id = frame->can_id &
						    CAN_STD_ID_MASK;
	}

	memcpy(msg.data, frame->data, frame->len);
	return guru_send_data(priv->guru_dev, CAN_MSG_SEND,
			      (const uint8_t *)&msg,
			      sizeof(struct can_send_header) + frame->len);
}

static void canguru_send_callback(struct canguru_priv *priv,
				  enum guru_status status)
{
	struct net_device *netdev = priv->can.dev;
	unsigned long flags;
	unsigned int frame_len = 0;
	int reply_idx;

	if (status == GURU_STATUS_DONE)
		return;
	// TODO: alloc_can_err_skb, netif_receive_skb
	spin_lock_irqsave(&priv->tx_lock, flags);
	reply_idx = push_tx_fifo_idx(priv);
	if (reply_idx < 0) {
		dev_warn(priv->guru_dev->dev, "err send tx fifo idx: %d",
			 reply_idx);
		spin_unlock_irqrestore(&priv->tx_lock, flags);
		return;
	}
	can_free_echo_skb(netdev, reply_idx, &frame_len);
	switch (status) {
	case GURU_STATUS_ERROR:
		netdev->stats.tx_errors++;
		dev_info(priv->guru_dev->dev, "error tx free echo %u",
			 frame_len);
		break;
	default:
		netdev->stats.tx_dropped++;
		dev_info(priv->guru_dev->dev, "drop tx free echo %u",
			 frame_len);
		break;
	}
	netdev_completed_queue(netdev, 1, frame_len);
	if (netif_queue_stopped(netdev))
		netif_wake_queue(netdev);
	spin_unlock_irqrestore(&priv->tx_lock, flags);
}
