// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/dbus_adaptor.h"

#include <memory>
#include <string>
#include <utility>

#include "base/memory/ptr_util.h"
#include "base/task/sequenced_task_runner.h"
#include "brillo/dbus/dbus_object.h"
#include "dbus/vm_concierge/dbus-constants.h"
#include "vm_tools/concierge/thread_utils.h"

namespace vm_tools::concierge {

namespace {

void OnOwned(std::unique_ptr<DbusAdaptor> adaptor,
             base::OnceCallback<void(std::unique_ptr<DbusAdaptor>)> on_created,
             const std::string& service_name,
             bool ownership_granted) {
  if (!ownership_granted) {
    LOG(ERROR) << "Failed to take ownership of " << kVmConciergeServiceName;
    adaptor.reset();
  }
  std::move(on_created).Run(std::move(adaptor));
}

void OnRegistered(
    scoped_refptr<dbus::Bus> bus,
    std::unique_ptr<DbusAdaptor> adaptor,
    base::OnceCallback<void(std::unique_ptr<DbusAdaptor>)> on_created,
    bool register_success) {
  if (!register_success) {
    LOG(ERROR) << "Failed to register: " << kVmConciergeServiceName;
    std::move(on_created).Run(nullptr);
    return;
  }
  bus->RequestOwnership(
      kVmConciergeServiceName, dbus::Bus::REQUIRE_PRIMARY,
      base::BindOnce(&OnOwned, std::move(adaptor), std::move(on_created)));
}

}  // namespace

// static
void DbusAdaptor::Create(
    scoped_refptr<dbus::Bus> bus,
    org::chromium::VmConciergeInterface* interface,
    base::OnceCallback<void(std::unique_ptr<DbusAdaptor>)> on_created) {
  std::unique_ptr<DbusAdaptor> adaptor =
      base::WrapUnique(new DbusAdaptor(bus, interface));
  brillo::dbus_utils::DBusObject* object = adaptor->dbus_object_.get();
  object->RegisterAsync(base::BindOnce(&OnRegistered, bus, std::move(adaptor),
                                       std::move(on_created)));
}

DbusAdaptor::~DbusAdaptor() {
  scoped_refptr<base::SequencedTaskRunner> dbus_runner =
      dbus_object_->GetBus()->GetDBusTaskRunner();
  PostTaskAndWait(
      dbus_runner,
      base::BindOnce(
          [](std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object) {
            dbus_object.reset();
          },
          std::move(dbus_object_)));
}

DbusAdaptor::DbusAdaptor(scoped_refptr<dbus::Bus> bus,
                         org::chromium::VmConciergeInterface* interface)
    : org::chromium::VmConciergeAdaptor(interface),
      dbus_object_(std::make_unique<brillo::dbus_utils::DBusObject>(
          nullptr, bus, dbus::ObjectPath(kVmConciergeServicePath))) {
  RegisterWithDBusObject(dbus_object_.get());
}

}  // namespace vm_tools::concierge
