// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_DISKS_CROS_DISKS_SERVER_H_
#define CROS_DISKS_CROS_DISKS_SERVER_H_

#include <map>
#include <string>
#include <tuple>
#include <vector>

#include <base/memory/ref_counted.h>
#include <brillo/dbus/dbus_object.h>
#include <dbus/bus.h>

#include "cros-disks/dbus_adaptors/org.chromium.CrosDisks.h"
#include "cros-disks/device_event_dispatcher_interface.h"
#include "cros-disks/device_event_queue.h"
#include "cros-disks/disk.h"
#include "cros-disks/format_manager_observer_interface.h"
#include "cros-disks/mount_entry.h"
#include "cros-disks/rename_manager_observer_interface.h"
#include "cros-disks/session_manager_observer_interface.h"

namespace cros_disks {

class DiskManager;
class FormatManager;
class FUSEMountManager;
class MountManager;
class Platform;
class RenameManager;

struct DeviceEvent;

class CrosDisksServer : public org::chromium::CrosDisksAdaptor,
                        public org::chromium::CrosDisksInterface,
                        public DeviceEventDispatcherInterface,
                        public FormatManagerObserverInterface,
                        public SessionManagerObserverInterface,
                        public RenameManagerObserverInterface {
 public:
  CrosDisksServer(scoped_refptr<dbus::Bus> bus,
                  Platform* platform,
                  DiskManager* disk_manager,
                  FormatManager* format_manager,
                  RenameManager* rename_manager);
  ~CrosDisksServer() override = default;

  // Registers the D-Bus object and interfaces.
  void RegisterAsync(
      const brillo::dbus_utils::AsyncEventSequencer::CompletionAction& cb);

  // Registers a mount manager.
  void RegisterMountManager(MountManager* mount_manager);

  // Implementation of org::chromium::CrosDisks:

  // A method for formatting a device specified by |path|.
  // On completion, a FormatCompleted signal is emitted to indicate whether
  // the operation succeeded or failed using a FormatErrorType enum value.
  void Format(const std::string& path,
              const std::string& filesystem_type,
              const std::vector<std::string>& options) override;

  // A method for renaming a device specified by |path|.
  // On completion, a RenameCompleted signal is emitted to indicate whether
  // the operation succeeded or failed using a RenameErrorType enum value.
  void Rename(const std::string& path, const std::string& volume_name) override;

  // Mounts a source when invoked.
  void Mount(const std::string& source,
             const std::string& filesystem_type,
             const std::vector<std::string>& options) override;

  // Unmounts a path when invoked.
  uint32_t Unmount(const std::string& path,
                   const std::vector<std::string>& options) override;

  // Unmounts all paths mounted by Mount() when invoked.
  void UnmountAll() override;

  // Returns a list of device sysfs paths for all disk devices attached to
  // the system.
  std::vector<std::string> EnumerateDevices() override;

  // Returns a list of mount entries (<error type, source path, source type,
  // mount path>) that are currently managed by cros-disks.
  using DBusMountEntry =
      std::tuple<uint32_t, std::string, uint32_t, std::string>;
  std::vector<DBusMountEntry> EnumerateMountEntries() override;

  // Returns properties of a disk device attached to the system.
  bool GetDeviceProperties(brillo::ErrorPtr* error,
                           const std::string& device_path,
                           brillo::VariantDictionary* properties) override;

  // Implements the FormatManagerObserverInterface interface to handle
  // the event when a formatting operation has completed.
  void OnFormatCompleted(const std::string& device_path,
                         FormatErrorType error_type) override;

  // Implements the RenameManagerObserverInterface interface to handle
  // the event when a renaming operation has completed.
  void OnRenameCompleted(const std::string& device_path,
                         RenameErrorType error_type) override;

  // Implements the SessionManagerObserverInterface interface to handle
  // the event when the screen is locked.
  void OnScreenIsLocked() override;

  // Implements the SessionManagerObserverInterface interface to handle
  // the event when the screen is unlocked.
  void OnScreenIsUnlocked() override;

  // Implements the SessionManagerObserverInterface interface to handle
  // the event when the session has been started.
  void OnSessionStarted() override;

  // Implements the SessionManagerObserverInterface interface to handle
  // the event when the session has been stopped.
  void OnSessionStopped() override;

 private:
  // Implements the DeviceEventDispatcherInterface to dispatch a device event
  // by emitting the corresponding D-Bus signal.
  void DispatchDeviceEvent(const DeviceEvent& event) override;

  // Finds and returns a mounter which can mount |source_path|, or nullptr if no
  // one can.
  MountManager* FindMounter(const std::string& source_path) const;

  brillo::dbus_utils::DBusObject dbus_object_;
  Platform* platform_;
  DiskManager* disk_manager_;
  FormatManager* format_manager_;
  RenameManager* rename_manager_;
  std::vector<MountManager*> mount_managers_;
};

}  // namespace cros_disks

#endif  // CROS_DISKS_CROS_DISKS_SERVER_H_
