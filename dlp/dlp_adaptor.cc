// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlp/dlp_adaptor.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/location.h>
#include <brillo/dbus/dbus_object.h>
#include <brillo/errors/error.h>
#include <google/protobuf/message_lite.h>
#include <session_manager/dbus-proxies.h>

#include "dlp/proto_bindings/dlp_service.pb.h"

namespace dlp {

namespace {

// Serializes |proto| to a vector of bytes. CHECKs for success (should
// never fail if there are no required proto fields).
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

// Calls Session Manager to get the user hash for the primary session. Returns
// an empty string and logs on error.
std::string GetSanitizedUsername(brillo::dbus_utils::DBusObject* dbus_object) {
  std::string username;
  std::string sanitized_username;
  brillo::ErrorPtr error;
  org::chromium::SessionManagerInterfaceProxy proxy(dbus_object->GetBus());
  if (!proxy.RetrievePrimarySession(&username, &sanitized_username, &error)) {
    const char* error_msg =
        error ? error->GetMessage().c_str() : "Unknown error.";
    LOG(ERROR) << "Call to RetrievePrimarySession failed. " << error_msg;
    return std::string();
  }
  return sanitized_username;
}

}  // namespace

DlpAdaptor::DlpAdaptor(
    std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object)
    : org::chromium::DlpAdaptor(this), dbus_object_(std::move(dbus_object)) {
  InitDatabase();
}

DlpAdaptor::~DlpAdaptor() = default;

void DlpAdaptor::RegisterAsync(
    const brillo::dbus_utils::AsyncEventSequencer::CompletionAction&
        completion_callback) {
  RegisterWithDBusObject(dbus_object_.get());
  dbus_object_->RegisterAsync(completion_callback);
}

std::vector<uint8_t> DlpAdaptor::SetDlpFilesPolicy(
    const std::vector<uint8_t>& request_blob) {
  LOG(INFO) << "Received DLP files policy.";

  SetDlpFilesPolicyRequest request;
  std::string error_message = ParseProto(FROM_HERE, &request, request_blob);

  SetDlpFilesPolicyResponse response;
  if (!error_message.empty()) {
    response.set_error_message(error_message);
    return SerializeProto(response);
  }

  policy_rules_ =
      std::vector<DlpFilesRule>(request.rules().begin(), request.rules().end());

  return SerializeProto(response);
}

void DlpAdaptor::InitDatabase() {
  const std::string sanitized_username =
      GetSanitizedUsername(dbus_object_.get());
  if (sanitized_username.empty()) {
    LOG(ERROR) << "No active user, can't open the database";
    return;
  }
  const base::FilePath database_path = base::FilePath("/run/daemon-store/dlp/")
                                           .Append(sanitized_username)
                                           .Append("database");

  LOG(INFO) << "Opening database in: " << database_path.value();
  leveldb::Options options;
  options.create_if_missing = true;
  options.paranoid_checks = true;
  leveldb::DB* db = nullptr;
  leveldb::Status status =
      leveldb::DB::Open(options, database_path.value(), &db);

  if (!status.ok()) {
    LOG(ERROR) << "Failed to open database: " << status.ToString();
    status = leveldb::RepairDB(database_path.value(), leveldb::Options());
    if (status.ok())
      status = leveldb::DB::Open(options, database_path.value(), &db);
  }

  if (!status.ok()) {
    LOG(ERROR) << "Failed to repair database: " << status.ToString();
    return;
  }

  db_.reset(db);
}

}  // namespace dlp
