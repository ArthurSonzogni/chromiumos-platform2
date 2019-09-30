// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BLUETOOTH_NEWBLUED_ADAPTER_INTERFACE_HANDLER_H_
#define BLUETOOTH_NEWBLUED_ADAPTER_INTERFACE_HANDLER_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/macros.h>
#include <base/memory/ref_counted.h>
#include <base/values.h>
#include <brillo/errors/error.h>
#include <dbus/bus.h>
#include <dbus/message.h>

#include "bluetooth/common/dbus_client.h"
#include "bluetooth/common/exported_object_manager_wrapper.h"
#include "bluetooth/newblued/device_interface_handler.h"
#include "bluetooth/newblued/newblue.h"
#include "bluetooth/newblued/scan_manager.h"

namespace bluetooth {

// Suspend and resume state. Corresponded BlueZ suspend/resume state names are
// SUS_RES_STATE_RUNNING, SUS_RES_STATE_SUS_IMMINT,
// SUS_RES_STATE_SUS_IMMINT_ACKED, and SUS_RES_STATE_SUS_DONE
enum SuspendResumeState : uint8_t {
  // System is running normally (awake)
  RUNNING,
  // Preparing for suspend upon suspend imminent signal sent by powerd
  SUSPEND_IMMINT,
  // Ack on suspend preparations sent to powerd
  SUSPEND_IMMINT_ACKED,
  // Resuming from suspend (notified by powerd)
  SUSPEND_DONE
};

// Lists the tasks that need to be done upon suspend and resume
enum SuspendResumeTask : uint8_t {
  NONE = (0),
  // Pause/unpause discovery
  PAUSE_UNPAUSE_DISCOVERY = (1 << 0),
};
// Handles org.bluez.Adapter1 interface.
class AdapterInterfaceHandler {
 public:
  // |newblue| and |exported_object_manager_wrapper| not owned, caller must make
  // sure it outlives this object.
  AdapterInterfaceHandler(
      scoped_refptr<dbus::Bus> bus,
      ExportedObjectManagerWrapper* exported_object_manager_wrapper);
  virtual ~AdapterInterfaceHandler() = default;

  // Starts exposing org.bluez.Adapter1 interface on object /org/bluez/hci0.
  // The properties of this object will be ignored by btdispatch, but the object
  // still has to be exposed to be able to receive org.bluez.Adapter1 method
  // calls, e.g. StartDiscovery(), StopDiscovery().
  void Init(DeviceInterfaceHandler* device_interface_handler, Newblue* newblue);

 private:
  // D-Bus method handlers for adapter object.
  bool HandleSetDiscoveryFilter(brillo::ErrorPtr* error,
                                dbus::Message* message,
                                const brillo::VariantDictionary& properties);
  bool HandleStartDiscovery(brillo::ErrorPtr* error, dbus::Message* message);
  bool HandleStopDiscovery(brillo::ErrorPtr* error, dbus::Message* message);
  bool HandleRemoveDevice(brillo::ErrorPtr* error,
                          dbus::Message* message,
                          const dbus::ObjectPath& device_path);

  // Called when a client is disconnected from D-Bus.
  void OnClientUnavailable(const std::string& client_address);

  // D-Bus method handlers for suspend/resume related methods
  void HandleSuspendImminent(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response,
      dbus::Message* message);
  void HandleSuspendDone(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response,
      dbus::Message* message);

  // Perform pause/unpause discovery action
  void PauseUnpauseDiscovery(void);
  // Update suspend/resume task status
  void UpdateSuspendResumeTasks(SuspendResumeTask task, bool is_completed);
  // Update suspend/resume state machine
  void UpdateSuspendResumeState(SuspendResumeState new_state);

  scoped_refptr<dbus::Bus> bus_;

  DeviceInterfaceHandler* device_interface_handler_;

  // All scanning-related requests must be delegated to |scan_manager_|.
  // TODO(b/140810173): |scan_manager_| should be owned by
  // AdapterInterfaceHandler instead of Adapter.
  std::unique_ptr<ScanManager> scan_manager_;

  ExportedObjectManagerWrapper* exported_object_manager_wrapper_;

  Newblue::DeviceDiscoveredCallback device_discovered_callback_;

  // Clients which currently have active discovery mapped by its D-Bus address.
  // (D-Bus address -> DBusClient object).
  std::map<std::string, std::unique_ptr<DBusClient>> discovery_clients_;

  bool is_in_suspension_ = false;

  // A bit map holding all suspend/resume related task status
  uint8_t suspend_resume_tasks_;

  SuspendResumeState suspend_resume_state_;

  std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> suspend_response_;

  // Must come last so that weak pointers will be invalidated before other
  // members are destroyed.
  base::WeakPtrFactory<AdapterInterfaceHandler> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(AdapterInterfaceHandler);
};

}  // namespace bluetooth

#endif  // BLUETOOTH_NEWBLUED_ADAPTER_INTERFACE_HANDLER_H_
