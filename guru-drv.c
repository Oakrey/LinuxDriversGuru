#include "guru-device.h"

#include "canguru-hobby-device.h"
#include "canguru-lite-device.h"

#include <linux/version.h>

/* vendor and product id */
#define GURU_MODULE_NAME "oakrey-guru"
#define GURU_VENDOR_ID 0x1fc9
#define GURU_PRODUCT_ID 0x008A

#define CANGURU_LITE_PRODUCT_NAME "CAN Guru Lite"
#define CANGURU_HOBBY_PRODUCT_NAME "CAN Guru Hobby"

static struct class *guru_class;

static const struct usb_device_id guru_usb_table[] = {
	{ USB_DEVICE(GURU_VENDOR_ID, GURU_PRODUCT_ID) },
	{} /* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, guru_usb_table);

static inline void guru_usb_report(struct usb_device *usbdev)
{
	dev_info(&usbdev->dev,
		 "Starting %s %s (Serial Number %s) driver version %s\n",
		 usbdev->manufacturer, usbdev->product, usbdev->serial,
		 GURU_GIT_VERSION);
}

static inline int guru_usb_device_alloc(struct usb_interface *intf)
{
	int err = -ENODEV;
	void *guru_dev;
	struct usb_device *usbdev = interface_to_usbdev(intf);
	char buf[GURU_STR_LEN_MAX];

	if (usb_string(usbdev, usbdev->descriptor.iProduct, buf, sizeof(buf)) <=
	    0)
		return err;

	if (strcmp(buf, CANGURU_LITE_PRODUCT_NAME) == 0) {
		guru_usb_report(usbdev);
		guru_dev = devm_kzalloc(&intf->dev,
					sizeof(struct canguru_lite_device),
					GFP_KERNEL);
		if (guru_dev == NULL) {
			return -ENOMEM;
		}
		err = canguru_lite_dev_init(
			(struct canguru_lite_device *)guru_dev, intf,
			guru_class);
	} else if (strcmp(buf, CANGURU_HOBBY_PRODUCT_NAME) == 0) {
		guru_usb_report(usbdev);
		guru_dev = devm_kzalloc(&intf->dev,
					sizeof(struct canguru_hobby_device),
					GFP_KERNEL);
		if (guru_dev == NULL) {
			return -ENOMEM;
		}
		err = canguru_hobby_dev_init(
			(struct canguru_hobby_device *)guru_dev, intf,
			guru_class);
	} else {
		dev_info(&usbdev->dev, "Device ignored, not an Guru device\n");
	}

	return err;
}

static int guru_usb_probe(struct usb_interface *intf,
			  __maybe_unused const struct usb_device_id *id)
{
	int err = guru_usb_device_alloc(intf);

	return err;
}

static void guru_usb_disconnect(__maybe_unused struct usb_interface *intf)
{
	struct guru_device *guru_dev = usb_get_intfdata(intf);

	guru_dev_deinit(guru_dev);
}

static struct usb_driver guru_usb_driver = {
	.name = GURU_MODULE_NAME,
	.probe = guru_usb_probe,
	.disconnect = guru_usb_disconnect,
	.id_table = guru_usb_table,
};

static int __init guru_drv_init(void)
{
	int err;

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 4, 0)
	guru_class = class_create(THIS_MODULE, GURU_MODULE_NAME);
#else
	// Linux 6.4 change parameters of class_create
	guru_class = class_create(GURU_MODULE_NAME);
#endif
	if (IS_ERR(guru_class)) {
		pr_warn(GURU_MODULE_NAME
			": GURU class initialization failed\n");
		err = PTR_ERR(guru_class);
		return err;
	}

	err = usb_register(&guru_usb_driver);
	if (err != 0) {
		pr_warn(GURU_MODULE_NAME
			": USB module initialization failed\n");
		return err;
	}

	return 0;
}

static void __exit guru_drv_exit(void)
{
	usb_deregister(&guru_usb_driver);
	class_destroy(guru_class);
}

module_init(guru_drv_init);
module_exit(guru_drv_exit);

MODULE_AUTHOR("Michal Kona <kona@oakrey.cz>");
MODULE_DESCRIPTION("SocketCAN driver for Oakrey Guru devices");
MODULE_VERSION(GURU_GIT_VERSION);
MODULE_LICENSE("MIT");
