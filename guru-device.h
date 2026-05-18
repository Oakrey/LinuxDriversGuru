#ifndef GURU_DEVICE_H
#define GURU_DEVICE_H

#include <linux/usb.h>

#include "guru-msg-std.h"

#define GURU_MAX_URBS 32
#define GURU_STR_LEN_MAX 32
#define GURU_DEV_MAX 16

struct guru_device {
	/* linux device structures */
	struct device *dev;
	struct usb_device *usbdev;
	struct usb_interface *intf;
	struct class *guru_class;

	void *rxbuf[GURU_MAX_URBS];
	dma_addr_t rxbuf_dma[GURU_MAX_URBS];

	struct usb_anchor tx_submitted;
	struct usb_anchor rx_submitted;

	/* canguru msg */
	u8 *curr_msg_data;
	u8 curr_msg_data_size;
	u16 curr_msg_id;
	struct list_head msg_list;
	struct guru_msg_std guru_msg_std;

	/* device */
	void *dev_data;
	void (*dev_deinit)(void *devData);
};

int guru_dev_init(struct guru_device *self, struct usb_interface *intf,
		  struct class *guru_class);
void guru_dev_deinit(struct guru_device *self);
/* The method send data to device over USB */
int guru_send_data(struct guru_device *self, uint16_t id, const uint8_t *data,
		   uint8_t data_len);

u8 *guru_get_msg_data(struct guru_device *self);
u8 guru_get_msg_data_size(struct guru_device *self);
u16 guru_get_msg_id(struct guru_device *self);

void guru_add_msg(struct guru_device *self, struct guru_msg *msg);

#endif // GURU_DEVICE_H
