// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dbus_perfetto_producer/dbus_tracer.h"

#include <memory>
#include <string>
#include <utility>

#include <base/check.h>
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

ProcessInfo& GetProcessInfo(std::string dbus_name, Maps& maps) {
  DCHECK(!dbus_name.empty());
  if (dbus_name[0] != ':') {
    // dbus_name is a d-bus well-known name
    DBusNameMap::iterator name = maps.names.find(dbus_name);
    if (name != maps.names.end()) {
      dbus_name = name->second;
    }
  }
  ProcessMap::iterator process = maps.processes.find(dbus_name);
  if (process != maps.processes.end()) {
    return process->second;
  }

  // Register a new ProcessInfo to ProcessMap.
  ProcessInfo process_info;
  process_info.id = std::hash<std::string>{}(dbus_name);
  process_info.name = "Unknown (" + dbus_name + ") ";
  process_info.methods = std::make_unique<MethodMap>();
  process = maps.processes.insert({dbus_name, std::move(process_info)}).first;
  return process->second;
}

std::string GetOriginalDestination(MethodMap& methods, uint64_t serial) {
  MethodMap::iterator method = methods.find(serial);
  if (method != methods.end()) {
    std::string destination = method->second;
    methods.erase(method);
    return destination;
  }
  return "";
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
                                      void* maps_ptr) {
  Maps* maps = reinterpret_cast<Maps*>(maps_ptr);

  int message_type = dbus_message_get_type(message);
  ProcessInfo& sender = GetProcessInfo(dbus_message_get_sender(message), *maps);
  auto track_sender = BuildTrack(sender.id, sender.name);
  const char* destination_name = dbus_message_get_destination(message);
  uint64_t serial = dbus_message_get_serial(message);
  std::string event_name = GetEventName(message);

  // The serial of D-Bus message is not globally unique, so it cannot be used
  // as an id of Perfetto.
  // Here, the id is generated from string which a name of the sender process
  // combined with the serial. This must not use a name of the destination
  // process because the sender of a method return can be different
  // from the destination of a method call.
  uint64_t flow_id =
      std::hash<std::string>{}(sender.name + std::to_string(serial));

  switch (message_type) {
    case DBUS_MESSAGE_TYPE_SIGNAL: {
      // Create an instant in the sender track
      TRACE_EVENT_INSTANT(DBUS_PERFETTO_PRODUCER_PERFETTO_CATEGORY,
                          perfetto::DynamicString("Sender" + event_name),
                          track_sender, perfetto::Flow::ProcessScoped(flow_id));

      if (destination_name) {
        ProcessInfo& destination = GetProcessInfo(destination_name, *maps);
        auto track_destination = BuildTrack(destination.id, destination.name);

        // Create an instant in the receiver (destination) track
        TRACE_EVENT_INSTANT(DBUS_PERFETTO_PRODUCER_PERFETTO_CATEGORY,
                            perfetto::DynamicString("Receiver" + event_name),
                            track_destination,
                            perfetto::TerminatingFlow::ProcessScoped(flow_id));
      }
      break;
    }

    case DBUS_MESSAGE_TYPE_METHOD_CALL: {
      // Create an instant in the caller (sender) track
      TRACE_EVENT_INSTANT(DBUS_PERFETTO_PRODUCER_PERFETTO_CATEGORY,
                          perfetto::DynamicString("Caller" + event_name),
                          track_sender, perfetto::Flow::ProcessScoped(flow_id));

      ProcessInfo& destination = GetProcessInfo(destination_name, *maps);

      // Intentionally using flow_id instead of process id to create a track.
      // See BuildTrack().
      auto track_destination = BuildTrack(flow_id, destination.name);

      // id for a flow from Method Call to Method handler or Method Returned
      uint64_t return_flow_id = std::hash<std::string>{}(
          sender.name + std::to_string(serial) + "return");

      // Begin a slice in the callee (destination) track
      TRACE_EVENT_BEGIN(DBUS_PERFETTO_PRODUCER_PERFETTO_CATEGORY,
                        perfetto::DynamicString("Callee" + event_name),
                        track_destination,
                        perfetto::TerminatingFlow::ProcessScoped(flow_id),
                        perfetto::Flow::ProcessScoped(return_flow_id));

      // Store the destination of method call because the sender of method
      // can be different from this destination
      sender.methods->insert({serial, destination.name});
      break;
    }

    case DBUS_MESSAGE_TYPE_METHOD_RETURN:
    case DBUS_MESSAGE_TYPE_ERROR: {
      ProcessInfo& destination = GetProcessInfo(destination_name, *maps);
      auto track_destination = BuildTrack(destination.id, destination.name);
      uint64_t reply_serial = dbus_message_get_reply_serial(message);
      uint64_t id = std::hash<std::string>{}(destination.name +
                                             std::to_string(reply_serial));
      uint64_t return_flow_id = std::hash<std::string>{}(
          destination.name + std::to_string(reply_serial) + "return");
      auto track = perfetto::Track(id);

      // End the corresponding slice
      TRACE_EVENT_END(DBUS_PERFETTO_PRODUCER_PERFETTO_CATEGORY, track);

      std::string original_destination =
          GetOriginalDestination(*destination.methods, reply_serial);
      if (sender.name.compare(original_destination)) {
        if (original_destination.empty()) {
          LOG(ERROR) << "Unmatched method return from " << sender.name << " to "
                     << destination.name;
        }

        // If the method return is sent by different process from the
        // destination of the corresponding method call, create an instant in
        // the caller (sender) of the method return track.
        TRACE_EVENT_INSTANT(
            DBUS_PERFETTO_PRODUCER_PERFETTO_CATEGORY,
            perfetto::DynamicString("Handler"), track_sender,
            perfetto::TerminatingFlow::ProcessScoped(return_flow_id),
            perfetto::Flow::ProcessScoped(flow_id));

        return_flow_id = flow_id;
      }

      event_name =
          (message_type == DBUS_MESSAGE_TYPE_ERROR) ? "Error" : "Return";

      // Create an instant in the callee (destination) track
      TRACE_EVENT_INSTANT(
          DBUS_PERFETTO_PRODUCER_PERFETTO_CATEGORY,
          perfetto::DynamicString("Method " + event_name), track_destination,
          perfetto::TerminatingFlow::ProcessScoped(return_flow_id));
      break;
    }

    default: {
      LOG(ERROR) << "Unknown D-Bus message type: "
                 << std::to_string(message_type);
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

bool DbusTracer(DBusConnection* connection, DBusError* error, Maps& maps) {
  DBusHandleMessageFunction filter_func = CreatePerfettoEvent;
  if (!dbus_connection_add_filter(connection, filter_func, &maps, nullptr)) {
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
