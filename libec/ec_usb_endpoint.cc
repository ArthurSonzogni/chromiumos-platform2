// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/ec_usb_endpoint.h"

#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <libusb-1.0/libusb.h>
#include <string.h>

#include "libec/ec_command.h"

namespace ec {

int EcUsbEndpoint::FindInterfaceWithEndpoint(struct usb_endpoint* uep) {
  struct libusb_config_descriptor* conf;
  struct libusb_device* dev = libusb_->get_device(uep->dev_handle);
  int r = libusb_->get_active_config_descriptor(dev, &conf);
  if (r != LIBUSB_SUCCESS) {
    LOG(ERROR) << "get_active_config failed: " << libusb_error_name(r);
    return -1;
  }

  for (int i = 0; i < conf->bNumInterfaces; i++) {
    const struct libusb_interface* iface0 = &conf->interface[i];
    for (int j = 0; j < iface0->num_altsetting; j++) {
      const struct libusb_interface_descriptor* iface = &iface0->altsetting[j];
      for (int k = 0; k < iface->bNumEndpoints; k++) {
        const struct libusb_endpoint_descriptor* ep = &iface->endpoint[k];
        if (ep->bEndpointAddress == uep->address) {
          uep->chunk_len = ep->wMaxPacketSize;
          r = iface->bInterfaceNumber;
          libusb_->free_config_descriptor(conf);
          return r;
        }
      }
    }
  }

  libusb_->free_config_descriptor(conf);
  return -1;
}

libusb_device_handle* EcUsbEndpoint::CheckDevice(libusb_device* dev,
                                                 uint16_t vid,
                                                 uint16_t pid) {
  struct libusb_device_descriptor desc;
  int r = libusb_->get_device_descriptor(dev, &desc);
  if (r != LIBUSB_SUCCESS) {
    LOG(ERROR) << "libusb_get_device_descriptor failed: "
               << libusb_error_name(r);
    return nullptr;
  }

  libusb_device_handle* handle = nullptr;
  r = libusb_->open(dev, &handle);
  if (r != LIBUSB_SUCCESS) {
    LOG(ERROR) << "libusb_open failed: " << libusb_error_name(r);
    return nullptr;
  }

  if (vid != 0 && vid != desc.idVendor) {
    LOG(INFO) << "idVendor doesn't match: " << std::hex << desc.idVendor;
    libusb_->close(handle);
    return nullptr;
  }
  if (pid != 0 && pid != desc.idProduct) {
    LOG(INFO) << "idProduct doesn't match: " << std::hex << desc.idProduct;
    libusb_->close(handle);
    return nullptr;
  }

  return handle;
}

const struct usb_endpoint& EcUsbEndpoint::GetEndpointPtr() {
  return endpoint_;
}

bool EcUsbEndpoint::Init(uint16_t vid, uint16_t pid) {
  int r = libusb_->init(nullptr);
  if (r != LIBUSB_SUCCESS) {
    LOG(ERROR) << "libusb_init failed: " << libusb_error_name(r);
    return false;
  }
  libusb_is_init_ = true;

  libusb_device** devs;
  r = libusb_->get_device_list(nullptr, &devs);
  if (r < LIBUSB_SUCCESS) {
    LOG(WARNING) << "No device is found: " << libusb_error_name(r);
    return false;
  }

  libusb_device_handle* devh = nullptr;
  for (int i = 0; devs[i]; i++) {
    devh = CheckDevice(devs[i], vid, pid);
    if (devh) {
      LOG(INFO) << "Found device " << std::hex << vid << ":" << std::hex << pid;
      break;
    }
  }

  libusb_->free_device_list(devs, 1);

  if (!devh) {
    LOG(WARNING) << "Can't find device " << std::hex << vid << ":" << std::hex
                 << pid;
    return false;
  }

  endpoint_.dev_handle = devh;
  endpoint_.address = 2; /* USB_EP_HOSTCMD */

  int iface_num = FindInterfaceWithEndpoint(&endpoint_);
  if (iface_num < 0) {
    LOG(WARNING) << "USB HOSTCMD not supported by that device";
    return false;
  }

  if (!endpoint_.chunk_len) {
    LOG(ERROR) << "wMaxPacketSize isn't valid";
    return false;
  }

  endpoint_.interface_number = iface_num;

  LOG(INFO) << "Found interface=" << endpoint_.interface_number
            << base::StringPrintf(" endpoint=0x%02x", endpoint_.address)
            << " chunk_len=" << endpoint_.chunk_len;

  return true;
}

bool EcUsbEndpoint::ClaimInterface() {
  if (endpoint_.dev_handle == nullptr || endpoint_.interface_number == 0) {
    LOG(ERROR) << "Device handle or interface number are not set.";
    return false;
  }

  int r =
      libusb_claim_interface(endpoint_.dev_handle, endpoint_.interface_number);
  if (r != LIBUSB_SUCCESS) {
    LOG(ERROR) << "libusb_claim_interface failed: " << libusb_error_name(r);
    return false;
  }

  return true;
}

bool EcUsbEndpoint::ReleaseInterface() {
  if (endpoint_.dev_handle == nullptr || endpoint_.interface_number == 0) {
    LOG(ERROR) << "Device handle or interface number are not set.";
    return false;
  }

  int r = libusb_->release_interface(endpoint_.dev_handle,
                                     endpoint_.interface_number);
  if (r != LIBUSB_SUCCESS && r != LIBUSB_ERROR_NOT_FOUND) {
    LOG(ERROR) << "libusb_release_interface failed: " << libusb_error_name(r);
    return false;
  }

  return true;
}

EcUsbEndpoint::~EcUsbEndpoint() {
  LOG(INFO) << "Exiting libusb";

  if (!libusb_is_init_)
    return;

  ReleaseInterface();

  if (endpoint_.dev_handle)
    libusb_->close(endpoint_.dev_handle);

  libusb_->exit(nullptr);
}

}  // namespace ec
