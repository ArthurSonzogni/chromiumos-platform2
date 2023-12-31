// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net-base/mock_proc_fs_stub.h"

namespace net_base {

namespace {
using ::testing::_;
using ::testing::Return;
}  // namespace

MockProcFsStub::MockProcFsStub(const std::string& interface_name)
    : ProcFsStub(interface_name) {
  ON_CALL(*this, SetIPFlag(_, _, _)).WillByDefault(Return(true));
}

MockProcFsStub::~MockProcFsStub() = default;

}  // namespace net_base
