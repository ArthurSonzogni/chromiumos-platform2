// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VTPM_BACKENDS_MOCK_NV_SPACE_MANAGER_H_
#define VTPM_BACKENDS_MOCK_NV_SPACE_MANAGER_H_

#include <string>

#include <gmock/gmock.h>
#include <trunks/tpm_generated.h>

namespace vtpm {

class MockNvSpaceManager : public NvSpaceManager {
 public:
  virtual ~MockNvSpaceManager() = default;

  MOCK_METHOD(trunks::TPM_RC,
              Read,
              (trunks::TPM_NV_INDEX, const std::string&, std::string&),
              (override));
};

}  // namespace vtpm

#endif  // VTPM_BACKENDS_MOCK_NV_SPACE_MANAGER_H_
