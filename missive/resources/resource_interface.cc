// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/resources/resource_interface.h"

#include <utility>

#include <cstdint>

namespace reporting {

ScopedReservation::ScopedReservation(uint64_t size,
                                     ResourceInterface* resource_interface)
    : resource_interface_(resource_interface) {
  if (!resource_interface->Reserve(size)) {
    return;
  }
  size_ = size;
}

ScopedReservation::ScopedReservation(ScopedReservation&& other)
    : resource_interface_(other.resource_interface_),
      size_(std::move(other.size_)) {}

bool ScopedReservation::reserved() const {
  return size_.has_value();
}

ScopedReservation::~ScopedReservation() {
  if (reserved()) {
    resource_interface_->Discard(size_.value());
  }
}

}  // namespace reporting
