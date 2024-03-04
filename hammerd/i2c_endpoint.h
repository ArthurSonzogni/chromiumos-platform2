// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HAMMERD_I2C_ENDPOINT_H_
#define HAMMERD_I2C_ENDPOINT_H_

#include <string>

#include "hammerd/usb_utils.h"

namespace hammerd {

class I2CEndpoint : public UsbEndpointInterface {
 public:
  explicit I2CEndpoint(const std::string& i2c_path) : i2c_path_(i2c_path) {}

  ~I2CEndpoint() override { Close(); }

  // Check whether the sysfs file exist or not.
  bool UsbSysfsExists() override;

  // Initializes the endpoint.
  UsbConnectStatus Connect(bool check_id) override;

  // Releases endpoint.
  void Close() override;

  // Returns whether the endpoint is initialized.
  bool IsConnected() const override { return fd_ >= 0; }

  // Sends the data to endpoint and then reads the result back.
  // Returns the byte number of the received data. -1 if the process fails, or
  // if `allow_less` is false and the received data does not match outlen.
  int Transfer(const void* outbuf,
               int outlen,
               void* inbuf,
               int inlen,
               bool allow_less = false,
               unsigned int timeout_ms = 0) override {
    if (int err = Send(outbuf, outlen, allow_less, timeout_ms); err < 0) {
      return err;
    }
    return Receive(inbuf, inlen, allow_less, timeout_ms);
  }
  // Sends the data to endpoint.
  // Returns the byte number of the received data. -1 if the process fails, or
  // if `allow_less` is false and the received data does not match outlen.
  int Send(const void* outbuf,
           int outlen,
           bool allow_less = false,
           unsigned int timeout_ms = 0) override;

  int ReceiveNoWait(void* inbuf, int inlen);

  // Receives the data from endpoint.
  // Returns the byte number of the received data. -1 if the process fails, or
  // if `allow_less` is false and the received data does not match outlen.
  int Receive(void* inbuf,
              int inlen,
              bool allow_less = false,
              unsigned int timeout_ms = 0) override;

  // Gets the chunk length of the USB endpoint.
  int GetChunkLength() const override { return 48; }

  // Gets the configuration string of the endpoint.
  std::string GetConfigurationString() const override {
    return configuration_string_;
  }

 private:
  int fd_ = -1;
  uint32_t addr_;
  std::string i2c_path_;
  std::string configuration_string_;
};

}  // namespace hammerd

#endif  // HAMMERD_I2C_ENDPOINT_H_
