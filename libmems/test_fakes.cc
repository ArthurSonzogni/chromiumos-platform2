// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libmems/test_fakes.h"

#include <base/logging.h>

namespace libmems {
namespace fakes {

FakeIioChannel::FakeIioChannel(const std::string& id, bool enabled)
    : id_(id), enabled_(enabled) {}

bool FakeIioChannel::SetEnabled(bool en) {
  enabled_ = en;
  return true;
}

template <typename T> base::Optional<T> FakeReadAttributes(
    const std::string& name,
    std::map<std::string, T> attributes) {
  auto k = attributes.find(name);
  if (k == attributes.end())
    return base::nullopt;
  return k->second;
}

base::Optional<std::string> FakeIioChannel::ReadStringAttribute(
    const std::string& name) const {
  return FakeReadAttributes<>(name, text_attributes_);
}
base::Optional<int64_t> FakeIioChannel::ReadNumberAttribute(
    const std::string& name) const {
  return FakeReadAttributes<>(name, numeric_attributes_);
}

void FakeIioChannel::WriteStringAttribute(const std::string& name,
                                         const std::string& value) {
  text_attributes_[name] = value;
}
void FakeIioChannel::WriteNumberAttribute(const std::string& name,
                                         int64_t value) {
  numeric_attributes_[name] = value;
}

FakeIioDevice::FakeIioDevice(FakeIioContext* ctx,
                             const std::string& name,
                             const std::string& id)
    : IioDevice(), context_(ctx), name_(name), id_(id) {}

base::Optional<std::string> FakeIioDevice::ReadStringAttribute(
    const std::string& name) const {
  return FakeReadAttributes<>(name, text_attributes_);
}
base::Optional<int64_t> FakeIioDevice::ReadNumberAttribute(
    const std::string& name) const {
  return FakeReadAttributes<>(name, numeric_attributes_);
}
base::Optional<double> FakeIioDevice::ReadDoubleAttribute(
    const std::string& name) const {
  return FakeReadAttributes<>(name, double_attributes_);
}

bool FakeIioDevice::WriteStringAttribute(const std::string& name,
                                         const std::string& value) {
  text_attributes_[name] = value;
  return true;
}
bool FakeIioDevice::WriteNumberAttribute(const std::string& name,
                                         int64_t value) {
  numeric_attributes_[name] = value;
  return true;
}
bool FakeIioDevice::WriteDoubleAttribute(const std::string& name,
                                         double value) {
  double_attributes_[name] = value;
  return true;
}

bool FakeIioDevice::SetTrigger(IioDevice* trigger) {
  trigger_ = trigger;
  return true;
}

IioChannel* FakeIioDevice::GetChannel(const std::string& id) {
  auto k = channels_.find(id);
  if (k == channels_.end())
    return nullptr;
  return k->second;
}

bool FakeIioDevice::EnableBuffer(size_t n) {
  buffer_length_ = n;
  buffer_enabled_ = true;
  return true;
}
bool FakeIioDevice::DisableBuffer() {
  buffer_enabled_ = false;
  return true;
}
bool FakeIioDevice::IsBufferEnabled(size_t* n) const {
  if (n && buffer_enabled_)
    *n = buffer_length_;
  return buffer_enabled_;
}

void FakeIioContext::AddDevice(FakeIioDevice* device) {
  CHECK(device);
  devices_.emplace(device->GetName(), device);
  devices_.emplace(device->GetId(), device);
}

IioDevice* FakeIioContext::GetDevice(const std::string& name) {
  auto k = devices_.find(name);
  return (k == devices_.end()) ? nullptr : k->second;
}

}  // namespace fakes
}  // namespace libmems
