// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_MOCK_CGPT_UTIL_H_
#define MINIOS_MOCK_CGPT_UTIL_H_

#include "minios/cgpt_util_interface.h"

#include <gmock/gmock.h>
#include <string>

namespace minios {

class MockCgptUtil : public CgptUtilInterface {
 public:
  MOCK_METHOD(std::optional<int>,
              GetPartitionNumber,
              (const std::string& label),
              (const, override));
  MOCK_METHOD(std::optional<uint64_t>,
              GetSize,
              (const uint32_t partition_number),
              (const, override));
};

}  // namespace minios

#endif  // MINIOS_MOCK_CGPT_UTIL_H_
