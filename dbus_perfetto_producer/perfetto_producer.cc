// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dbus_perfetto_producer/perfetto_producer.h"

#include <sys/poll.h>

#include <fstream>
#include <utility>

#include <base/check.h>
#include <base/logging.h>
#include <dbus/dbus.h>

PERFETTO_TRACK_EVENT_STATIC_STORAGE();

namespace {
// Send a D-Bus message (method call) and wait for a reply (method return)
DBusMessage* SendMessage(DBusConnection* connection,
                         DBusMessage* message,
                         DBusError* error) {
  DBusMessage* reply;
  reply =
      dbus_connection_send_with_reply_and_block(connection, message, -1, error);
  if (dbus_error_is_set(error)) {
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
    process_name = "Unknown";
  }
  process_name += " " + std::to_string(pid);
  return process_name;
}

ProcessInfo& StoreProcessName(DBusConnection* connection,
                              DBusError* error,
                              Maps& maps,
                              const char* dbus_name) {
  std::string unique_name = std::string(dbus_name);
  DCHECK(dbus_name);
  if (dbus_name[0] != ':') {
    // dbus_name is a D-Bus well-known name
    const char* unique_name_ptr = GetUniqueName(connection, error, dbus_name);
    if (unique_name_ptr) {
      unique_name = std::string(unique_name_ptr);
    }
    maps.names[std::string(dbus_name)] = unique_name;
  }

  uint32_t pid = GetPid(connection, error, dbus_name);
  std::string process_name;
  if (pid) {
    process_name = GetProcessName(pid);
  } else {
    process_name = "Unknown (" + std::string(dbus_name) + ")";
  }
  uint64_t id = std::hash<std::string>{}(process_name);

  ProcessInfo process_info;
  process_info.id = id;
  process_info.name = process_name;
  process_info.methods = std::make_unique<MethodMap>();

  // Store processes[unique_name] = ProcessInfo
  auto process = maps.processes.insert({unique_name, std::move(process_info)});

  return process.first->second;
}

bool ReadInt(int fd, uint64_t& ptr) {
  return (read(fd, &ptr, sizeof(uint64_t)) > 0);
}

bool ReadBuf(int fd, std::string& buf) {
  size_t size;
  if (read(fd, &size, sizeof(size_t)) <= 0) {
    return false;
  }

  if (size > 0) {
    char* buf_tmp = new char[size];
    if (read(fd, buf_tmp, size) <= 0) {
      return false;
    }
    buf = std::string(buf_tmp);
    delete[] buf_tmp;
  } else {
    buf = "";
  }

  return true;
}

bool GetMessageInfo(int fd, MessageInfo& message_info) {
  if (!ReadInt(fd, message_info.message_type) ||
      !ReadBuf(fd, message_info.member) ||
      !ReadBuf(fd, message_info.interface) ||
      !ReadBuf(fd, message_info.sender) ||
      !ReadBuf(fd, message_info.destination) ||
      !ReadInt(fd, message_info.serial) ||
      !ReadInt(fd, message_info.reply_serial) ||
      !ReadInt(fd, message_info.timestamp)) {
    LOG(ERROR) << "Failed to read from the pipe";
    return false;
  }
  return true;
}

ProcessInfo& GetProcessInfo(DBusConnection* connection,
                            DBusError* error,
                            Maps& maps,
                            std::string dbus_name) {
  DCHECK(!dbus_name.empty());
  if (dbus_name[0] != ':') {
    // dbus_name is a D-Bus well-known name
    DBusNameMap::iterator name = maps.names.find(dbus_name);
    if (name == maps.names.end()) {
      return StoreProcessName(connection, error, maps, dbus_name.c_str());
    }
    dbus_name = name->second;
  }

  // dbus_name is a D-Bus unique name
  ProcessMap::iterator process = maps.processes.find(dbus_name);
  if (process == maps.processes.end()) {
    StoreProcessName(connection, error, maps, dbus_name.c_str());
    return GetProcessInfo(connection, error, maps, dbus_name);
  }
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

void CreatePerfettoEvent(DBusConnection* connection,
                         DBusError* error,
                         Maps& maps,
                         MessageInfo message_info) {
  ProcessInfo& sender =
      GetProcessInfo(connection, error, maps, message_info.sender);
  auto track_sender = BuildTrack(sender.id, sender.name);
  std::string event_name =
      ": " + message_info.member + " (" + message_info.interface + ")";

  // The serial of D-Bus message is not globally unique, so it cannot be used
  // as an id of Perfetto.
  // Here, the id is generated from string which a name of the sender process
  // combined with the serial. This must not use a name of the destination
  // process because the sender of a method return can be different
  // from the destination of a method call.
  uint64_t flow_id = std::hash<std::string>{}(
      sender.name + std::to_string(message_info.serial));

  switch (message_info.message_type) {
    case DBUS_MESSAGE_TYPE_SIGNAL: {
      // Create an instant in the caller (sender) track
      TRACE_EVENT_INSTANT(DBUS_PERFETTO_PRODUCER_PERFETTO_CATEGORY,
                          perfetto::DynamicString("Sender" + event_name),
                          track_sender, message_info.timestamp,
                          perfetto::Flow::ProcessScoped(flow_id));

      if (!message_info.destination.empty()) {
        ProcessInfo& destination =
            GetProcessInfo(connection, error, maps, message_info.destination);
        auto track_destination = BuildTrack(destination.id, destination.name);

        // Create an instant in the callee (destination) track
        TRACE_EVENT_INSTANT(DBUS_PERFETTO_PRODUCER_PERFETTO_CATEGORY,
                            perfetto::DynamicString("Receiver" + event_name),
                            track_destination, message_info.timestamp,
                            perfetto::TerminatingFlow::ProcessScoped(flow_id));
      }
      break;
    }

    case DBUS_MESSAGE_TYPE_METHOD_CALL: {
      // Create an instant in the caller (sender) track
      TRACE_EVENT_INSTANT(DBUS_PERFETTO_PRODUCER_PERFETTO_CATEGORY,
                          perfetto::DynamicString("Caller" + event_name),
                          track_sender, message_info.timestamp,
                          perfetto::Flow::ProcessScoped(flow_id));

      ProcessInfo& destination =
          GetProcessInfo(connection, error, maps, message_info.destination);

      // Intentionally using flow_id instead of process id to create a track.
      // Detail in BuildTrack().
      auto track_destination = BuildTrack(flow_id, destination.name);

      // id for a flow from Callee slice to Handler, Return or Error
      uint64_t return_flow_id = std::hash<std::string>{}(
          sender.name + std::to_string(message_info.serial) + "return");

      // Begin a slice in the callee (destination) track
      TRACE_EVENT_BEGIN(DBUS_PERFETTO_PRODUCER_PERFETTO_CATEGORY,
                        perfetto::DynamicString("Callee" + event_name),
                        track_destination, message_info.timestamp,
                        perfetto::TerminatingFlow::ProcessScoped(flow_id),
                        perfetto::Flow::ProcessScoped(return_flow_id));

      // Store the destination of method call because the sender of method
      // can be different from this destination
      // sender.methods->insert({message_info.serial, destination.name});
      (*sender.methods)[message_info.serial] = destination.name;
      break;
    }

    case DBUS_MESSAGE_TYPE_METHOD_RETURN:
    case DBUS_MESSAGE_TYPE_ERROR: {
      event_name = (message_info.message_type == DBUS_MESSAGE_TYPE_ERROR)
                       ? "Error"
                       : "Return";
      ProcessInfo& destination =
          GetProcessInfo(connection, error, maps, message_info.destination);
      auto track_destination = BuildTrack(destination.id, destination.name);
      uint64_t id = std::hash<std::string>{}(
          destination.name + std::to_string(message_info.reply_serial));
      uint64_t return_flow_id = std::hash<std::string>{}(
          destination.name + std::to_string(message_info.reply_serial) +
          "return");
      auto track = perfetto::Track(id);

      // End the corresponding slice
      TRACE_EVENT_END(DBUS_PERFETTO_PRODUCER_PERFETTO_CATEGORY, track,
                      message_info.timestamp);

      std::string original_destination = GetOriginalDestination(
          *destination.methods, message_info.reply_serial);
      if (sender.name.compare(original_destination)) {
        // If the method return is sent by different process from the
        // destination of the corresponding method call, create an instant in
        // the caller (sender) of the method return track.
        TRACE_EVENT_INSTANT(
            DBUS_PERFETTO_PRODUCER_PERFETTO_CATEGORY,
            perfetto::DynamicString("Handler"), track_sender,
            message_info.timestamp,
            perfetto::TerminatingFlow::ProcessScoped(return_flow_id),
            perfetto::Flow::ProcessScoped(flow_id));

        return_flow_id = flow_id;
      }

      // Create an instant in the callee (destination) track
      TRACE_EVENT_INSTANT(
          DBUS_PERFETTO_PRODUCER_PERFETTO_CATEGORY,
          perfetto::DynamicString(event_name), track_destination,
          message_info.timestamp,
          perfetto::TerminatingFlow::ProcessScoped(return_flow_id));
      break;
    }

    default: {
      LOG(ERROR) << "Unknown D-Bus message type: "
                 << std::to_string(message_info.message_type);
      break;
    }
  }
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
    StoreProcessName(connection, error, maps, dbus_name);
    dbus_message_iter_next(&array_iter);
  }
  dbus_message_unref(reply);
  return true;
}

// Return only when an error occurs
void PerfettoProducer(DBusConnection* connection,
                      DBusError* error,
                      Maps& maps,
                      int fd) {
  pollfd pollfd[1];
  pollfd[0].fd = fd;
  pollfd[0].events = POLLIN;

  // This while loop only breaks when an error occurs
  while (true) {
    if (poll(pollfd, 1, -1) < 1) {
      LOG(ERROR) << "Failed to poll";
      continue;
    }

    MessageInfo message_info;
    if (!GetMessageInfo(fd, message_info)) {
      return;
    }
    CreatePerfettoEvent(connection, error, maps, message_info);
  }
}
