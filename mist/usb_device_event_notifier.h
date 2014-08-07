// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MIST_USB_DEVICE_EVENT_NOTIFIER_H_
#define MIST_USB_DEVICE_EVENT_NOTIFIER_H_

#include <stdint.h>

#include <string>

#include <base/basictypes.h>
#include <base/compiler_specific.h>
#include <base/memory/scoped_ptr.h>
#include <base/message_loop/message_loop.h>
#include <base/observer_list.h>
#include <gtest/gtest_prod.h>

namespace mist {

class EventDispatcher;
class Udev;
class UdevDevice;
class UdevMonitor;
class UsbDeviceEventObserver;

// A USB device event notifier, which monitors udev events for USB devices and
// notifies registered observers that implement UsbDeviceEventObserver
// interface.
class UsbDeviceEventNotifier : public base::MessageLoopForIO::Watcher {
 public:
  // Constructs a UsbDeviceEventNotifier object by taking a raw pointer to an
  // EventDispatcher as |dispatcher| and a raw pointer to a Udev as |udev|. The
  // ownership of |dispatcher| and |udev| is not transferred, and thus they
  // should outlive this object.
  UsbDeviceEventNotifier(EventDispatcher* dispatcher, Udev* udev);

  virtual ~UsbDeviceEventNotifier();

  // Initializes USB device event monitoring such that this object can notify
  // registered observers upon USB device events. Returns true on success.
  bool Initialize();

  // Scans existing USB devices on the system and notify registered observers
  // of these devices via UsbDeviceEventObserver::OnUsbDeviceAdded().
  bool ScanExistingDevices();

  // Adds |observer| to the observer list such that |observer| will be notified
  // on USB device events.
  void AddObserver(UsbDeviceEventObserver* observer);

  // Removes |observer| from the observer list such that |observer| will no
  // longer be notified on USB device events.
  void RemoveObserver(UsbDeviceEventObserver* observer);

  // Implements base::MessageLoopForIO::Watcher.
  virtual void OnFileCanReadWithoutBlocking(int file_descriptor) OVERRIDE;
  virtual void OnFileCanWriteWithoutBlocking(int file_descriptor) OVERRIDE;

  // Gets the bus number, device address, vendor ID, and product ID of |device|.
  // Return true on success.
  static bool GetDeviceAttributes(const UdevDevice* device,
                                  uint8_t* bus_number,
                                  uint8_t* device_address,
                                  uint16_t* vendor_id,
                                  uint16_t* product_id);

 private:
  FRIEND_TEST(UsbDeviceEventNotifierTest, ConvertHexStringToUint16);
  FRIEND_TEST(UsbDeviceEventNotifierTest, ConvertNullToEmptyString);
  FRIEND_TEST(UsbDeviceEventNotifierTest, ConvertStringToUint8);
  FRIEND_TEST(UsbDeviceEventNotifierTest, OnUsbDeviceEventNotAddOrRemove);
  FRIEND_TEST(UsbDeviceEventNotifierTest, OnUsbDeviceEventWithInvalidBusNumber);
  FRIEND_TEST(UsbDeviceEventNotifierTest,
              OnUsbDeviceEventWithInvalidDeviceAddress);
  FRIEND_TEST(UsbDeviceEventNotifierTest, OnUsbDeviceEventWithInvalidProductId);
  FRIEND_TEST(UsbDeviceEventNotifierTest, OnUsbDeviceEventWithInvalidVendorId);
  FRIEND_TEST(UsbDeviceEventNotifierTest, OnUsbDeviceEvents);

  // Returns a string with value of |str| if |str| is not NULL, or an empty
  // string otherwise.
  static std::string ConvertNullToEmptyString(const char* str);

  // Converts a 4-digit hexadecimal ID string without the 0x prefix (e.g. USB
  // vendor/product ID) into an unsigned 16-bit value. Return true on success.
  static bool ConvertHexStringToUint16(const std::string& str, uint16_t* value);

  // Converts a decimal string, which denotes an integer between 0 and 255, into
  // an unsigned 8-bit integer. Return true on success.
  static bool ConvertStringToUint8(const std::string& str, uint8_t* value);

  EventDispatcher* const dispatcher_;
  ObserverList<UsbDeviceEventObserver> observer_list_;
  Udev* const udev_;
  scoped_ptr<UdevMonitor> udev_monitor_;
  int udev_monitor_file_descriptor_;

  DISALLOW_COPY_AND_ASSIGN(UsbDeviceEventNotifier);
};

}  // namespace mist

#endif  // MIST_USB_DEVICE_EVENT_NOTIFIER_H_
