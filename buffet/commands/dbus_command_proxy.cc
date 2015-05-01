// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffet/commands/dbus_command_proxy.h"

#include <chromeos/dbus/async_event_sequencer.h>
#include <chromeos/dbus/exported_object_manager.h>

#include "buffet/commands/command_definition.h"
#include "buffet/commands/command_instance.h"
#include "buffet/commands/object_schema.h"
#include "buffet/commands/prop_constraints.h"
#include "buffet/commands/prop_types.h"

using chromeos::dbus_utils::AsyncEventSequencer;
using chromeos::dbus_utils::ExportedObjectManager;

namespace buffet {

DBusCommandProxy::DBusCommandProxy(ExportedObjectManager* object_manager,
                                   const scoped_refptr<dbus::Bus>& bus,
                                   CommandInstance* command_instance,
                                   std::string object_path)
    : command_instance_{command_instance},
      dbus_object_{object_manager, bus, dbus::ObjectPath{object_path}} {
}

void DBusCommandProxy::RegisterAsync(
      const AsyncEventSequencer::CompletionAction& completion_callback) {
  dbus_adaptor_.RegisterWithDBusObject(&dbus_object_);

  // Set the initial property values before registering the DBus object.
  dbus_adaptor_.SetName(command_instance_->GetName());
  dbus_adaptor_.SetCategory(command_instance_->GetCategory());
  dbus_adaptor_.SetId(command_instance_->GetID());
  dbus_adaptor_.SetStatus(command_instance_->GetStatus());
  dbus_adaptor_.SetProgress(
      ObjectToDBusVariant(command_instance_->GetProgress()));
  dbus_adaptor_.SetOrigin(command_instance_->GetOrigin());

  dbus_adaptor_.SetParameters(ObjectToDBusVariant(
      command_instance_->GetParameters()));
  dbus_adaptor_.SetResults(ObjectToDBusVariant(
      command_instance_->GetResults()));

  // Register the command DBus object and expose its methods and properties.
  dbus_object_.RegisterAsync(completion_callback);
}

void DBusCommandProxy::OnResultsChanged() {
  dbus_adaptor_.SetResults(
      ObjectToDBusVariant(command_instance_->GetResults()));
}

void DBusCommandProxy::OnStatusChanged() {
  dbus_adaptor_.SetStatus(command_instance_->GetStatus());
}

void DBusCommandProxy::OnProgressChanged() {
  dbus_adaptor_.SetProgress(
      ObjectToDBusVariant(command_instance_->GetProgress()));
}

bool DBusCommandProxy::SetProgress(
    chromeos::ErrorPtr* error,
    const chromeos::VariantDictionary& progress) {
  LOG(INFO) << "Received call to Command<" << command_instance_->GetName()
            << ">::SetProgress()";

  auto progress_schema =
      command_instance_->GetCommandDefinition()->GetProgress();
  native_types::Object obj;
  if (!ObjectFromDBusVariant(progress_schema, progress, &obj, error))
    return false;

  command_instance_->SetProgress(obj);
  return true;
}

bool DBusCommandProxy::SetResults(chromeos::ErrorPtr* error,
                                  const chromeos::VariantDictionary& results) {
  LOG(INFO) << "Received call to Command<"
            << command_instance_->GetName() << ">::SetResults()";

  auto results_schema = command_instance_->GetCommandDefinition()->GetResults();
  native_types::Object obj;
  if (!ObjectFromDBusVariant(results_schema, results, &obj, error))
    return false;

  command_instance_->SetResults(obj);
  return true;
}

void DBusCommandProxy::Abort() {
  LOG(INFO) << "Received call to Command<"
            << command_instance_->GetName() << ">::Abort()";
  command_instance_->Abort();
}

void DBusCommandProxy::Cancel() {
  LOG(INFO) << "Received call to Command<"
            << command_instance_->GetName() << ">::Cancel()";
  command_instance_->Cancel();
}

void DBusCommandProxy::Done() {
  LOG(INFO) << "Received call to Command<"
            << command_instance_->GetName() << ">::Done()";
  command_instance_->Done();
}


}  // namespace buffet
