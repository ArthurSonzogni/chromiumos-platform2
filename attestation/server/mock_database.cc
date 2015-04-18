// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "attestation/server/mock_database.h"

using testing::Return;
using testing::ReturnRef;

namespace attestation {

MockDatabase::MockDatabase() {
  ON_CALL(*this, GetProtobuf()).WillByDefault(ReturnRef(fake_));
  ON_CALL(*this, GetMutableProtobuf()).WillByDefault(Return(&fake_));
  ON_CALL(*this, SaveChanges()).WillByDefault(Return(true));
}

MockDatabase::~MockDatabase() {}

}  // namespace attestation
