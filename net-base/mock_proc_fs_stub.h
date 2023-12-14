// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_MOCK_PROC_FS_STUB_H_
#define NET_BASE_MOCK_PROC_FS_STUB_H_

#include <string>

#include <gmock/gmock.h>

#include "net-base/proc_fs_stub.h"

namespace net_base {

class NET_BASE_EXPORT MockProcFsStub : public ProcFsStub {
 public:
  explicit MockProcFsStub(const std::string& interface_name);
  MockProcFsStub(const MockProcFsStub&) = delete;
  MockProcFsStub& operator=(const MockProcFsStub&) = delete;
  ~MockProcFsStub() override;

  MOCK_METHOD(bool,
              SetIPFlag,
              (net_base::IPFamily, const std::string&, const std::string&),
              (override));

  MOCK_METHOD(bool, FlushRoutingCache, (), (override));
};
}  // namespace net_base

#endif  // NET_BASE_MOCK_PROC_FS_STUB_H_
