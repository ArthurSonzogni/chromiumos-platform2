// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flex_hwis/http_sender.h"

#include <string>

#include <base/logging.h>
#include <brillo/http/http_request.h>
#include <brillo/http/http_utils.h>
#include <brillo/http/http_transport.h>
#include <brillo/mime_utils.h>

#include "flex_hwis/flex_hwis_server_info.h"

namespace flex_hwis {

constexpr char kApiVersion[] = "/v1/";

HttpSender::HttpSender(ServerInfo& server_info) : server_info_(server_info) {}

bool HttpSender::DeleteDevice(const hwis_proto::DeleteDevice& device_info) {
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
  if (!response || !response->IsSuccessful()) {
    LOG(WARNING) << "Failed to delete device";
    if (error) {
      LOG(WARNING) << error->GetMessage();
    }
    return false;
  }
  return true;
}

bool HttpSender::UpdateDevice(const hwis_proto::Device& device_info) {
  if (server_info_.GetServerUrl().empty()) {
    LOG(WARNING) << "flex_hwis_tool has no server configured";
    return false;
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
  if (!response || !response->IsSuccessful()) {
    LOG(WARNING) << "Failed to update device";
    if (error) {
      LOG(WARNING) << error->GetMessage();
    }
    return false;
  }
  return true;
}

DeviceRegisterResult HttpSender::RegisterNewDevice(
    const hwis_proto::Device& device_info) {
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
  if (!response || !response->IsSuccessful()) {
    LOG(WARNING) << "Failed to register a new device";
    if (error) {
      LOG(WARNING) << error->GetMessage();
    }
    register_result.success = false;
    return register_result;
  }

  hwis_proto::Device device_proto;
  device_proto.ParseFromString(response->ExtractDataAsString());
  register_result.device_name = device_proto.name();
  register_result.success = true;
  return register_result;
}

}  // namespace flex_hwis
