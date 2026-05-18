#ifndef CANGURU_MSG_H
#define CANGURU_MSG_H

#include <linux/module.h>

enum guru_msg_size { GURU_SIZE_STATIC = 0, GURU_SIZE_VARIABLE };

enum guru_status {
	GURU_STATUS_DONE = 0, //!< IDP_STATUS_DONE
	GURU_STATUS_AWAIT_RESULT = 1, //!< IDP_STATUS_AWAIT_RESULT
	GURU_STATUS_ERROR = 2, //!< IDP_STATUS_ERROR
	GURU_STATUS_INVALID_ARG = 3, //!< IDP_STATUS_INVALID_ARG
	GURU_STATUS_BUSY = 4, //!< IDP_STATUS_BUSY
	GURU_STATUS_NOT_PERMITTED = 5, //!< IDP_STATUS_NOT_PERMITTED
	GURU_STATUS_NOT_IMPLEMETED = 6, //!< IDP_STATUS_NOT_IMPLEMETED
	GURU_STATUS_FALSE = 7 //!< IDP_STATUS_FALSE
};

struct guru_msg {
	u16 msgId;
	u8 data_count;
	enum guru_msg_size size_restriction;

	void *user_data;
	void (*callback)(void *user_data);
	struct list_head node;
};

struct guru_send_reply {
	void *user_data;
	void (*status_callback)(void *user_data, enum guru_status status);
	struct list_head node;
};

#endif // CANGURU_MSG_H
