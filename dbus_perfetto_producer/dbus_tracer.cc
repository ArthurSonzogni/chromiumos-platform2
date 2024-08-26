// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dbus_perfetto_producer/dbus_tracer.h"

#include <string>

#include <base/logging.h>
#include <dbus/dbus.h>
#include <perfetto/perfetto.h>

#include "dbus_perfetto_producer/dbus_request.h"

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

ProcessInfo GetProcessInfo(std::string dbus_name, ProcessMap* processes) {
  ProcessMap::iterator process = processes->find(dbus_name);
  if (process != processes->end()) {
    return process->second;
  } else {
    ProcessInfo process_info;
    process_info.uuid = std::hash<std::string>{}(dbus_name);
    process_info.name = "Unknown (" + dbus_name + ") ";
    return process_info;
  }
}

// This function creates a new perfetto::Track object every time when called.
// However, events created on different track objects with the same track name
// appear on the same track on Perfetto UI. Having the different track uuid
// helps to distinguish multiple events that should appear on the same track on
// UI.
perfetto::Track BuildTrack(uint64_t uuid, std::string track_name) {
  auto track = perfetto::Track(uuid);
  auto desc = track.Serialize();
  desc.set_name(track_name);
  perfetto::TrackEvent::SetTrackDescriptor(track, desc);
  return track;
}

// filter function to be registered to D-Bus daemon
DBusHandlerResult CreatePerfettoEvent(DBusConnection* connection,
                                      DBusMessage* message,
                                      void* processes_data) {
  ProcessMap* processes = reinterpret_cast<ProcessMap*>(processes_data);
  int message_type = dbus_message_get_type(message);
  ProcessInfo sender =
      GetProcessInfo(dbus_message_get_sender(message), processes);
  const char* destination_name = dbus_message_get_destination(message);
  std::string serial = std::to_string(dbus_message_get_serial(message));
  uint64_t flow_id = std::hash<std::string>{}(sender.name + serial);
  std::string event_name = GetEventName(message);

  switch (message_type) {
    case DBUS_MESSAGE_TYPE_SIGNAL: {
      // Create an instant in the caller (sender) track
      auto track_sender = BuildTrack(sender.uuid, sender.name);
      TRACE_EVENT_INSTANT(DBUS_PERFETTO_PRODUCER_PERFETTO_CATEGORY,
                          perfetto::DynamicString("Signal" + event_name),
                          track_sender, perfetto::Flow::ProcessScoped(flow_id));
      if (destination_name) {
        // Create an instant in the callee (destination) track
        ProcessInfo destination = GetProcessInfo(destination_name, processes);
        auto track_destination = BuildTrack(destination.uuid, destination.name);
        TRACE_EVENT_INSTANT(DBUS_PERFETTO_PRODUCER_PERFETTO_CATEGORY,
                            perfetto::DynamicString("Signal" + event_name),
                            track_destination,
                            perfetto::TerminatingFlow::ProcessScoped(flow_id));
      }
      break;
    }
    case DBUS_MESSAGE_TYPE_METHOD_CALL: {
      // Create an instant in the caller (sender) track
      auto track_sender = BuildTrack(sender.uuid, sender.name);
      TRACE_EVENT_INSTANT(DBUS_PERFETTO_PRODUCER_PERFETTO_CATEGORY,
                          perfetto::DynamicString("Method" + event_name),
                          track_sender, perfetto::Flow::ProcessScoped(flow_id));

      // Begin a slice in the callee (destination) track
      ProcessInfo destination = GetProcessInfo(destination_name, processes);

      // The serial of D-Bus message is not globally unique, so it cannot be
      // as an uuid of Perfetto.
      // Here, the uuid is generated from string which a name of the sender
      // process combined with the serial. This must not use a name of the
      // destination process because the method return can be sent from another
      // process.
      uint64_t uuid = std::hash<std::string>{}(sender.name + serial);

      auto track_destination = BuildTrack(uuid, destination.name);
      TRACE_EVENT_BEGIN(DBUS_PERFETTO_PRODUCER_PERFETTO_CATEGORY,
                        perfetto::DynamicString("Method" + event_name),
                        track_destination,
                        perfetto::TerminatingFlow::ProcessScoped(flow_id));
      break;
    }
    case DBUS_MESSAGE_TYPE_METHOD_RETURN:
    case DBUS_MESSAGE_TYPE_ERROR: {
      // End the corresponding slice
      ProcessInfo destination = GetProcessInfo(destination_name, processes);
      std::string reply_serial =
          std::to_string(dbus_message_get_reply_serial(message));
      uint64_t uuid = std::hash<std::string>{}(destination.name + reply_serial);
      auto track = perfetto::Track(uuid);
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
    LOG(ERROR) << "Failed to get a reply: " +
                      static_cast<std::string>(error->name) + " " +
                      static_cast<std::string>(error->message);
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

bool DbusTracer(DBusConnection* connection,
                DBusError* error,
                ProcessMap* processes) {
  DBusHandleMessageFunction filter_func = CreatePerfettoEvent;
  if (!dbus_connection_add_filter(connection, filter_func, processes,
                                  nullptr)) {
    LOG(ERROR) << "Failed to add a filter";
    return false;
  }
  if (!BecomeMonitor(connection, error)) {
    return false;
  }

  LOG(INFO) << "Finished initialisation. Starting tracing.";
  while (dbus_connection_read_write_dispatch(connection, -1)) {
  }
  return true;
}
