// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_SYSTEM_FAKE_SHILL_CLIENT_H_
#define RMAD_SYSTEM_FAKE_SHILL_CLIENT_H_

#include "rmad/system/shill_client.h"

namespace rmad {
namespace fake {

class FakeShillClient : public ShillClient {
 public:
  FakeShillClient() = default;
  FakeShillClient(const FakeShillClient&) = delete;
  FakeShillClient& operator=(const FakeShillClient&) = delete;
  ~FakeShillClient() override = default;

  bool DisableCellular() const override { return true; }
};

}  // namespace fake
}  // namespace rmad

#endif  // RMAD_SYSTEM_FAKE_SHILL_CLIENT_H_
