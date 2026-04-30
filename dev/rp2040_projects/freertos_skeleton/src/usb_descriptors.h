#ifndef USB_DESCRIPTORS_H
#define USB_DESCRIPTORS_H

#include <stdbool.h>

void usb_descriptors_set_profile_normal(void);
void usb_descriptors_set_profile_msc_only(void);
bool usb_descriptors_profile_is_msc_only(void);

#endif
