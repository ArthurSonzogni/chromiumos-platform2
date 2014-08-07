// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mist/usb_device_event_notifier.h"

#include <gtest/gtest.h>

#include "mist/event_dispatcher.h"
#include "mist/mock_udev.h"
#include "mist/mock_udev_device.h"
#include "mist/mock_udev_enumerate.h"
#include "mist/mock_udev_list_entry.h"
#include "mist/mock_udev_monitor.h"
#include "mist/mock_usb_device_event_observer.h"

using testing::InSequence;
using testing::Return;
using testing::StrEq;
using testing::_;

namespace mist {

namespace {

const int kFakeUdevMonitorFileDescriptor = 999;

const char kUdevActionAdd[] = "add";
const char kUdevActionChange[] = "change";
const char kUdevActionRemove[] = "remove";

const char kFakeUsbDevice1SysPath[] = "/sys/devices/fake/1";
const uint16_t kFakeUsbDevice1BusNumber = 1;
const char kFakeUsbDevice1BusNumberString[] = "1";
const uint16_t kFakeUsbDevice1DeviceAddress = 2;
const char kFakeUsbDevice1DeviceAddressString[] = "2";
const uint16_t kFakeUsbDevice1VendorId = 0x0123;
const char kFakeUsbDevice1VendorIdString[] = "0123";
const uint16_t kFakeUsbDevice1ProductId = 0x4567;
const char kFakeUsbDevice1ProductIdString[] = "4567";

const char kFakeUsbDevice2SysPath[] = "/sys/devices/fake/2";
const uint16_t kFakeUsbDevice2BusNumber = 3;
const char kFakeUsbDevice2BusNumberString[] = "3";
const uint16_t kFakeUsbDevice2DeviceAddress = 4;
const char kFakeUsbDevice2DeviceAddressString[] = "4";
const uint16_t kFakeUsbDevice2VendorId = 0x89ab;
const char kFakeUsbDevice2VendorIdString[] = "89ab";
const uint16_t kFakeUsbDevice2ProductId = 0xcdef;
const char kFakeUsbDevice2ProductIdString[] = "cdef";

}  // namespace

class UsbDeviceEventNotifierTest : public testing::Test {
 protected:
  UsbDeviceEventNotifierTest() : notifier_(&dispatcher_, &udev_) {}

  EventDispatcher dispatcher_;
  MockUdev udev_;
  MockUsbDeviceEventObserver observer_;
  UsbDeviceEventNotifier notifier_;
};

TEST_F(UsbDeviceEventNotifierTest, ConvertNullToEmptyString) {
  EXPECT_EQ("", UsbDeviceEventNotifier::ConvertNullToEmptyString(NULL));
  EXPECT_EQ("", UsbDeviceEventNotifier::ConvertNullToEmptyString(""));
  EXPECT_EQ("a", UsbDeviceEventNotifier::ConvertNullToEmptyString("a"));
  EXPECT_EQ("test string",
            UsbDeviceEventNotifier::ConvertNullToEmptyString("test string"));
}

TEST_F(UsbDeviceEventNotifierTest, ConvertHexStringToUint16) {
  uint16_t value = 0x0000;

  EXPECT_FALSE(UsbDeviceEventNotifier::ConvertHexStringToUint16("", &value));
  EXPECT_FALSE(UsbDeviceEventNotifier::ConvertHexStringToUint16("0", &value));
  EXPECT_FALSE(UsbDeviceEventNotifier::ConvertHexStringToUint16("00", &value));
  EXPECT_FALSE(UsbDeviceEventNotifier::ConvertHexStringToUint16("000", &value));
  EXPECT_FALSE(
      UsbDeviceEventNotifier::ConvertHexStringToUint16("00000", &value));
  EXPECT_FALSE(
      UsbDeviceEventNotifier::ConvertHexStringToUint16("000z", &value));

  EXPECT_TRUE(UsbDeviceEventNotifier::ConvertHexStringToUint16("abcd", &value));
  EXPECT_EQ(0xabcd, value);

  EXPECT_TRUE(UsbDeviceEventNotifier::ConvertHexStringToUint16("0000", &value));
  EXPECT_EQ(0x0000, value);

  EXPECT_TRUE(UsbDeviceEventNotifier::ConvertHexStringToUint16("ffff", &value));
  EXPECT_EQ(0xffff, value);
}

TEST_F(UsbDeviceEventNotifierTest, ConvertStringToUint8) {
  uint8_t value = 0;

  EXPECT_FALSE(UsbDeviceEventNotifier::ConvertStringToUint8("", &value));
  EXPECT_FALSE(UsbDeviceEventNotifier::ConvertStringToUint8("z", &value));
  EXPECT_FALSE(UsbDeviceEventNotifier::ConvertStringToUint8("-1", &value));
  EXPECT_FALSE(UsbDeviceEventNotifier::ConvertStringToUint8("256", &value));

  EXPECT_TRUE(UsbDeviceEventNotifier::ConvertStringToUint8("1", &value));
  EXPECT_EQ(1, value);

  EXPECT_TRUE(UsbDeviceEventNotifier::ConvertStringToUint8("0", &value));
  EXPECT_EQ(0, value);

  EXPECT_TRUE(UsbDeviceEventNotifier::ConvertStringToUint8("255", &value));
  EXPECT_EQ(255, value);
}

TEST_F(UsbDeviceEventNotifierTest, GetDeviceAttributes) {
  uint8_t bus_number;
  uint8_t device_address;
  uint16_t vendor_id;
  uint16_t product_id;

  // Invalid bus number.
  MockUdevDevice device1;
  EXPECT_CALL(device1, GetSysAttributeValue(_)).WillOnce(Return(""));
  EXPECT_FALSE(UsbDeviceEventNotifier::GetDeviceAttributes(
      &device1, &bus_number, &device_address, &vendor_id, &product_id));

  // Invalid device address.
  MockUdevDevice device2;
  EXPECT_CALL(device2, GetSysAttributeValue(_))
      .WillOnce(Return(kFakeUsbDevice1BusNumberString))
      .WillOnce(Return(""));
  EXPECT_FALSE(UsbDeviceEventNotifier::GetDeviceAttributes(
      &device2, &bus_number, &device_address, &vendor_id, &product_id));

  // Invalid vendor ID.
  MockUdevDevice device3;
  EXPECT_CALL(device3, GetSysAttributeValue(_))
      .WillOnce(Return(kFakeUsbDevice1BusNumberString))
      .WillOnce(Return(kFakeUsbDevice1DeviceAddressString))
      .WillOnce(Return(""));
  EXPECT_FALSE(UsbDeviceEventNotifier::GetDeviceAttributes(
      &device3, &bus_number, &device_address, &vendor_id, &product_id));

  // Invalid product ID.
  MockUdevDevice device4;
  EXPECT_CALL(device4, GetSysAttributeValue(_))
      .WillOnce(Return(kFakeUsbDevice1BusNumberString))
      .WillOnce(Return(kFakeUsbDevice1DeviceAddressString))
      .WillOnce(Return(kFakeUsbDevice1VendorIdString))
      .WillOnce(Return(""));
  EXPECT_FALSE(UsbDeviceEventNotifier::GetDeviceAttributes(
      &device4, &bus_number, &device_address, &vendor_id, &product_id));

  // Valid bus number, device address, vendor ID, and product ID.
  MockUdevDevice device5;
  EXPECT_CALL(device5, GetSysAttributeValue(_))
      .WillOnce(Return(kFakeUsbDevice1BusNumberString))
      .WillOnce(Return(kFakeUsbDevice1DeviceAddressString))
      .WillOnce(Return(kFakeUsbDevice1VendorIdString))
      .WillOnce(Return(kFakeUsbDevice1ProductIdString));
  EXPECT_TRUE(UsbDeviceEventNotifier::GetDeviceAttributes(
      &device5, &bus_number, &device_address, &vendor_id, &product_id));
  EXPECT_EQ(kFakeUsbDevice1BusNumber, bus_number);
  EXPECT_EQ(kFakeUsbDevice1DeviceAddress, device_address);
  EXPECT_EQ(kFakeUsbDevice1VendorId, vendor_id);
  EXPECT_EQ(kFakeUsbDevice1ProductId, product_id);
}

TEST_F(UsbDeviceEventNotifierTest, OnUsbDeviceEvents) {
  MockUdevMonitor* monitor = new MockUdevMonitor();
  // Ownership of |monitor| is transferred.
  notifier_.udev_monitor_.reset(monitor);

  MockUdevDevice* device1 = new MockUdevDevice();
  MockUdevDevice* device2 = new MockUdevDevice();
  MockUdevDevice* device3 = new MockUdevDevice();
  MockUdevDevice* device4 = new MockUdevDevice();
  // Ownership of |device*| is transferred.
  EXPECT_CALL(*monitor, ReceiveDevice())
      .WillOnce(Return(device1))
      .WillOnce(Return(device2))
      .WillOnce(Return(device3))
      .WillOnce(Return(device4));

  EXPECT_CALL(*device1, GetSysPath()).WillOnce(Return(kFakeUsbDevice1SysPath));
  EXPECT_CALL(*device1, GetAction()).WillOnce(Return(kUdevActionAdd));

  EXPECT_CALL(*device2, GetSysPath()).WillOnce(Return(kFakeUsbDevice2SysPath));
  EXPECT_CALL(*device2, GetAction()).WillOnce(Return(kUdevActionAdd));
  EXPECT_CALL(*device2, GetSysAttributeValue(_))
      .WillOnce(Return(kFakeUsbDevice2BusNumberString))
      .WillOnce(Return(kFakeUsbDevice2DeviceAddressString))
      .WillOnce(Return(kFakeUsbDevice2VendorIdString))
      .WillOnce(Return(kFakeUsbDevice2ProductIdString));

  EXPECT_CALL(*device3, GetSysPath()).WillOnce(Return(kFakeUsbDevice1SysPath));
  EXPECT_CALL(*device3, GetAction()).WillOnce(Return(kUdevActionRemove));

  EXPECT_CALL(*device4, GetSysPath()).WillOnce(Return(kFakeUsbDevice2SysPath));
  EXPECT_CALL(*device4, GetAction()).WillOnce(Return(kUdevActionRemove));

  EXPECT_CALL(observer_,
              OnUsbDeviceAdded(kFakeUsbDevice2SysPath,
                               kFakeUsbDevice2BusNumber,
                               kFakeUsbDevice2DeviceAddress,
                               kFakeUsbDevice2VendorId,
                               kFakeUsbDevice2ProductId));
  EXPECT_CALL(observer_, OnUsbDeviceRemoved(kFakeUsbDevice1SysPath));

  notifier_.OnFileCanReadWithoutBlocking(kFakeUdevMonitorFileDescriptor);
  notifier_.AddObserver(&observer_);
  notifier_.OnFileCanReadWithoutBlocking(kFakeUdevMonitorFileDescriptor);
  notifier_.OnFileCanReadWithoutBlocking(kFakeUdevMonitorFileDescriptor);
  notifier_.RemoveObserver(&observer_);
  notifier_.OnFileCanReadWithoutBlocking(kFakeUdevMonitorFileDescriptor);
}

TEST_F(UsbDeviceEventNotifierTest, OnUsbDeviceEventNotAddOrRemove) {
  MockUdevMonitor* monitor = new MockUdevMonitor();
  // Ownership of |monitor| is transferred.
  notifier_.udev_monitor_.reset(monitor);

  MockUdevDevice* device = new MockUdevDevice();
  // Ownership of |device| is transferred.
  EXPECT_CALL(*monitor, ReceiveDevice()).WillOnce(Return(device));

  EXPECT_CALL(*device, GetSysPath()).WillOnce(Return(kFakeUsbDevice1SysPath));
  EXPECT_CALL(*device, GetAction()).WillOnce(Return(kUdevActionChange));

  EXPECT_CALL(observer_, OnUsbDeviceAdded(_, _, _, _, _)).Times(0);
  EXPECT_CALL(observer_, OnUsbDeviceRemoved(_)).Times(0);

  notifier_.AddObserver(&observer_);
  notifier_.OnFileCanReadWithoutBlocking(kFakeUdevMonitorFileDescriptor);
}

TEST_F(UsbDeviceEventNotifierTest, OnUsbDeviceEventWithInvalidBusNumber) {
  MockUdevMonitor* monitor = new MockUdevMonitor();
  // Ownership of |monitor| is transferred.
  notifier_.udev_monitor_.reset(monitor);

  MockUdevDevice* device = new MockUdevDevice();
  // Ownership of |device| is transferred.
  EXPECT_CALL(*monitor, ReceiveDevice()).WillOnce(Return(device));

  EXPECT_CALL(*device, GetSysPath()).WillOnce(Return(kFakeUsbDevice1SysPath));
  EXPECT_CALL(*device, GetAction()).WillOnce(Return(kUdevActionAdd));
  EXPECT_CALL(*device, GetSysAttributeValue(_)).WillOnce(Return(""));

  EXPECT_CALL(observer_, OnUsbDeviceAdded(_, _, _, _, _)).Times(0);
  EXPECT_CALL(observer_, OnUsbDeviceRemoved(_)).Times(0);

  notifier_.AddObserver(&observer_);
  notifier_.OnFileCanReadWithoutBlocking(kFakeUdevMonitorFileDescriptor);
}

TEST_F(UsbDeviceEventNotifierTest, OnUsbDeviceEventWithInvalidDeviceAddress) {
  MockUdevMonitor* monitor = new MockUdevMonitor();
  // Ownership of |monitor| is transferred.
  notifier_.udev_monitor_.reset(monitor);

  MockUdevDevice* device = new MockUdevDevice();
  // Ownership of |device| is transferred.
  EXPECT_CALL(*monitor, ReceiveDevice()).WillOnce(Return(device));

  EXPECT_CALL(*device, GetSysPath()).WillOnce(Return(kFakeUsbDevice1SysPath));
  EXPECT_CALL(*device, GetAction()).WillOnce(Return(kUdevActionAdd));
  EXPECT_CALL(*device, GetSysAttributeValue(_))
      .WillOnce(Return(kFakeUsbDevice1BusNumberString))
      .WillOnce(Return(""));

  EXPECT_CALL(observer_, OnUsbDeviceAdded(_, _, _, _, _)).Times(0);
  EXPECT_CALL(observer_, OnUsbDeviceRemoved(_)).Times(0);

  notifier_.AddObserver(&observer_);
  notifier_.OnFileCanReadWithoutBlocking(kFakeUdevMonitorFileDescriptor);
}

TEST_F(UsbDeviceEventNotifierTest, OnUsbDeviceEventWithInvalidVendorId) {
  MockUdevMonitor* monitor = new MockUdevMonitor();
  // Ownership of |monitor| is transferred.
  notifier_.udev_monitor_.reset(monitor);

  MockUdevDevice* device = new MockUdevDevice();
  // Ownership of |device| is transferred.
  EXPECT_CALL(*monitor, ReceiveDevice()).WillOnce(Return(device));

  EXPECT_CALL(*device, GetSysPath()).WillOnce(Return(kFakeUsbDevice1SysPath));
  EXPECT_CALL(*device, GetAction()).WillOnce(Return(kUdevActionAdd));
  EXPECT_CALL(*device, GetSysAttributeValue(_))
      .WillOnce(Return(kFakeUsbDevice1BusNumberString))
      .WillOnce(Return(kFakeUsbDevice1DeviceAddressString))
      .WillOnce(Return(""));

  EXPECT_CALL(observer_, OnUsbDeviceAdded(_, _, _, _, _)).Times(0);
  EXPECT_CALL(observer_, OnUsbDeviceRemoved(_)).Times(0);

  notifier_.AddObserver(&observer_);
  notifier_.OnFileCanReadWithoutBlocking(kFakeUdevMonitorFileDescriptor);
}

TEST_F(UsbDeviceEventNotifierTest, OnUsbDeviceEventWithInvalidProductId) {
  MockUdevMonitor* monitor = new MockUdevMonitor();
  // Ownership of |monitor| is transferred.
  notifier_.udev_monitor_.reset(monitor);

  MockUdevDevice* device = new MockUdevDevice();
  // Ownership of |device| is transferred.
  EXPECT_CALL(*monitor, ReceiveDevice()).WillOnce(Return(device));

  EXPECT_CALL(*device, GetSysPath()).WillOnce(Return(kFakeUsbDevice1SysPath));
  EXPECT_CALL(*device, GetAction()).WillOnce(Return(kUdevActionAdd));
  EXPECT_CALL(*device, GetSysAttributeValue(_))
      .WillOnce(Return(kFakeUsbDevice1BusNumberString))
      .WillOnce(Return(kFakeUsbDevice1DeviceAddressString))
      .WillOnce(Return(kFakeUsbDevice1VendorIdString))
      .WillOnce(Return(""));

  EXPECT_CALL(observer_, OnUsbDeviceAdded(_, _, _, _, _)).Times(0);
  EXPECT_CALL(observer_, OnUsbDeviceRemoved(_)).Times(0);

  notifier_.AddObserver(&observer_);
  notifier_.OnFileCanReadWithoutBlocking(kFakeUdevMonitorFileDescriptor);
}

TEST_F(UsbDeviceEventNotifierTest, ScanExistingDevices) {
  // Ownership of |enumerate|, |list_entry*|, and |device*| is transferred
  // to and managed inside ScanExistingDevices().
  MockUdevEnumerate* enumerate = new MockUdevEnumerate();
  MockUdevListEntry* list_entry1 = new MockUdevListEntry();
  MockUdevListEntry* list_entry2 = new MockUdevListEntry();
  MockUdevDevice* device1 = new MockUdevDevice();
  MockUdevDevice* device2 = new MockUdevDevice();

  EXPECT_CALL(udev_, CreateEnumerate()).WillOnce(Return(enumerate));
  EXPECT_CALL(*enumerate,
              AddMatchSubsystem(StrEq("usb"))).WillOnce(Return(true));
  EXPECT_CALL(*enumerate,
              AddMatchProperty(StrEq("DEVTYPE"), StrEq("usb_device")))
      .WillOnce(Return(true));
  EXPECT_CALL(*enumerate, ScanDevices()).WillOnce(Return(true));
  EXPECT_CALL(*enumerate, GetListEntry()).WillOnce(Return(list_entry1));
  EXPECT_CALL(*list_entry1, GetName()).WillOnce(Return(kFakeUsbDevice1SysPath));
  EXPECT_CALL(*list_entry1, GetNext()).WillOnce(Return(list_entry2));
  EXPECT_CALL(*list_entry2, GetName()).WillOnce(Return(kFakeUsbDevice2SysPath));
  EXPECT_CALL(*list_entry2, GetNext())
      .WillOnce(Return(static_cast<UdevListEntry*>(NULL)));
  EXPECT_CALL(udev_, CreateDeviceFromSysPath(_))
      .WillOnce(Return(device1))
      .WillOnce(Return(device2));
  EXPECT_CALL(*device1, GetSysAttributeValue(_))
      .WillOnce(Return(kFakeUsbDevice1BusNumberString))
      .WillOnce(Return(kFakeUsbDevice1DeviceAddressString))
      .WillOnce(Return(kFakeUsbDevice1VendorIdString))
      .WillOnce(Return(kFakeUsbDevice1ProductIdString));
  EXPECT_CALL(*device2, GetSysAttributeValue(_))
      .WillOnce(Return(kFakeUsbDevice2BusNumberString))
      .WillOnce(Return(kFakeUsbDevice2DeviceAddressString))
      .WillOnce(Return(kFakeUsbDevice2VendorIdString))
      .WillOnce(Return(kFakeUsbDevice2ProductIdString));

  InSequence sequence;
  EXPECT_CALL(observer_,
              OnUsbDeviceAdded(kFakeUsbDevice1SysPath,
                               kFakeUsbDevice1BusNumber,
                               kFakeUsbDevice1DeviceAddress,
                               kFakeUsbDevice1VendorId,
                               kFakeUsbDevice1ProductId));
  EXPECT_CALL(observer_,
              OnUsbDeviceAdded(kFakeUsbDevice2SysPath,
                               kFakeUsbDevice2BusNumber,
                               kFakeUsbDevice2DeviceAddress,
                               kFakeUsbDevice2VendorId,
                               kFakeUsbDevice2ProductId));
  EXPECT_CALL(observer_, OnUsbDeviceRemoved(_)).Times(0);

  notifier_.AddObserver(&observer_);
  notifier_.ScanExistingDevices();
}

}  // namespace mist
