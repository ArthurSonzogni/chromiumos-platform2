// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "private_computing/private_computing_adaptor.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/location.h>
#include <base/logging.h>
#include <brillo/dbus/dbus_object.h>
#include <brillo/dbus/file_descriptor.h>
#include <brillo/errors/error.h>
#include <dbus/private_computing/dbus-constants.h>
#include <google/protobuf/message_lite.h>

#include "private_computing/proto_bindings/private_computing_service.pb.h"

namespace private_computing {

namespace {

// Serializes |proto| to a vector of bytes.
std::vector<uint8_t> SerializeProto(
    const google::protobuf::MessageLite& proto) {
  std::vector<uint8_t> proto_blob(proto.ByteSizeLong());
  CHECK(proto.SerializeToArray(proto_blob.data(), proto_blob.size()));
  return proto_blob;
}

// Parses a proto from an array of bytes |proto_blob|. Returns
// error message or empty string if no error.
std::string ParseProto(const base::Location& from_here,
                       google::protobuf::MessageLite* proto,
                       const std::vector<uint8_t>& proto_blob) {
  if (!proto->ParseFromArray(proto_blob.data(), proto_blob.size())) {
    const std::string error_message = "Failed to parse proto message.";
    LOG(ERROR) << from_here.ToString() << " " << error_message;
    return error_message;
  }
  return "";
}

}  // namespace

PrivateComputingAdaptor::PrivateComputingAdaptor(
    std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object)
    : org::chromium::PrivateComputingAdaptor(this),
      dbus_object_(std::move(dbus_object)) {}

void PrivateComputingAdaptor::RegisterAsync(
    brillo::dbus_utils::AsyncEventSequencer::CompletionAction
        completion_callback) {
  RegisterWithDBusObject(dbus_object_.get());
  dbus_object_->RegisterAsync(std::move(completion_callback));
}

std::vector<uint8_t> PrivateComputingAdaptor::SaveLastPingDatesStatus(
    const std::vector<uint8_t>& request_blob) {
  LOG(INFO) << "Save the last ping dates to file.";
  SaveStatusRequest request;
  std::string error_message = ParseProto(FROM_HERE, &request, request_blob);

  SaveStatusResponse response;
  if (!error_message.empty()) {
    response.set_error_message(error_message);
    return SerializeProto(response);
  }

  return SerializeProto(response);
}

std::vector<uint8_t> PrivateComputingAdaptor::GetLastPingDatesStatus() {
  LOG(INFO) << "Get the last ping dates from preserved file.";
  GetStatusResponse response;

  return SerializeProto(response);
}

}  // namespace private_computing
