// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECAGENTD_TEST_MOCK_PROCESS_CACHE_H_
#define SECAGENTD_TEST_MOCK_PROCESS_CACHE_H_

#include <memory>
#include <vector>

#include "gmock/gmock-function-mocker.h"
#include "missive/proto/security_xdr_events.pb.h"
#include "secagentd/bpf/process.h"
#include "secagentd/process_cache.h"

namespace secagentd::testing {

class MockProcessCache : public ProcessCacheInterface {
 public:
  MOCK_METHOD(void,
              PutFromBpfExec,
              (const bpf::cros_process_start&),
              (override));
  MOCK_METHOD(std::vector<std::unique_ptr<cros_xdr::reporting::Process>>,
              GetProcessHierarchy,
              (uint64_t, bpf::time_ns_t, int),
              (override));
};

}  // namespace secagentd::testing

#endif  // SECAGENTD_TEST_MOCK_PROCESS_CACHE_H_
