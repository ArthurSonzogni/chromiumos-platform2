// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_SUPPLICANT_SUPPLICANT_P2PDEVICE_EVENT_DELEGATE_INTERFACE_H_
#define SHILL_SUPPLICANT_SUPPLICANT_P2PDEVICE_EVENT_DELEGATE_INTERFACE_H_

#include <string>

namespace shill {

// SupplicantP2PDeviceEventDelegateInterface declares the set of methods
// that a SupplicantP2PdeviceProxy calls on an interested party when
// wpa_supplicant events occur on the p2p device interface.
class SupplicantP2PDeviceEventDelegateInterface {
 public:
  virtual ~SupplicantP2PDeviceEventDelegateInterface() = default;

  virtual void GroupStarted(const KeyValueStore& properties) = 0;
  virtual void GroupFinished(const KeyValueStore& properties) = 0;
  virtual void GroupFormationFailure(const std::string& reason) = 0;
};

}  // namespace shill

#endif  // SHILL_SUPPLICANT_SUPPLICANT_P2PDEVICE_EVENT_DELEGATE_INTERFACE_H_
