// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dbus_perfetto_producer/dbus_monitor.h"

#include <base/logging.h>
#include <dbus/dbus.h>

#include "dbus_perfetto_producer/perfetto_producer.h"

namespace {
bool WriteInt(int fd, uint64_t num) {
  return (write(fd, &num, sizeof(uint64_t)) > 0);
}

bool WriteBuf(int fd, const char* name) {
  size_t len = name ? strlen(name) + 1 : 0;
  return (write(fd, &len, sizeof(size_t)) > 0 && write(fd, name, len) >= 0);
}

// filter function to be registered to D-Bus daemon
DBusHandlerResult PassMessage(DBusConnection* connection,
                              DBusMessage* message,
                              void* fd_ptr) {
  uint64_t timestamp = perfetto::TrackEvent::GetTraceTimeNs();
  int fd = static_cast<int>(reinterpret_cast<intptr_t>(fd_ptr));
  if (!WriteInt(fd, dbus_message_get_type(message)) ||
      !WriteBuf(fd, dbus_message_get_member(message)) ||
      !WriteBuf(fd, dbus_message_get_interface(message)) ||
      !WriteBuf(fd, dbus_message_get_sender(message)) ||
      !WriteBuf(fd, dbus_message_get_destination(message)) ||
      !WriteInt(fd, dbus_message_get_serial(message)) ||
      !WriteInt(fd, dbus_message_get_reply_serial(message)) ||
      !WriteInt(fd, timestamp)) {
    PLOG(ERROR) << "Failed to write to the file";
  }
  return DBUS_HANDLER_RESULT_HANDLED;
}

// Convert the connection into a monitor connection
bool BecomeMonitor(DBusConnection* connection, DBusError* error) {
  DBusMessage* message;
  DBusMessage* reply;
  dbus_uint32_t zero = 0;
  DBusMessageIter appender, array_appender;

  message =
      dbus_message_new_method_call(DBUS_SERVICE_DBUS, DBUS_PATH_DBUS,
                                   DBUS_INTERFACE_MONITORING, "BecomeMonitor");
  if (!message) {
    LOG(ERROR) << "Failed to become a monitor";
    return false;
  }

  dbus_message_iter_init_append(message, &appender);
  if (!dbus_message_iter_open_container(&appender, DBUS_TYPE_ARRAY, "s",
                                        &array_appender)) {
    LOG(ERROR) << "Failed to append string array";
    return false;
  }
  if (!dbus_message_iter_close_container(&appender, &array_appender) ||
      !dbus_message_iter_append_basic(&appender, DBUS_TYPE_UINT32, &zero)) {
    LOG(ERROR) << "Failed to finish adding arguments";
    return false;
  }

  reply =
      dbus_connection_send_with_reply_and_block(connection, message, -1, error);
  if (dbus_error_is_set(error)) {
    LOG(ERROR) << "Failed to get a reply: " << error->name << " "
               << error->message;
    dbus_error_free(error);
    return false;
  }
  if (!reply) {
    LOG(ERROR) << "Failed to get a reply";
    return false;
  }

  dbus_message_unref(reply);
  dbus_message_unref(message);
  return true;
}
}  // namespace

bool SetupConnection(DBusConnection* connection, DBusError* error, int fd) {
  DBusHandleMessageFunction filter_func = PassMessage;
  void* fd_ptr = reinterpret_cast<void*>(static_cast<intptr_t>(fd));
  if (!dbus_connection_add_filter(connection, filter_func, fd_ptr, nullptr)) {
    LOG(ERROR) << "Failed to add a filter";
    return false;
  }

  if (!BecomeMonitor(connection, error)) {
    return false;
  }

  return true;
}
