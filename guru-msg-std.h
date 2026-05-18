#ifndef CANGURU_MSG_STD_H
#define CANGURU_MSG_STD_H

#include "guru-msg.h"

struct guru_device;

enum guru_msg_std_id {
	GURU_MSG_GET_VERSION = 0x1,
};

struct guru_msg_std {
	struct guru_device *canguru;

	struct guru_msg fw_version;
};

int guru_msg_std_init(struct guru_msg_std *self, struct guru_device *canguru);
int guru_get_fw_ver(struct guru_msg_std *self);

#endif // CANGURU_MSG_STD_H
