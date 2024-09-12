// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dbus_perfetto_producer/dbus_request.h"

#include <fstream>
#include <utility>

#include <base/check.h>
#include <base/logging.h>
#include <dbus/dbus.h>

namespace {
// Send a D-Bus message (method call) and wait for a reply (method return)
DBusMessage* SendMessage(DBusConnection* connection,
                         DBusMessage* message,
                         DBusError* error) {
  DBusMessage* reply;
  reply =
      dbus_connection_send_with_reply_and_block(connection, message, -1, error);
  if (dbus_error_is_set(error)) {
    LOG(ERROR) << "Failed to get a reply: " << error->name << " "
               << error->message;
    dbus_error_free(error);
    return nullptr;
  }
  if (!reply) {
    LOG(ERROR) << "Failed to get a reply";
    return nullptr;
  }
  dbus_message_unref(message);
  return reply;
}

// dbus_name needs to be D-Bus well-known name
const char* GetUniqueName(DBusConnection* connection,
                          DBusError* error,
                          const char* dbus_name) {
  DBusMessage* message;
  DBusMessageIter appender;
  message = dbus_message_new_method_call(DBUS_SERVICE_DBUS, DBUS_PATH_DBUS,
                                         DBUS_INTERFACE_DBUS, "GetNameOwner");
  if (!message) {
    LOG(ERROR) << "Failed to create a message";
    return nullptr;
  }

  dbus_message_iter_init_append(message, &appender);
  if (!dbus_message_iter_append_basic(&appender, DBUS_TYPE_STRING,
                                      &dbus_name)) {
    LOG(ERROR) << "Failed to add an argument";
    return nullptr;
  }

  DBusMessage* reply;
  DBusMessageIter iter;
  const char* unique_name;
  reply = SendMessage(connection, message, error);
  if (!reply) {
    return nullptr;
  }
  dbus_message_iter_init(reply, &iter);
  dbus_message_iter_get_basic(&iter, &unique_name);
  dbus_message_unref(reply);

  return unique_name;
}

uint32_t GetPid(DBusConnection* connection,
                DBusError* error,
                const char* dbus_name) {
  DBusMessage* message;
  DBusMessageIter appender;
  message = dbus_message_new_method_call(DBUS_SERVICE_DBUS, DBUS_PATH_DBUS,
                                         DBUS_INTERFACE_DBUS,
                                         "GetConnectionUnixProcessID");
  if (!message) {
    LOG(ERROR) << "Failed to create a message";
    return 0;
  }

  dbus_message_iter_init_append(message, &appender);
  if (!dbus_message_iter_append_basic(&appender, DBUS_TYPE_STRING,
                                      &dbus_name)) {
    LOG(ERROR) << "Failed to add an argument";
    return 0;
  }

  DBusMessage* reply;
  DBusMessageIter iter;
  uint32_t pid;
  reply = SendMessage(connection, message, error);
  if (!reply) {
    return 0;
  }
  dbus_message_iter_init(reply, &iter);
  dbus_message_iter_get_basic(&iter, &pid);
  dbus_message_unref(reply);

  return pid;
}

static std::string GetProcessName(uint32_t pid) {
  std::string process_name;
  std::ifstream comm_file("/proc/" + std::to_string(pid) + "/comm");
  if (comm_file.is_open()) {
    getline(comm_file, process_name);
    comm_file.close();
  } else {
    process_name = "(Unknown)";
  }
  process_name += " " + std::to_string(pid);
  return process_name;
}
}  // namespace

bool StoreProcessesNames(DBusConnection* connection,
                         DBusError* error,
                         Maps& maps) {
  DBusMessage* message;
  message = dbus_message_new_method_call(DBUS_SERVICE_DBUS, DBUS_PATH_DBUS,
                                         DBUS_INTERFACE_DBUS, "ListNames");
  if (!message) {
    LOG(ERROR) << "Failed to create a message";
    return false;
  }

  DBusMessage* reply;
  DBusMessageIter iter, array_iter;
  reply = SendMessage(connection, message, error);
  if (!reply) {
    return false;
  }
  dbus_message_iter_init(reply, &iter);
  dbus_message_iter_recurse(&iter, &array_iter);
  while (dbus_message_iter_get_arg_type(&array_iter) != DBUS_TYPE_INVALID) {
    const char* dbus_name;
    dbus_message_iter_get_basic(&array_iter, &dbus_name);

    std::string unique_name = dbus_name;
    DCHECK(dbus_name);
    if (dbus_name[0] != ':') {
      // dbus_name is a well-known name
      unique_name = GetUniqueName(connection, error, dbus_name);
      maps.names[dbus_name] = unique_name;
    }

    uint32_t pid = GetPid(connection, error, dbus_name);
    std::string process_name = GetProcessName(pid);
    uint64_t id = std::hash<std::string>{}(process_name);

    ProcessInfo process_info;
    process_info.id = id;
    process_info.name = process_name;
    process_info.methods = std::make_unique<MethodMap>();
    maps.processes[unique_name] = std::move(process_info);

    dbus_message_iter_next(&array_iter);
  }
  dbus_message_unref(reply);
  return true;
}
