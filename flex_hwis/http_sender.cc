// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flex_hwis/http_sender.h"

#include <memory>
#include <string>

#include <base/logging.h>
#include <brillo/http/http_request.h>
#include <brillo/http/http_utils.h>
#include <brillo/http/http_transport.h>
#include <brillo/mime_utils.h>

#include "flex_hwis/flex_hwis_server_info.h"

namespace flex_hwis {

namespace {
bool FailedBecauseDeviceNotFoundOnServer(
    std::unique_ptr<brillo::http::Response>& response) {
  const auto response_content = response->ExtractDataAsString();
  const std::string device_not_found_msg_from_server =
      "Requested entity was not found.";
  if (response->GetStatusCode() == /*not found error=*/404 &&
      response_content.find(device_not_found_msg_from_server) !=
          std::string::npos) {
    LOG(INFO) << "Device was not found on the server";
    return true;
  } else {
    LOG(WARNING) << "Send HTTP request failed with error: " << response_content;
    return false;
  }
}
}  // namespace

constexpr char kApiVersion[] = "/v1/";
// TODO(b/309690409): Create unit tests to verify network interactions.
HttpSender::HttpSender(ServerInfo& server_info) : server_info_(server_info) {}

bool HttpSender::DeleteDevice(const hwis_proto::DeleteDevice& device_info) {
  LOG(INFO) << "Delete a device on server";
  if (server_info_.GetServerUrl().empty()) {
    LOG(WARNING) << "flex_hwis_tool has no server configured";
    return false;
  }
  brillo::ErrorPtr error;
  const brillo::http::HeaderList kApiHeaders = {
      {"X-Goog-Api-Key", server_info_.GetApiKey()}};
  auto response = brillo::http::SendRequestAndBlock(
      brillo::http::request_type::kDelete,
      server_info_.GetServerUrl() + kApiVersion + device_info.name(),
      /*data=*/nullptr,
      /*data_size=*/0, brillo::mime::application::kProtobuf, kApiHeaders,
      brillo::http::Transport::CreateDefault(), &error);
  if (!response) {
    if (error) {
      LOG(WARNING) << "Delete device failed with error: "
                   << error->GetMessage();
    }
    return false;
  }
  if (!response->IsSuccessful()) {
    // If the device to be deleted is not found on the server, the
    // deletion is considered successful.
    return FailedBecauseDeviceNotFoundOnServer(response);
  }
  return true;
}

DeviceUpdateResult HttpSender::UpdateDevice(
    const hwis_proto::Device& device_info) {
  LOG(INFO) << "Update a device on server";
  if (server_info_.GetServerUrl().empty()) {
    LOG(WARNING) << "flex_hwis_tool has no server configured";
    return DeviceUpdateResult::Fail;
  }
  brillo::ErrorPtr error;
  const brillo::http::HeaderList kApiHeaders = {
      {"X-Goog-Api-Key", server_info_.GetApiKey()}};
  auto response = brillo::http::SendRequestAndBlock(
      brillo::http::request_type::kPatch,
      server_info_.GetServerUrl() + kApiVersion + device_info.name(),
      device_info.SerializeAsString().c_str(),
      device_info.SerializeAsString().size(),
      brillo::mime::application::kProtobuf, kApiHeaders,
      brillo::http::Transport::CreateDefault(), &error);
  if (!response) {
    if (error) {
      LOG(WARNING) << "Update device failed with error: "
                   << error->GetMessage();
    }
    return DeviceUpdateResult::Fail;
  }
  if (!response->IsSuccessful()) {
    if (FailedBecauseDeviceNotFoundOnServer(response)) {
      return DeviceUpdateResult::DeviceNotFound;
    }
    // Most errors in update requests are related to the data content and
    // format. Therefore, the request body is logged.
    LOG(WARNING) << "Update device failed with request body: "
                 << device_info.DebugString();
    return DeviceUpdateResult::Fail;
  }

  return DeviceUpdateResult::Success;
}

DeviceRegisterResult HttpSender::RegisterNewDevice(
    const hwis_proto::Device& device_info) {
  LOG(INFO) << "Register a device on server";
  if (server_info_.GetServerUrl().empty()) {
    LOG(WARNING) << "flex_hwis_tool has no server configured";
    return DeviceRegisterResult(/*success=*/false, /*device_name=*/"");
  }
  brillo::ErrorPtr error;
  const brillo::http::HeaderList kApiHeaders = {
      {"X-Goog-Api-Key", server_info_.GetApiKey()}};
  auto response = brillo::http::PostBinaryAndBlock(
      server_info_.GetServerUrl() + kApiVersion + "devices",
      device_info.SerializeAsString().c_str(),
      device_info.SerializeAsString().size(),
      brillo::mime::application::kProtobuf, kApiHeaders,
      brillo::http::Transport::CreateDefault(), &error);

  DeviceRegisterResult register_result;
  register_result.success = false;

  if (response) {
    if (response->IsSuccessful()) {
      hwis_proto::Device device_proto;
      device_proto.ParseFromString(response->ExtractDataAsString());
      register_result.device_name = device_proto.name();
      register_result.success = true;
    } else {
      LOG(WARNING) << "Register device failed with error: "
                   << response->ExtractDataAsString();
    }
  } else if (error) {
    LOG(WARNING) << "Register device failed with error: "
                 << error->GetMessage();
  }
  return register_result;
}

}  // namespace flex_hwis
