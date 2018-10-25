// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_UINPUT_DEVICE_H_
#define BIOD_UINPUT_DEVICE_H_

#include <base/files/file_util.h>
#include <base/macros.h>

namespace biod {

// A class to allow you to create uinput devices and generate events.
//
// This class can be used to create uinput device, to report user activity.
//  The general flow is to instantiate a UinputDevice object and call
// CreateUinputDevice(). You can then use SendEvent to report events on
// the input device created.
class UinputDevice {
 public:
  UinputDevice();
  ~UinputDevice();

  // Generate a new uinput device. Once this call is successful, SendEvent
  // can be used to post events to the input device.
  bool Init();

  // Once the device is finalized, this function sends the actual events
  // to the input subsystem just like a normal input device.
  bool SendEvent(int value) const;

 private:
  // Wrap up creation once all your events are enabled, and give it a name.
  // Once this is called the device is ready to start sending events out.
  bool FinalizeUinputCreation() const;

  base::ScopedFD uinput_fd_;

  DISALLOW_COPY_AND_ASSIGN(UinputDevice);
};

}  // namespace biod

#endif  // BIOD_UINPUT_DEVICE_H_
