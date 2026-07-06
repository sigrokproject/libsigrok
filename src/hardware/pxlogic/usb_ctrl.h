#ifndef USB_CTRL_H
#define USB_CTRL_H
#include <stdio.h>
#include <string.h>
#include "libusb.h"
#include <libsigrok/libsigrok.h>
#include "../../libsigrok-internal.h"

/* Protocol commands */
#define CMD_CTL_RD              0xb0

struct ctl_data {
    uint64_t sync_cur_sample;
    uint32_t trig_out_validset;
    uint32_t real_pos;
};


//extern bool usb_busy ;
SR_PRIV int command_ctl_rddata(libusb_device_handle *usbdevh, struct ctl_data *data);
unsigned int usb_wr_reg(libusb_device_handle *usbdevh,unsigned int reg_addr,unsigned int reg_data);
unsigned int usb_rd_reg(libusb_device_handle *usbdevh,unsigned int reg_addr,unsigned int *reg_data);

unsigned int usb_wr_reg2(libusb_device_handle *usbdevh,unsigned int reg_addr,unsigned int reg_data);
unsigned int usb_rd_reg2(libusb_device_handle *usbdevh,unsigned int reg_addr,unsigned int *reg_data);

unsigned int usb_wr_data(libusb_device_handle *usbdevh,unsigned char *buff,int length,unsigned int timeout);
unsigned int usb_rd_data(libusb_device_handle *usbdevh,unsigned char *buff,int length,unsigned int timeout);
unsigned int usb_wr_data_update(libusb_device_handle *usbdevh,unsigned int base_addr,int length,unsigned int mode,unsigned char *buff,unsigned int timeout);
unsigned int usb_rd_data_update(libusb_device_handle *usbdevh,unsigned int base_addr,int length,unsigned int mode,unsigned char *buff,unsigned int timeout);

unsigned int usb_wr_data_req(libusb_device_handle *usbdevh,unsigned int base_addr,int length,unsigned int mode,unsigned char *buff,unsigned int timeout);
unsigned int usb_rd_data_req(libusb_device_handle *usbdevh,unsigned int base_addr,int length,unsigned int mode,unsigned char *buff,unsigned int timeout);


#endif // USB_CTRL_H
