// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BLUETOOTH_DISPATCHER_DISPATCHER_H_
#define BLUETOOTH_DISPATCHER_DISPATCHER_H_

#include <map>
#include <memory>
#include <string>

#include <base/memory/ref_counted.h>
#include <base/memory/weak_ptr.h>
#include <brillo/dbus/dbus_object.h>
#include <brillo/dbus/exported_object_manager.h>
#include <brillo/dbus/exported_property_set.h>
#include <dbus/exported_object.h>
#include <dbus/message.h>
#include <dbus/object_manager.h>

#include "bluetooth/dispatcher/client_manager.h"
#include "bluetooth/dispatcher/impersonation_object_manager_interface.h"

namespace bluetooth {

// Normally the dispatcher task is to multiplex both BlueZ and NewBlue. This
// enum allows the dispatcher to be configured to passthrough the D-Bus traffic
// to/from BlueZ or NewBlue, acting as a pure proxy.
enum class PassthroughMode {
  // The normal BlueZ/NewBlue multiplexing. This is not yet supported and
  // fallbacks to BlueZ passthrough.
  MULTIPLEX = 0,
  // Pure D-Bus forwarding to/from BlueZ.
  BLUEZ_ONLY = 1,
  // Pure D-Bus forwarding to/from NewBlue.
  NEWBLUE_ONLY = 2,
};

// Exports BlueZ-compatible API and dispatches the requests to BlueZ or newblue.
class Dispatcher {
 public:
  explicit Dispatcher(scoped_refptr<dbus::Bus> bus);
  ~Dispatcher();

  // Initializes the daemon D-Bus operations.
  bool Init(PassthroughMode mode);

  // Frees up all resources, stopping all D-Bus operations.
  // Currently only needed in test.
  void Shutdown();

 private:
  // Forwards org.freedesktop.DBus.Properties.Set methods.
  void HandleForwardSetProperty(
      scoped_refptr<dbus::Bus> bus,
      dbus::MethodCall* method_call,
      dbus::ExportedObject::ResponseSender response_sender);

  // Registers our custom GetAll/Get/Set method handlers.
  void SetupPropertyMethodHandlers(
      brillo::dbus_utils::DBusInterface* prop_interface,
      brillo::dbus_utils::ExportedPropertySet* property_set);

  scoped_refptr<dbus::Bus> bus_;

  // The exported ObjectManager interface which is the impersonation of BlueZ's
  // ObjectManager.
  std::unique_ptr<ExportedObjectManagerWrapper>
      exported_object_manager_wrapper_;

  // Connects to the source object manager (BlueZ or NewBlue). Owned by |bus_|.
  dbus::ObjectManager* source_object_manager_;

  // Impersonates BlueZ's objects on various interfaces.
  std::map<std::string, std::unique_ptr<ImpersonationObjectManagerInterface>>
      impersonation_object_manager_interfaces_;

  std::unique_ptr<ClientManager> client_manager_;

  // Must come last so that weak pointers will be invalidated before other
  // members are destroyed.
  base::WeakPtrFactory<Dispatcher> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(Dispatcher);
};

}  // namespace bluetooth

#endif  // BLUETOOTH_DISPATCHER_DISPATCHER_H_
