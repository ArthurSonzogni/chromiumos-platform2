// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUFFET_DBUS_COMMAND_DISPATCHER_H_
#define BUFFET_DBUS_COMMAND_DISPATCHER_H_

#include <map>
#include <string>

#include <base/macros.h>
#include <base/memory/weak_ptr.h>
#include <weave/commands.h>

namespace chromeos {
namespace dbus_utils {
class ExportedObjectManager;
}  // namespace dbus_utils
}  // namespace chromeos

namespace buffet {

// Implements D-Bus dispatch of commands. When OnCommandAdded is called,
// DBusCommandDispacher creates an instance of DBusCommandProxy object and
// advertises it through ExportedObjectManager on D-Bus. Command handling
// processes can watch the new D-Bus object appear and communicate with it to
// update the command handling progress. Once command is handled,
// DBusCommandProxy::Done() is called and the command is removed from the
// command queue and D-Bus ExportedObjectManager.
class DBusCommandDispacher final {
 public:
  explicit DBusCommandDispacher(
      const base::WeakPtr<chromeos::dbus_utils::ExportedObjectManager>&
          object_manager,
      weave::Commands* command_manager);

 private:
  void OnCommandAdded(weave::Command* command);

  base::WeakPtr<chromeos::dbus_utils::ExportedObjectManager> object_manager_;
  int next_id_{0};

  // Default constructor is used in special circumstances such as for testing.
  DBusCommandDispacher() = default;

  base::WeakPtrFactory<DBusCommandDispacher> weak_ptr_factory_{this};

  DISALLOW_COPY_AND_ASSIGN(DBusCommandDispacher);
};

}  // namespace buffet

#endif  // BUFFET_DBUS_COMMAND_DISPATCHER_H_
