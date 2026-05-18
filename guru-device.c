#include "guru-device.h"

#define GURU_CMD_PREAMBLE 0x58
#define GURU_CTRL_FLAG_REPLY 0x1
#define GURU_MAX_DATA_SIZE 123
#define GURU_DATA_EP 0x1
#define GURU_STR_LEN_MAX 32

struct __packed guru_msg_header {
	u8 preamble;
	u8 ctrl;
	u16 id;
	u8 data_count;
};

struct __packed guru_data {
	struct guru_msg_header header;
	uint8_t data[GURU_MAX_DATA_SIZE];
};

static int canguru_usb_rx_init(struct guru_device *self);
static void canguru_read_bulk_callback(struct urb *urb);
static void canguru_write_bulk_callback(struct urb *urb);
static void canguru_process_msg_list(struct guru_device *self,
				     struct guru_data *msg);

/* CanGuru device public methods implementation */

int guru_dev_init(struct guru_device *self, struct usb_interface *intf,
		  struct class *guru_class)
{
	struct usb_device *usbdev = interface_to_usbdev(intf);
	int err;

	INIT_LIST_HEAD(&self->msg_list);

	self->usbdev = usbdev;
	self->guru_class = guru_class;
	self->dev = &intf->dev;
	self->intf = intf;

	init_usb_anchor(&self->rx_submitted);
	init_usb_anchor(&self->tx_submitted);
	usb_set_intfdata(intf, self);

	err = canguru_usb_rx_init(self);
	if (err != 0) {
		return err;
	}

	err = guru_msg_std_init(&self->guru_msg_std, self);
	if (err != 0) {
		return err;
	}

	guru_get_fw_ver(&self->guru_msg_std);
	return 0;
}

void guru_dev_deinit(struct guru_device *self)
{
	usb_set_intfdata(self->intf, NULL);

	usb_kill_anchored_urbs(&self->rx_submitted);
	usb_kill_anchored_urbs(&self->tx_submitted);

	if (self->dev_deinit != NULL) {
		self->dev_deinit(self->dev_data);
	}
}

int guru_send_data(struct guru_device *self, uint16_t id, const uint8_t *data,
		   uint8_t data_len)
{
	struct urb *urb;
	u8 *buf;
	struct guru_data *msg;
	int err = 0;
	size_t msg_size = sizeof(struct guru_msg_header) + data_len;

	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (urb == NULL) {
		dev_err(self->dev, "Unable to alloc TX URB.\n");
		err = -ENOMEM;
		goto failed_urb;
	}

	buf = usb_alloc_coherent(self->usbdev, msg_size, GFP_ATOMIC,
				 &urb->transfer_dma);
	if (buf == NULL) {
		dev_err(self->dev, "Unable to alloc TX buffer.\n");
		err = -ENOMEM;
		goto failed_alloc;
	}

	msg = (struct guru_data *)buf;

	msg->header.preamble = GURU_CMD_PREAMBLE;
	msg->header.id = id;
	msg->header.ctrl = 0;
	msg->header.data_count = data_len;
	if (data != NULL) {
		memcpy(msg->data, data, data_len);
	}

	usb_fill_bulk_urb(urb, self->usbdev,
			  usb_sndbulkpipe(self->usbdev, GURU_DATA_EP), buf,
			  msg_size, canguru_write_bulk_callback, self);

	usb_anchor_urb(urb, &self->tx_submitted);
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err != 0) {
		dev_err(self->dev, "Unable to submit URB TX (%d).\n", err);
		goto failed;
	}

	usb_free_urb(urb);
	return 0;

failed:
	usb_unanchor_urb(urb);
	usb_free_coherent(self->usbdev, msg_size, buf, urb->transfer_dma);
failed_alloc:
	usb_free_urb(urb);

failed_urb:
	switch (err) {
	case -ENOENT: /* driver removed */
	case -ENODEV: /* device removed */
		break;
	default:
		dev_err(self->dev, "Unable to send CAN TX data (%d).\n", err);
		break;
	}

	return err;
}

u8 *guru_get_msg_data(struct guru_device *self)
{
	return self->curr_msg_data;
}

u8 guru_get_msg_data_size(struct guru_device *self)
{
	return self->curr_msg_data_size;
}

u16 guru_get_msg_id(struct guru_device *self)
{
	return self->curr_msg_id;
}

void guru_add_msg(struct guru_device *self, struct guru_msg *msg)
{
	list_add_tail(&msg->node, &self->msg_list);
}

/* CanGuru private methods implementation */

static int canguru_usb_rx_init(struct guru_device *self)
{
	dma_addr_t buf_dma;
	struct urb *urb;
	u8 *buf;
	int idx;
	int err = 0;

	for (idx = 0; idx < GURU_MAX_URBS; idx++) {
		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (urb == NULL) {
			err = -ENOBUFS;
			break;
		}

		buf = usb_alloc_coherent(self->usbdev, sizeof(struct guru_data),
					 GFP_KERNEL, &buf_dma);
		if (buf == NULL) {
			usb_free_urb(urb);
			err = -ENOBUFS;
			break;
		}

		urb->transfer_dma = buf_dma;
		usb_fill_bulk_urb(urb, self->usbdev,
				  usb_rcvbulkpipe(self->usbdev, GURU_DATA_EP),
				  buf, sizeof(struct guru_data),
				  canguru_read_bulk_callback, self);
		urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
		usb_anchor_urb(urb, &self->rx_submitted);

		err = usb_submit_urb(urb, GFP_KERNEL);
		if (err != 0) {
			dev_err(self->dev, "Unable to submit RX URB (%d).\n",
				err);
			usb_unanchor_urb(urb);
			usb_free_coherent(self->usbdev,
					  sizeof(struct guru_data), buf,
					  urb->transfer_dma);
			usb_free_urb(urb);
			break;
		}

		self->rxbuf[idx] = buf;
		self->rxbuf_dma[idx] = buf_dma;

		usb_free_urb(urb);
	}

	if (idx == 0) {
		dev_err(self->dev, "Unable to setup any RX URBs\n");
	} else if (idx < GURU_MAX_URBS) {
		dev_warn(self->dev, "Unable to setup all RX URBs\n");
	}

	return err;
}

static void canguru_read_bulk_callback(struct urb *urb)
{
	struct guru_device *self = (struct guru_device *)urb->context;
	struct guru_data *msg;
	u32 pos = 0;
	int err;

	switch (urb->status) {
	case 0: /* success */
		break;
	case -ENOENT:
	case -EPIPE:
	case -EPROTO:
	case -ESHUTDOWN:
		return;

	default:
		dev_info(self->dev, "Rx URB aborted (%d)\n", urb->status);
		goto resubmit;
	}

	while ((urb->actual_length - pos) >= sizeof(struct guru_msg_header)) {
		msg = (struct guru_data *)((u8 *)urb->transfer_buffer + pos);
		canguru_process_msg_list(self, msg);
		pos += sizeof(struct guru_msg_header) + msg->header.data_count;
	}

resubmit:
	usb_fill_bulk_urb(urb, self->usbdev,
			  usb_rcvbulkpipe(self->usbdev, GURU_DATA_EP),
			  urb->transfer_buffer, sizeof(struct guru_data),
			  canguru_read_bulk_callback, self);

	usb_anchor_urb(urb, &self->rx_submitted);
	err = usb_submit_urb(urb, GFP_ATOMIC);

	if (err != 0) {
		usb_unanchor_urb(urb);
		dev_err(self->dev, "Failed resubmit read bulk: %d\n", err);
	}
}

static void canguru_write_bulk_callback(struct urb *urb)
{
	switch (urb->status) {
	case 0:
		break;
	case -ENOENT:
		usb_free_coherent(urb->dev, urb->transfer_buffer_length,
				  urb->transfer_buffer, urb->transfer_dma);
		break;
	}
}

static void canguru_process_msg_list(struct guru_device *self,
				     struct guru_data *msg)
{
	struct guru_msg *item;
	struct guru_msg *next;

	self->curr_msg_data = msg->data;
	self->curr_msg_data_size = msg->header.data_count;
	self->curr_msg_id = msg->header.id;
	list_for_each_entry_safe(item, next, &self->msg_list, node) {
		if (item->msgId != msg->header.id) {
			continue;
		}

		if (item->data_count == msg->header.data_count ||
		    item->size_restriction == GURU_SIZE_VARIABLE) {
			if (item->callback != NULL) {
				item->callback(item->user_data);
			}
		} else {
			dev_warn(self->dev, "Bad message length: %u\n",
				 item->data_count);
		}
	}
	self->curr_msg_data = NULL;
	self->curr_msg_data_size = 0;
	self->curr_msg_id = 0;
}
