// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLEX_HWIS_HTTP_SENDER_H_
#define FLEX_HWIS_HTTP_SENDER_H_

#include "flex_hwis/hwis_data.pb.h"

#include <string>

namespace flex_hwis {
class DeviceRegisterResult {
 public:
  bool success = false;
  std::string device_name;
};

// Sender implemented using brillo HTTP library.
class HttpSender {
 public:
  HttpSender() = default;
  explicit HttpSender(std::string server_url);
  HttpSender(const HttpSender&) = delete;
  HttpSender& operator=(const HttpSender&) = delete;

  virtual ~HttpSender() {}
  // Send a delete request to the HWIS server to delete the hardware
  // data if the user does not grant permission and there is a device
  // name on the client side.
  virtual bool DeleteDevice(const hwis_proto::DeleteDevice& device_info);
  // Send a post request to the HWIS server to create a new hardware
  // information entry in the database if the device name doesn’t exist
  // on the client side.
  virtual DeviceRegisterResult RegisterNewDevice(
      const hwis_proto::Device& device_info);
  // Send a put request to the HWIS server to replace an existing device
  // entry in the database if the device name exists on the client side.
  virtual bool UpdateDevice(const hwis_proto::Device& device_info);

 private:
  const std::string server_url_;
};
}  // namespace flex_hwis

#endif  // FLEX_HWIS_HTTP_SENDER_H_
