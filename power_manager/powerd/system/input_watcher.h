// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_INPUT_WATCHER_H_
#define POWER_MANAGER_POWERD_SYSTEM_INPUT_WATCHER_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/cancelable_callback.h>
#include <base/files/file_path.h>
#include <base/macros.h>
#include <base/memory/linked_ptr.h>
#include <base/memory/weak_ptr.h>
#include <base/observer_list.h>

#include "power_manager/common/power_constants.h"
#include "power_manager/powerd/system/input_watcher_interface.h"
#include "power_manager/powerd/system/udev_subsystem_observer.h"

struct input_event;  // from <linux/input.h>

namespace power_manager {

class PrefsInterface;

namespace system {

class EventDeviceInterface;
class EventDeviceFactoryInterface;
class InputObserver;
struct UdevEvent;
class UdevInterface;
class WakeupDeviceFactoryInterface;
class WakeupDeviceInterface;

class InputWatcher : public InputWatcherInterface,
                     public UdevSubsystemObserver {
 public:
  // udev subsystem to watch for input device-related events.
  static const char kInputUdevSubsystem[];

  // Physical location (as returned by EVIOCGPHYS()) of power button devices
  // that should be skipped.
  //
  // Skip input events from the ACPI power button (identified as LNXPWRBN) if a
  // new power button is present on the keyboard.
  static const char kPowerButtonToSkip[];

  // Skip input events that are on the built-in keyboard if a legacy power
  // button is used. Many of these devices advertise a power button but do not
  // physically have one. Skipping will reduce the wasteful waking of powerd due
  // to keyboard events.
  static const char kPowerButtonToSkipForLegacy[];

  // Hardware ID for ACPI lid device.
  static const char kAcpiLidDevice[];

  InputWatcher();
  ~InputWatcher() override;

  void set_dev_input_path_for_testing(const base::FilePath& path) {
    dev_input_path_ = path;
  }
  void set_sys_class_input_path_for_testing(const base::FilePath& path) {
    sys_class_input_path_ = path;
  }
  // Leaves the InputWatcher in an unusable state, but useful for tests that
  // want to use the same event_device_factory_ across multiple InputWatchers.
  EventDeviceFactoryInterface* release_event_device_factory_for_testing() {
    return event_device_factory_.release();
  }
  // Leaves the InputWatcher in an unusable state, but useful for tests that
  // want to use the same wakeup_device_factory_ across multiple InputWatchers.
  WakeupDeviceFactoryInterface* release_wakeup_device_factory_for_testing() {
    return wakeup_device_factory_.release();
  }

  // Returns true on success.
  bool Init(std::unique_ptr<EventDeviceFactoryInterface> event_device_factory,
            std::unique_ptr<WakeupDeviceFactoryInterface> wakeup_device_factory,
            PrefsInterface* prefs,
            UdevInterface* udev);

  // InputWatcherInterface implementation:
  void AddObserver(InputObserver* observer) override;
  void RemoveObserver(InputObserver* observer) override;
  LidState QueryLidState() override;
  TabletMode GetTabletMode() override;
  bool IsUSBInputDeviceConnected() const override;
  void PrepareForSuspendRequest() override;
  void HandleResume() override;
  bool InputDeviceCausedLastWake() const override;

  // UdevSubsystemObserver implementation:
  void OnUdevEvent(const UdevEvent& event) override;

 private:
  // Different types of devices monitored by InputWatcher. It's possible for a
  // given device to fulfill more than one role.
  enum DeviceType {
    DEVICE_NONE = 0,
    DEVICE_POWER_BUTTON = 1 << 0,
    DEVICE_LID_SWITCH = 1 << 1,
    DEVICE_TABLET_MODE_SWITCH = 1 << 2,
    DEVICE_HOVER = 1 << 3,
  };

  // Returns a bitfield of DeviceType values describing |device|.
  uint32_t GetDeviceTypes(const EventDeviceInterface* device) const;

  // Flushes queued events and reads new events from |device|.
  void OnNewEvents(EventDeviceInterface* device);

  // Updates internal state and notifies observers in response to |event|.
  // |device_types| is a DeviceType bitfield describing the device from which
  // the event was read.
  void ProcessEvent(const input_event& event, uint32_t device_types);

  // Updates |current_multitouch_slot_| and |multitouch_slots_hover_state_| in
  // response to |event|, doing nothing if it isn't actually an MT event. Helper
  // method called by ProcessEvent().
  void ProcessHoverEvent(const input_event& event);

  // Handles an input being added to or removed from the system.
  // If |notify_state| is true, |observers| will be notified about the initial
  // state (if the input is watched). Notifications are not sent during
  // initialization, but they are sent later so that observers can learn about
  // the state of new devices.
  void HandleAddedInput(const std::string& input_name,
                        int input_num,
                        const base::FilePath& wakeup_device_path,
                        bool notify_state);

  void HandleRemovedInput(int input_num);

  // Monitors a device (with |wakeup_device_path|) to identify the potential
  // wakeup reason. Monitors only if the |wakeup_device_path| points to a
  // directory with power/wakeup property. Returns true if the device is
  // being monitored.
  // Example: /sys/devices/pci0000:00/0000:00:14.0/usb1/1-2/
  bool MonitorWakeupDevice(int input_num,
                           const base::FilePath& wakeup_device_path);

  // Calls NotifyObserversAboutEvent() for each event in |queued_events_| and
  // clears the vector.
  void SendQueuedEvents();

  // Notifies observers about |event| if came from a lid switch or power button.
  void NotifyObserversAboutEvent(const input_event& event);

  // Returns true if |device| should be monitored to identify user-driven wake
  // events.
  bool ShouldMonitorForWakeEvents(linked_ptr<EventDeviceInterface> device);

  base::FilePath dev_input_path_;
  base::FilePath sys_class_input_path_;

  // Factories for creating EventDevice and WakeupDevice objects.
  std::unique_ptr<EventDeviceFactoryInterface> event_device_factory_;
  std::unique_ptr<WakeupDeviceFactoryInterface> wakeup_device_factory_;

  // Event devices reporting power button events. Weak pointers to elements in
  // |event_devices_|.
  std::set<const EventDeviceInterface*> power_button_devices_;

  // The event device exposing the lid switch. Weak pointer to an element in
  // |event_devices_|, or null if no lid device was found.
  EventDeviceInterface* lid_device_ = nullptr;

  // The event device exposing the tablet mode switch. Weak pointer to an
  // element in |event_devices_|, or null if no tablet mode switch device was
  // found.
  EventDeviceInterface* tablet_mode_device_ = nullptr;

  // The event device reporting hover events. Weak pointer to an element in
  // |event_devices_|, or null if no hover device was found.
  EventDeviceInterface* hover_device_ = nullptr;

  // Should the lid be watched for events if present?
  bool use_lid_ = true;

  // Most-recently-seen lid state.
  LidState lid_state_ = LidState::OPEN;

  // Most-recently-seen tablet mode.
  TabletMode tablet_mode_ = TabletMode::UNSUPPORTED;

  // Should hover events be reported?
  bool detect_hover_ = false;

  // Most-recently-reported hover state.
  bool hovering_ = false;

  // Multitouch slot for which input events are currently being reported. See
  // https://www.kernel.org/doc/Documentation/input/multi-touch-protocol.txt for
  // more details about the protocol.
  int current_multitouch_slot_ = 0;

  // Bitfield containing the hover state of individual multitouch slots; if a
  // bit is true, it indicates that the corresponding slot is either reporting a
  // hover event above the touchpad or a touch event on the touchpad.
  uint64_t multitouch_slots_hover_state_ = 0;

  // Some touch devices only provide a binary hover value for the whole sensor
  // instead of the signals by finger.  These variables track that hover
  // state when it's not tied to a specific slot by using btn_tool_finger to
  // confirm that the abs_distance value is valid.
  bool single_touch_hover_valid_ = false;
  bool single_touch_hover_distance_nonzero_ = false;

  // (Events, DeviceType-bitfield) tuples read from |lid_device_| by
  // QueryLidState() that haven't yet been sent to observers.
  std::vector<std::pair<input_event, uint32_t>> queued_events_;

  // Posted by QueryLidState() to run SendQueuedEvents() to notify observers
  // about |queued_events_|.
  base::CancelableClosure send_queued_events_task_;

  // Name of the power button interface to skip monitoring for input events.
  std::string power_button_to_skip_;

  // ACPI lid device name.
  std::string acpi_lid_device_;

  UdevInterface* udev_ = nullptr;  // non-owned

  // Keyed by input event number.
  typedef std::map<int, linked_ptr<EventDeviceInterface>> InputMap;
  InputMap event_devices_;

  // Keyed by the input event number.
  using WakeupDeviceMap = std::map<int, std::unique_ptr<WakeupDeviceInterface>>;
  WakeupDeviceMap wakeup_devices_;

  base::ObserverList<InputObserver> observers_;

  // Used by IsUSBInputDeviceConnected() instead of the default path if
  // non-empty.
  base::FilePath sysfs_input_path_for_testing_;

  base::WeakPtrFactory<InputWatcher> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(InputWatcher);
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_INPUT_WATCHER_H_
