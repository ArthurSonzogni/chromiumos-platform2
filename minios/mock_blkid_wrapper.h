// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_MOCK_BLKID_WRAPPER_H_
#define MINIOS_MOCK_BLKID_WRAPPER_H_

#include <string>

#include <gmock/gmock.h>

#include "minios/blkid_wrapper.h"

namespace minios {

class MockBlkIdWrapper : public BlkIdWrapperInterface {
 public:
  MockBlkIdWrapper() = default;

  MOCK_METHOD(bool,
              FindDevice,
              (const std::string& devname),
              (const, override));

  MOCK_METHOD(void, GetCache, (), (override));

  MOCK_METHOD(std::optional<std::string>,
              GetTagValue,
              (const std::string& tagname, const std::string& devname),
              (const, override));
};

}  // namespace minios

#endif  // MINIOS_MOCK_BLKID_WRAPPER_H_
