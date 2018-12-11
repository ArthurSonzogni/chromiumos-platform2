// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/at_exit.h>
#include <base/bind.h>
#include <base/memory/weak_ptr.h>
#include <base/single_thread_task_runner.h>
#include <base/threading/thread.h>
#include <dbus/dbus.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <unistd.h>
#include <iostream>
#include <memory>
// NOLINTNEXTLINE
#include <mutex>
#include <string>

#include "base/logging.h"
#include "media_perception/cras_client_impl.h"
#include "media_perception/cras_client_wrapper.h"
#include "media_perception/dbus_service.h"
#include "media_perception/mojo_connector.h"
#include "media_perception/video_capture_service_client_impl.h"
#include "mojom/media_perception_service.mojom.h"

namespace {

// We need to poll the dbus message queue periodically for handling new method
// calls. This variable defines the polling period in milliseconds, and it will
// affect the responsiveness of the dbus server and cpu usage.
constexpr int kPollingPeriodMilliSeconds = 1;

std::string RequestOwnershipReplyToString(unsigned int reply) {
  switch (reply) {
    case DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER:
      return "DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER";
    case DBUS_REQUEST_NAME_REPLY_IN_QUEUE:
      return "DBUS_REQUEST_NAME_REPLY_IN_QUEUE";
    case DBUS_REQUEST_NAME_REPLY_EXISTS:
      return "DBUS_REQUEST_NAME_REPLY_EXISTS";
    case DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER:
      return "DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER";
    default:
      return "UNKNOWN_TYPE";
  }
}

}  // namespace

namespace mri {

class CrOSDbusService : public mri::DbusService {
 public:
  CrOSDbusService();

  void SetMojoConnector(MojoConnector* mojo_connector) {
    mojo_connector_ = mojo_connector;
  }

  // Disconnects dbus connection.
  ~CrOSDbusService() override;

  // Establishes dbus connection. bus_type could be either DBUS_BUS_SYSTEM or
  // DBUS_BUS_SESSION in order to use system bus or session bus, respectively.
  // service_ownership_mask is a bitmask that indicates how this service
  // provider is going to own the service name. All valid bitmasks can be found
  // in third_party/dbus/src/dbus/dbus-shared.h. For example,
  // Connect(DBUS_BUS_SYSTEM, DBUS_NAME_FLAG_REPLACE_EXISTING) means this
  // dbus entity will be connected to system bus, and take ownership of the
  // service name from the exitsing owner (if there is any).
  void Connect(const mri::Service service) override;

  // Checks if dbus connection has been established.
  bool IsConnected() const override;

  // Publish a signal to dbus.
  bool PublishSignal(const mri::Signal signal,
                     const std::vector<uint8_t>* bytes) override;

  // Polls the message queue periodically for handling dbus method calls. Valid
  // requests will be processed by the set MessageHandler.
  void PollMessageQueue() override;

 private:
  // Processes this dbus message and stores the reply in |bytes|. Return value
  // indicates if processing the message was successful and if a reply should be
  // sent.
  bool ProcessMessage(DBusMessage* message, std::vector<uint8_t>* bytes);

  // This mutex is used to guard concurrent access to the dbus connection.
  mutable std::mutex connection_lock_;

  // The service takes ownership of the pointer. Its deletion or decommissioning
  // has to be handled specifically by dbus_connection_unref().
  DBusConnection* connection_;

  // The MojoConnector object pointer for bootstrapping the mojo connection over
  // D-Bus.
  MojoConnector* mojo_connector_;
};

CrOSDbusService::CrOSDbusService() : connection_(nullptr) {}

CrOSDbusService::~CrOSDbusService() {
  // Applications should unref the shared connection created with
  // dbus_bus_get().
  if (IsConnected()) {
    std::lock_guard<std::mutex> lock(connection_lock_);
    dbus_connection_unref(connection_);
    connection_ = nullptr;
  }
}

void CrOSDbusService::Connect(const Service service) {
  if (IsConnected()) {
    LOG(WARNING) << "Dbus connection has already been established.";
    return;
  }

  DBusError error;
  dbus_error_init(&error);

  if (dbus_error_is_set(&error)) {
    LOG(ERROR) << "Dbus connection error: " << error.message;
    dbus_error_free(&error);
    return;
  }

  std::lock_guard<std::mutex> lock(connection_lock_);
  connection_ = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
  CHECK(connection_ != nullptr) << "Connection is nullptr.";

  // This request will return -1 if error is set, and a non-negative number
  // otherwise.
  const int reply = dbus_bus_request_name(
      connection_, ServiceEnumToServiceName(service).c_str(),
      DBUS_NAME_FLAG_REPLACE_EXISTING, &error);

  CHECK(reply >= 0) << "Failed to own media perception service: "
                    << error.message;

  DLOG(INFO) << "dbus_connection_get_server_id = "
             << dbus_connection_get_server_id(connection_);
  DLOG(INFO) << "dbus_bus_get_id = " << dbus_bus_get_id(connection_, &error);
  DLOG(INFO) << "dbus_get_local_machine_id = " << dbus_get_local_machine_id();
  DLOG(INFO) << "dbus_request_name() has reply: "
             << RequestOwnershipReplyToString(reply);

  // Store the service enum for the active connection.
  service_ = service;
}

bool CrOSDbusService::IsConnected() const {
  std::lock_guard<std::mutex> lock(connection_lock_);
  return connection_ != nullptr;
}

bool CrOSDbusService::PublishSignal(const mri::Signal signal,
                                    const std::vector<uint8_t>* bytes) {
  if (bytes == nullptr) {
    LOG(WARNING) << "Failed to publish signal - bytes is nullptr.";
    return false;
  }

  if (!IsConnected()) {
    LOG(WARNING) << "Failed to publish signal - not connected.";
    return false;
  }

  DBusMessage* message =
      dbus_message_new_signal(ServiceEnumToServicePath(service_).c_str(),
                              ServiceEnumToServiceName(service_).c_str(),
                              SignalEnumToSignalName(signal).c_str());

  if (message == nullptr) {
    LOG(WARNING) << "Out of memory!";
    return false;
  }

  if (!dbus_message_append_args(message, DBUS_TYPE_ARRAY, DBUS_TYPE_BYTE, bytes,
                                bytes->size(), DBUS_TYPE_INVALID)) {
    LOG(WARNING) << "Out of memory!";
    dbus_message_unref(message);
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(connection_lock_);
    dbus_connection_send(connection_, message, nullptr);
    dbus_connection_flush(connection_);
  }

  dbus_message_unref(message);
  return true;
}

void CrOSDbusService::PollMessageQueue() {
  if (!IsConnected()) {
    LOG(WARNING) << "Failed to poll message queue.";
    return;
  }

  // This loop will continue until another management process explicitly kills
  // the current program.
  while (true) {
    DBusMessage* message = nullptr;

    {
      std::lock_guard<std::mutex> lock(connection_lock_);

      // Non-blocking read of the next available message.
      dbus_connection_read_write(connection_, 0);

      message = dbus_connection_pop_message(connection_);
    }

    // Poll the message queue every kPollingPeriodMilliSeconds for the new
    // method call.
    if (message == nullptr) {
      usleep(kPollingPeriodMilliSeconds * 1000);
      continue;
    }

    // Process this message and store the reply in |bytes|.
    std::vector<uint8_t> bytes;
    if (!ProcessMessage(message, &bytes)) {
      continue;
    }

    DBusMessage* reply = dbus_message_new_method_return(message);

    if (!bytes.empty()) {
      dbus_message_append_args(reply, DBUS_TYPE_ARRAY, DBUS_TYPE_BYTE, &bytes,
                               bytes.size(), DBUS_TYPE_INVALID);
    }

    {
      std::lock_guard<std::mutex> lock(connection_lock_);
      dbus_connection_send(connection_, reply, nullptr);
      dbus_connection_flush(connection_);
    }

    dbus_message_unref(reply);
    dbus_message_unref(message);
  }
}

bool CrOSDbusService::ProcessMessage(DBusMessage* message,
                                     std::vector<uint8_t>* bytes) {
  if (message == nullptr || bytes == nullptr) {
    LOG(WARNING) << "Failed to process this Dbus message.";
    return false;
  }

  // Check to see if its a BootstrapMojoConnection method call.
  if (dbus_message_is_method_call(
          message, ServiceEnumToServiceName(service_).c_str(),
          MethodEnumToMethodName(Method::BOOTSTRAP_MOJO_CONNECTION).c_str())) {
    DBusMessageIter iter;
    if (!dbus_message_iter_init(message, &iter)) {
      LOG(ERROR) << "Could not get iter.";
      return false;
    }

    DBusBasicValue value;
    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_UNIX_FD) {
      LOG(ERROR) << "Arg type is not UNIX_FD.";
    }
    dbus_message_iter_get_basic(&iter, &value);

    if (mojo_connector_ == nullptr) {
      LOG(ERROR) << "Mojo Connector is nullptr.";
      return false;
    }
    LOG(INFO) << "File descriptor: " << value.fd;
    mojo_connector_->ReceiveMojoInvitationFileDescriptor(value.fd);
    return true;
  }

  if (message_handler_ == nullptr) {
    LOG(ERROR) << "Message handler is not set.";
    return false;
  }

  // Check to see if its a GetDiagnostics method call.
  if (dbus_message_is_method_call(
          message, ServiceEnumToServiceName(service_).c_str(),
          MethodEnumToMethodName(Method::GET_DIAGNOSTICS).c_str())) {
    // No input arguments for GetDiagnostics.
    message_handler_(Method::GET_DIAGNOSTICS, nullptr, 0, bytes);
    return true;
  }

  // Check to see if its a State method call.
  if (!dbus_message_is_method_call(
          message, ServiceEnumToServiceName(service_).c_str(),
          MethodEnumToMethodName(Method::STATE).c_str())) {
    // Neither GetDiagnostics or State.
    return false;
  }

  // We have a State method call, check to see if it is a GetState call.
  DBusMessageIter iter;
  if (!dbus_message_iter_init(message, &iter)) {
    // No input arguments for GetState.
    message_handler_(Method::STATE, nullptr, 0, bytes);
    return true;
  }

  // This means SetState and we use the following variables to store
  // arguments of this method call.
  uint8_t* arg_bytes = nullptr;
  int arg_size = 0;

  if (!dbus_message_get_args(message, nullptr, DBUS_TYPE_ARRAY, DBUS_TYPE_BYTE,
                             &arg_bytes, &arg_size, DBUS_TYPE_INVALID)) {
    LOG(WARNING) << "Failed to parse args of a SetState method call.";
    return false;
  }

  message_handler_(Method::STATE, arg_bytes, arg_size, bytes);
  return true;
}
}  // namespace mri

using DbusServicePtr = std::unique_ptr<mri::DbusService>;
using CrasClientWrapperPtr = std::unique_ptr<mri::CrasClientWrapper>;
using VideoCaptureServiceClientPtr =
    std::unique_ptr<mri::VideoCaptureServiceClient>;
// This is a reference to run_rtanalytics() in the RTA library.
extern "C" int run_rtanalytics(int argc, char** argv, DbusServicePtr&& dbus,
                               CrasClientWrapperPtr&& cras,
                               VideoCaptureServiceClientPtr&& vidcap);

int main(int argc, char** argv) {
  // Needs to exist for creating and starting ipc_thread.
  base::AtExitManager exit_manager;

  mri::MojoConnector mojo_connector;
  mri::CrOSDbusService* cros_dbus_service = new mri::CrOSDbusService();
  cros_dbus_service->SetMojoConnector(&mojo_connector);

  mri::VideoCaptureServiceClientImpl* vidcap_client =
      new mri::VideoCaptureServiceClientImpl();
  vidcap_client->SetMojoConnector(&mojo_connector);

  auto dbus = std::unique_ptr<mri::DbusService>(cros_dbus_service);
  auto cras =
      std::unique_ptr<mri::CrasClientWrapper>(new mri::CrasClientImpl());
  auto vidcap = std::unique_ptr<mri::VideoCaptureServiceClient>(vidcap_client);

  const int return_value = run_rtanalytics(argc, argv, std::move(dbus),
                                           std::move(cras), std::move(vidcap));
  return return_value;
}
