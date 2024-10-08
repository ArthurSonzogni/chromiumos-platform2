// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/network/mock_address_service.h"

#include "patchpanel/network/address_service.h"

namespace patchpanel {

MockAddressService::MockAddressService() : AddressService(nullptr) {}
MockAddressService::~MockAddressService() = default;

}  // namespace patchpanel
