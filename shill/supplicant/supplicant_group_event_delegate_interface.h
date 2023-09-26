// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_SUPPLICANT_SUPPLICANT_GROUP_EVENT_DELEGATE_INTERFACE_H_
#define SHILL_SUPPLICANT_SUPPLICANT_GROUP_EVENT_DELEGATE_INTERFACE_H_

namespace shill {

// SupplicantGroupEventDelegateInterface declares the set of methods
// that a SupplicantGroupProxy calls on an interested party when
// wpa_supplicant events occur on the group interface.
class SupplicantGroupEventDelegateInterface {
 public:
  virtual ~SupplicantGroupEventDelegateInterface() = default;

  virtual void PeerJoined(const dbus::ObjectPath& peer) = 0;
  virtual void PeerDisconnected(const dbus::ObjectPath& peer) = 0;
};

}  // namespace shill

#endif  // SHILL_SUPPLICANT_SUPPLICANT_GROUP_EVENT_DELEGATE_INTERFACE_H_
