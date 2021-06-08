// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trunks/csme/mei_client_char_device.h"

#include <error.h>
#include <fcntl.h>
#include <linux/mei.h>
#include <linux/uuid.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cstring>
#include <memory>

#include <base/logging.h>

namespace trunks {
namespace csme {

MeiClientCharDevice::MeiClientCharDevice(const std::string& mei_path,
                                         const uuid_le& guid)
    : mei_path_(mei_path) {
  DCHECK(!mei_path_.empty());
  memcpy(&guid_, &guid, sizeof(guid));
}

MeiClientCharDevice::~MeiClientCharDevice() {
  Uninitialize();
}

bool MeiClientCharDevice::Initialize() {
  if (initialized_) {
    return true;
  }
  DCHECK_EQ(fd_, -1);

  if (!InitializeInternal()) {
    Uninitialize();
    return false;
  }

  initialized_ = true;

  return true;
}

void MeiClientCharDevice::Uninitialize() {
  if (fd_ != -1) {
    close(fd_);
    fd_ = -1;
  }
}

bool MeiClientCharDevice::Send(const std::string& data) {
  if (!initialized_ && !Initialize()) {
    LOG(ERROR) << __func__ << ": Not initialized.";
    return false;
  }
  if (data.size() > max_message_size_) {
    LOG(WARNING) << __func__ << ": Data size too large: " << data.size()
                 << ", shoud be less than " << max_message_size_;
  }
  ssize_t wsize = write(fd_, data.data(), data.size());
  if (wsize != data.size()) {
    LOG(ERROR) << __func__ << ": Bad written size of payload: " << wsize;
    return false;
  }
  return true;
}

bool MeiClientCharDevice::Receive(std::string* data) {
  if (!initialized_ && !Initialize()) {
    LOG(ERROR) << __func__ << ": Not initialized.";
    return false;
  }
  ssize_t rsize = read(fd_, message_buffer_.data(), max_message_size_);
  if (rsize < 0) {
    LOG(ERROR) << ": Error calling `read()`: " << errno;
  }
  data->assign(message_buffer_.begin(), message_buffer_.begin() + rsize);
  return true;
}

bool MeiClientCharDevice::InitializeInternal() {
  DCHECK_EQ(fd_, -1);

  fd_ = open(mei_path_.c_str(), O_RDWR);
  if (fd_ == -1) {
    LOG(ERROR) << __func__ << ": Error calling `open()`: " << errno;
    return false;
  }
  struct mei_connect_client_data data = {};
  memcpy(&data.in_client_uuid, &guid_, sizeof(guid_));

  int result = ioctl(fd_, IOCTL_MEI_CONNECT_CLIENT, &data);
  if (result) {
    LOG(ERROR) << __func__ << ": Error calling `ioctl()`: " << errno;
    Uninitialize();
    return false;
  }
  if (data.out_client_properties.max_msg_length <= 0) {
    LOG(DFATAL) << __func__ << ": Limit to message size too small.";
    return false;
  }

  max_message_size_ = data.out_client_properties.max_msg_length;
  message_buffer_.resize(max_message_size_);

  return true;
}

}  // namespace csme
}  // namespace trunks
