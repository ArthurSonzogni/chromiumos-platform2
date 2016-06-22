// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_INPUT_WATCHER_STUB_H_
#define POWER_MANAGER_POWERD_SYSTEM_INPUT_WATCHER_STUB_H_

#include <base/observer_list.h>

#include "power_manager/powerd/system/input_watcher_interface.h"

namespace power_manager {
namespace system {

// Stub implementation of InputWatcherInterface for use by tests.
class InputWatcherStub : public InputWatcherInterface {
 public:
  InputWatcherStub();
  virtual ~InputWatcherStub();

  void set_lid_state(LidState state) { lid_state_ = state; }
  void set_tablet_mode(TabletMode tablet_mode) { tablet_mode_ = tablet_mode; }
  void set_usb_input_device_connected(bool connected) {
    usb_input_device_connected_ = connected;
  }
  void set_active_vt(int vt) { active_vt_ = vt; }

  // Notifies registered observers about various events.
  void NotifyObserversAboutLidState();
  void NotifyObserversAboutTabletMode();
  void NotifyObserversAboutPowerButtonEvent(ButtonState state);
  void NotifyObserversAboutHoverState(bool hovering);

  // InputWatcherInterface implementation:
  void AddObserver(InputObserver* observer) override;
  void RemoveObserver(InputObserver* observer) override;
  LidState QueryLidState() override;
  TabletMode GetTabletMode() override;
  bool IsUSBInputDeviceConnected() const override;
  int GetActiveVT() override;

 private:
  // Current input state.
  LidState lid_state_;
  TabletMode tablet_mode_;
  bool usb_input_device_connected_;
  int active_vt_;

  base::ObserverList<InputObserver> observers_;

  DISALLOW_COPY_AND_ASSIGN(InputWatcherStub);
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_INPUT_WATCHER_STUB_H_
