#include "guru-msg-std.h"

#include "guru-device.h"

#define MSG_STR_SIZE_MAX 32

static void msg_std_fw_version(struct guru_msg_std *self);

/* CanGuru STD messages public methods implementation */

int guru_msg_std_init(struct guru_msg_std *self, struct guru_device *canguru)
{
	self->canguru = canguru;

	self->fw_version.size_restriction = GURU_SIZE_VARIABLE;
	self->fw_version.msgId = GURU_MSG_GET_VERSION;
	self->fw_version.user_data = self;
	self->fw_version.data_count = 0;
	self->fw_version.callback = (void (*)(void *))msg_std_fw_version;

	guru_add_msg(canguru, &self->fw_version);
	return 0;
}

int guru_get_fw_ver(struct guru_msg_std *self)
{
	return guru_send_data(self->canguru, GURU_MSG_GET_VERSION, NULL, 0);
}

/* CanGuru STD messages private methods implementation */

static void msg_std_fw_version(struct guru_msg_std *self)
{
	char fw_ver_buf[MSG_STR_SIZE_MAX] = { 0 };
	u8 *data = guru_get_msg_data(self->canguru);
	u8 data_size = guru_get_msg_data_size(self->canguru);

	memcpy(fw_ver_buf, data, data_size);

	dev_info(self->canguru->dev, "FW version %s\n", fw_ver_buf);
}
