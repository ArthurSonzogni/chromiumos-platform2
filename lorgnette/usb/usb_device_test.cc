// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/usb/usb_device.h"

#include <string.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "lorgnette/usb/usb_device_fake.h"

namespace lorgnette {

namespace {

libusb_device_descriptor MakeMinimalDeviceDescriptor() {
  libusb_device_descriptor descriptor;
  memset(&descriptor, 0, sizeof(descriptor));
  descriptor.bLength = sizeof(descriptor);
  descriptor.bDescriptorType = LIBUSB_DT_DEVICE;
  descriptor.idVendor = 0x1234;
  descriptor.idProduct = 0x4321;
  return descriptor;
}

TEST(UsbDeviceTest, ExpectedDescription) {
  UsbDeviceFake device;

  libusb_device_descriptor device_desc = MakeMinimalDeviceDescriptor();
  device_desc.idVendor = 0x5678;
  device_desc.idProduct = 0xfedc;
  device.SetDeviceDescriptor(device_desc);
  device.Init();

  EXPECT_EQ(device.Description(), "5678:fedc");
}

TEST(UsbDeviceTest, NoIppUsbInvalidDeviceDescriptor) {
  UsbDeviceFake device;
  EXPECT_FALSE(device.SupportsIppUsb());
}

TEST(UsbDeviceTest, NoIppUsbWrongDeviceClass) {
  UsbDeviceFake device;

  libusb_device_descriptor device_desc = MakeMinimalDeviceDescriptor();
  device_desc.bDeviceClass = LIBUSB_CLASS_HUB;
  device.SetDeviceDescriptor(device_desc);
  device.Init();

  EXPECT_FALSE(device.SupportsIppUsb());
}

TEST(UsbDeviceTest, NoIppUsbNoPrinterInterface) {
  UsbDeviceFake device;

  libusb_device_descriptor device_desc = MakeMinimalDeviceDescriptor();
  device_desc.bDeviceClass = LIBUSB_CLASS_PER_INTERFACE;
  device_desc.bNumConfigurations = 1;
  device.SetDeviceDescriptor(device_desc);
  device.Init();

  // One config with no interfaces.
  libusb_config_descriptor descriptor;
  memset(&descriptor, 0, sizeof(descriptor));
  descriptor.bLength = sizeof(descriptor);
  descriptor.bDescriptorType = LIBUSB_DT_CONFIG;
  descriptor.wTotalLength = sizeof(descriptor);
  device.SetConfigDescriptors({descriptor});

  EXPECT_FALSE(device.SupportsIppUsb());
}

TEST(UsbDeviceTest, PrinterWithoutIppUsb) {
  UsbDeviceFake device;

  libusb_device_descriptor device_desc = MakeMinimalDeviceDescriptor();
  device_desc.bDeviceClass = LIBUSB_CLASS_PER_INTERFACE;
  device_desc.bNumConfigurations = 1;
  device.SetDeviceDescriptor(device_desc);
  device.Init();

  // One altsetting with a printer class but not the IPP-USB protocol.
  auto altsetting = std::make_unique<libusb_interface_descriptor>();
  altsetting->bLength = sizeof(libusb_interface_descriptor);
  altsetting->bDescriptorType = LIBUSB_DT_INTERFACE;
  altsetting->bInterfaceNumber = 0;
  altsetting->bAlternateSetting = 1;
  altsetting->bInterfaceClass = LIBUSB_CLASS_PRINTER;

  // One interface containing the altsetting.
  auto interface = std::make_unique<libusb_interface>();
  interface->num_altsetting = 1;
  interface->altsetting = altsetting.get();

  // One config descriptor containing the interface.
  libusb_config_descriptor descriptor;
  memset(&descriptor, 0, sizeof(descriptor));
  descriptor.bLength = sizeof(descriptor);
  descriptor.bDescriptorType = LIBUSB_DT_CONFIG;
  descriptor.wTotalLength = sizeof(descriptor);
  descriptor.bNumInterfaces = 1;
  descriptor.interface = interface.get();

  device.SetConfigDescriptors({descriptor});

  EXPECT_FALSE(device.SupportsIppUsb());
}

TEST(UsbDeviceTest, PrinterWithIppUsb) {
  UsbDeviceFake device;

  libusb_device_descriptor device_desc = MakeMinimalDeviceDescriptor();
  device_desc.bDeviceClass = LIBUSB_CLASS_PER_INTERFACE;
  device_desc.bNumConfigurations = 1;
  device.SetDeviceDescriptor(device_desc);
  device.Init();

  // One altsetting with a printer class and the IPP-USB protocol.
  auto altsetting = std::make_unique<libusb_interface_descriptor>();
  altsetting->bLength = sizeof(libusb_interface_descriptor);
  altsetting->bDescriptorType = LIBUSB_DT_INTERFACE;
  altsetting->bInterfaceNumber = 0;
  altsetting->bAlternateSetting = 1;
  altsetting->bInterfaceClass = LIBUSB_CLASS_PRINTER;
  altsetting->bInterfaceProtocol = 0x04;  // IPP-USB protocol.

  // One interface containing the altsetting.
  auto interface = std::make_unique<libusb_interface>();
  interface->num_altsetting = 1;
  interface->altsetting = altsetting.get();

  // One config descriptor containing the interface.
  libusb_config_descriptor descriptor;
  memset(&descriptor, 0, sizeof(descriptor));
  descriptor.bLength = sizeof(descriptor);
  descriptor.bDescriptorType = LIBUSB_DT_CONFIG;
  descriptor.wTotalLength = sizeof(descriptor);
  descriptor.bNumInterfaces = 1;
  descriptor.interface = interface.get();

  device.SetConfigDescriptors({descriptor});

  EXPECT_TRUE(device.SupportsIppUsb());
}

}  // namespace

}  // namespace lorgnette
