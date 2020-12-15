// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/missive_daemon.h"

#include <cstdlib>
#include <deque>
#include <fcntl.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <type_traits>
#include <utility>
#include <vector>

#include <chromeos/dbus/service_constants.h>

namespace reporting {

namespace {

// Logs the error message and returns a D-Bus error object with the message.
brillo::ErrorPtr LogError(const std::string& msg) {
  LOG(ERROR) << msg;
  return brillo::Error::Create(FROM_HERE, "missived", "INTERNAL", msg);
}

}  // namespace

MissiveDaemon::MissiveDaemon()
    : brillo::DBusServiceDaemon(::missive::kMissiveServiceName),
      org::chromium::MissivedAdaptor(this) {}

int MissiveDaemon::OnInit() {
  const int exit_code = DBusServiceDaemon::OnInit();
  if (exit_code != EXIT_SUCCESS) {
    LOG(ERROR) << "Shutting down due to fatal DBus initialization failure";
    return exit_code;
  }

  LOG(INFO) << "Starting...";

  // TODO(zatrudo): startup code here
  return EXIT_SUCCESS;
}

void MissiveDaemon::RegisterDBusObjectsAsync(
    brillo::dbus_utils::AsyncEventSequencer* sequencer) {
  dbus_object_ = std::make_unique<brillo::dbus_utils::DBusObject>(
      /*object_manager=*/nullptr, bus_,
      org::chromium::MissivedAdaptor::GetObjectPath());
  RegisterWithDBusObject(dbus_object_.get());
  dbus_object_->RegisterAsync(
      sequencer->GetHandler("RegisterAsync failed." /* descriptive_message */,
                            true /* failure_is_fatal */));
  LOG(INFO) << "Initialized Missive Service DBus interface";
}

void MissiveDaemon::EnqueueRecords(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        reporting::test::TestMessage>> response,
    const reporting::test::TestMessage& in_request) {
  // TODO(zatrudo): Consider wrapping this in a task and reply and posting to a
  // separate task runner off of DBus.
  brillo::ErrorPtr err = LogError("Function EnqueueRecords not implemented");
  std::move(response)->ReplyWithError(err.get());
}

void MissiveDaemon::FlushPriority(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        reporting::test::TestMessage>> response,
    const reporting::test::TestMessage& in_request) {
  brillo::ErrorPtr err = LogError("Function FlushPriority not implemented");
  std::move(response)->ReplyWithError(err.get());
}

void MissiveDaemon::ConfirmRecordUpload(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        reporting::test::TestMessage>> response,
    const reporting::test::TestMessage& in_request) {
  brillo::ErrorPtr err =
      LogError("Function ConfirmRecordUpload not implemented");
  std::move(response)->ReplyWithError(err.get());
}

}  // namespace reporting
