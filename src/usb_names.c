/*
 * USB Device Name Override
 * 
 * Makes the device appear as a generic USB keyboard
 * No suspicious identifiers - just looks like a standard keyboard
 */

#include <usb_names.h>

// Generic manufacturer name - appears as standard USB keyboard manufacturer
// Many generic keyboards show "USB" or blank - using "USB" is common
#define MANUFACTURER_NAME {'U','S','B'}
#define MANUFACTURER_NAME_LEN 3

// Generic product name - appears as standard USB keyboard
// Common names: "USB Keyboard", "Keyboard", "HID Keyboard Device"
#define PRODUCT_NAME {'U','S','B',' ','K','e','y','b','o','a','r','d'}
#define PRODUCT_NAME_LEN 12

// Override USB string descriptors
struct usb_string_descriptor_struct usb_string_manufacturer_name = {
    2 + MANUFACTURER_NAME_LEN * 2,
    3,
    MANUFACTURER_NAME
};

struct usb_string_descriptor_struct usb_string_product_name = {
    2 + PRODUCT_NAME_LEN * 2,
    3,
    PRODUCT_NAME
};
