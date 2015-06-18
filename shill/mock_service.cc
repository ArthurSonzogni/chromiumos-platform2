// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/mock_service.h"

#include <string>

#include <base/memory/ref_counted.h>
#include <base/strings/stringprintf.h>
#include <gmock/gmock.h>

#include "shill/refptr_types.h"
#include "shill/store_interface.h"
#include "shill/technology.h"

using std::string;
using testing::_;
using testing::Return;
using testing::ReturnRef;

namespace shill {

class ControlInterface;
class EventDispatcher;
class Manager;

MockService::MockService(ControlInterface* control_interface,
                         EventDispatcher* dispatcher,
                         Metrics* metrics,
                         Manager* manager)
    : Service(control_interface, dispatcher, metrics, manager,
              Technology::kUnknown) {
  const string& id = unique_name();
  EXPECT_CALL(*this, GetRpcIdentifier()).WillRepeatedly(Return(id));
  EXPECT_CALL(*this, GetStorageIdentifier()).WillRepeatedly(Return(id));
  ON_CALL(*this, IsVisible()).WillByDefault(Return(true));
  ON_CALL(*this, state()).WillByDefault(Return(kStateUnknown));
  ON_CALL(*this, failure()).WillByDefault(Return(kFailureUnknown));
  ON_CALL(*this, technology()).WillByDefault(Return(Technology::kUnknown));
  ON_CALL(*this, connection()).WillByDefault(ReturnRef(mock_connection_));
}

MockService::~MockService() {}

bool MockService::FauxSave(StoreInterface* store) {
  return store->SetString(GetStorageIdentifier(), "dummy", "dummy");
}

}  // namespace shill
