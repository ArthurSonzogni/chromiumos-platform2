// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_MOCK_VM_CONCIERGE_CLIENT_H_
#define PATCHPANEL_MOCK_VM_CONCIERGE_CLIENT_H_

#include <string>

#include <gmock/gmock.h>

#include "patchpanel/vm_concierge_client.h"

namespace patchpanel {

// Mock VM interface client for test.
class MockVmConciergeClient : public VmConciergeClient {
 public:
  MockVmConciergeClient();
  ~MockVmConciergeClient();

  MOCK_METHOD(bool, RegisterVm, (int64_t vm_cid), (override));

  MOCK_METHOD(bool,
              AttachTapDevice,
              (int64_t vm_cid,
               const std::string& tap_name,
               AttachTapCallback callback),
              (override));

  MOCK_METHOD(bool,
              DetachTapDevice,
              (int64_t vm_cid, uint32_t bus_num, DetachTapCallback callback),
              (override));
};
}  // namespace patchpanel

#endif  // PATCHPANEL_MOCK_VM_CONCIERGE_CLIENT_H_
