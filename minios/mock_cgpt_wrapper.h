// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_MOCK_CGPT_WRAPPER_H_
#define MINIOS_MOCK_CGPT_WRAPPER_H_

#include <gmock/gmock.h>

#include "minios/cgpt_wrapper.h"

namespace minios {

class MockCgptWrapper : public CgptWrapperInterface {
 public:
  MockCgptWrapper() = default;
  ~MockCgptWrapper() override = default;

  MockCgptWrapper(const MockCgptWrapper&) = delete;
  MockCgptWrapper& operator=(const MockCgptWrapper&) = delete;

  MOCK_METHOD(void, CgptFind, (CgptFindParams * params), (const, override));
  MOCK_METHOD(int,
              CgptGetPartitionDetails,
              (CgptAddParams * params),
              (const, override));
};

}  // namespace minios

#endif  // MINIOS_MOCK_CGPT_WRAPPER_H_
