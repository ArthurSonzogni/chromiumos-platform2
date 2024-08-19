// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dbus_perfetto_producer/dbus_tracer.h"

#include <string>

#include <base/logging.h>
#include <dbus/dbus.h>
#include <perfetto/perfetto.h>

PERFETTO_TRACK_EVENT_STATIC_STORAGE();

namespace {
std::string GetEventName(DBusMessage* message) {
  std::string event_name = ":";
  const char* member = dbus_message_get_member(message);
  const char* interface = dbus_message_get_interface(message);
  if (member) {
    event_name += " " + static_cast<std::string>(member);
  }
  if (interface) {
    event_name += " (" + static_cast<std::string>(interface) + ")";
  }
  return event_name;
}

// This function creates a new perfetto::Track object every time when called.
// However, events created on different track objects with the same track name
// appear on the same track on Perfetto UI. Having the different track uuid
// helps to distinguish multiple events that should appear on the same track on
// UI.
perfetto::Track BuildTrack(uint32_t uuid, std::string track_name) {
  auto track = perfetto::Track(uuid);
  auto desc = track.Serialize();
  desc.set_name(track_name);
  perfetto::TrackEvent::SetTrackDescriptor(track, desc);
  return track;
}

// filter function to be registered to D-Bus daemon
DBusHandlerResult CreatePerfettoEvent(DBusConnection* connection,
                                      DBusMessage* message,
                                      void* user_data) {
  int message_type = dbus_message_get_type(message);
  const char* sender = dbus_message_get_sender(message);
  const char* destination = dbus_message_get_destination(message);
  uint32_t serial = dbus_message_get_serial(message);
  std::string event_name = GetEventName(message);

  switch (message_type) {
    case DBUS_MESSAGE_TYPE_SIGNAL: {
      // Create an instant in the caller (sender) track
      std::hash<std::string> hasher;
      uint64_t hashed = hasher(sender);
      auto track_sender = BuildTrack(hashed, sender);
      TRACE_EVENT_INSTANT(DBUS_PERFETTO_PRODUCER_PERFETTO_CATEGORY,
                          perfetto::DynamicString("Signal" + event_name),
                          track_sender, perfetto::Flow::ProcessScoped(serial));
      if (destination) {
        // Create an instant in the callee (destination) track
        auto track_destination = BuildTrack(serial, destination);
        TRACE_EVENT_INSTANT(DBUS_PERFETTO_PRODUCER_PERFETTO_CATEGORY,
                            perfetto::DynamicString(event_name),
                            track_destination,
                            perfetto::TerminatingFlow::ProcessScoped(serial));
      }
      break;
    }
    case DBUS_MESSAGE_TYPE_METHOD_CALL: {
      // Create an instant in the caller (sender) track
      std::hash<std::string> hasher;
      uint64_t hashed = hasher(sender);
      auto track_sender = BuildTrack(hashed, sender);
      TRACE_EVENT_INSTANT(DBUS_PERFETTO_PRODUCER_PERFETTO_CATEGORY,
                          perfetto::DynamicString("Method" + event_name),
                          track_sender, perfetto::Flow::ProcessScoped(serial));

      // Begin a slice with uuid of serial in the callee (destination) track
      auto track_destination = BuildTrack(serial, destination);
      TRACE_EVENT_BEGIN(DBUS_PERFETTO_PRODUCER_PERFETTO_CATEGORY,
                        perfetto::DynamicString("Method" + event_name),
                        track_destination,
                        perfetto::TerminatingFlow::ProcessScoped(serial));
      break;
    }
    case DBUS_MESSAGE_TYPE_METHOD_RETURN:
    case DBUS_MESSAGE_TYPE_ERROR: {
      // End a slice with uuid of reply serial
      auto track = perfetto::Track(dbus_message_get_reply_serial(message));
      TRACE_EVENT_END(DBUS_PERFETTO_PRODUCER_PERFETTO_CATEGORY, track);
      break;
    }
    default: {
      LOG(ERROR) << "Unknown D-Bus message type: " +
                        std::to_string(message_type);
      break;
    }
  }

  return DBUS_HANDLER_RESULT_HANDLED;
}

// Convert the connection into a monitor connection
bool BecomeMonitor(DBusConnection* connection) {
  DBusError error = DBUS_ERROR_INIT;
  DBusMessage* m;
  DBusMessage* r;
  dbus_uint32_t zero = 0;
  DBusMessageIter appender, array_appender;

  m = dbus_message_new_method_call(DBUS_SERVICE_DBUS, DBUS_PATH_DBUS,
                                   DBUS_INTERFACE_MONITORING, "BecomeMonitor");
  if (!m) {
    LOG(ERROR) << "Failed to become a monitor";
    return false;
  }

  dbus_message_iter_init_append(m, &appender);
  if (!dbus_message_iter_open_container(&appender, DBUS_TYPE_ARRAY, "s",
                                        &array_appender)) {
    LOG(ERROR) << "Failed to append string array";
    return false;
  }
  if (!dbus_message_iter_close_container(&appender, &array_appender) ||
      !dbus_message_iter_append_basic(&appender, DBUS_TYPE_UINT32, &zero)) {
    LOG(ERROR) << "Failed to append zero";
    return false;
  }

  r = dbus_connection_send_with_reply_and_block(connection, m, -1, &error);

  if (r) {
    dbus_message_unref(r);
  } else {
    LOG(ERROR) << "Failed to get a reply: " +
                      static_cast<std::string>(error.name) + " " +
                      static_cast<std::string>(error.message);
    dbus_error_free(&error);
  }

  dbus_message_unref(m);

  return (r != nullptr);
}
}  // namespace

bool DbusTracer() {
  DBusConnection* connection;
  DBusError error;
  DBusBusType type = DBUS_BUS_SYSTEM;
  DBusHandleMessageFunction filter_func = CreatePerfettoEvent;

  dbus_error_init(&error);
  connection = dbus_bus_get(type, &error);

  if (!connection) {
    LOG(ERROR) << "Failed to open a connection: " +
                      static_cast<std::string>(error.name) + " " +
                      static_cast<std::string>(error.message);
    dbus_error_free(&error);
    return false;
  }

  if (!dbus_connection_add_filter(connection, filter_func, nullptr, nullptr)) {
    LOG(ERROR) << "Failed to add a filter";
    return false;
  }

  if (!BecomeMonitor(connection)) {
    return false;
  }

  while (dbus_connection_read_write_dispatch(connection, -1)) {
  }
  return true;
}
