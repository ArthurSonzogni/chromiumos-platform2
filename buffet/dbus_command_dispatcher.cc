// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffet/dbus_command_dispatcher.h"

#include <chromeos/dbus/exported_object_manager.h>
#include <weave/command.h>
#include <weave/device.h>

#include "buffet/dbus_command_proxy.h"
#include "buffet/dbus_constants.h"

using chromeos::dbus_utils::AsyncEventSequencer;
using chromeos::dbus_utils::ExportedObjectManager;

namespace buffet {

DBusCommandDispacher::DBusCommandDispacher(
    const base::WeakPtr<ExportedObjectManager>& object_manager,
    weave::Device* device)
    : object_manager_{object_manager} {
  device->AddCommandHandler("",
                            base::Bind(&DBusCommandDispacher::OnCommandAdded,
                                       weak_ptr_factory_.GetWeakPtr()));
}

void DBusCommandDispacher::OnCommandAdded(
    const std::weak_ptr<weave::Command>& cmd) {
  auto command = cmd.lock();
  if (!object_manager_ || !command)
    return;
  std::unique_ptr<DBusCommandProxy> proxy{new DBusCommandProxy(
      object_manager_.get(), object_manager_->GetBus(), command,
      buffet::dbus_constants::kCommandServicePathPrefix +
          std::to_string(++next_id_))};
  proxy->RegisterAsync(AsyncEventSequencer::GetDefaultCompletionAction());
  // DBusCommandProxy::DBusCommandProxy() subscribe itself to weave::Command
  // notifications. When weave::Command is being destroyed it sends
  // ::OnCommandDestroyed() and DBusCommandProxy deletes itself.
  proxy.release();
}

}  // namespace buffet
