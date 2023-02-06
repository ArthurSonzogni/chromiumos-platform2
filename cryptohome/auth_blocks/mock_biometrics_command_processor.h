// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_MOCK_BIOMETRICS_COMMAND_PROCESSOR_H_
#define CRYPTOHOME_AUTH_BLOCKS_MOCK_BIOMETRICS_COMMAND_PROCESSOR_H_

#include "cryptohome/auth_blocks/biometrics_command_processor.h"

#include <gmock/gmock.h>

namespace cryptohome {

class MockBiometricsCommandProcessor : public BiometricsCommandProcessor {
 public:
  MockBiometricsCommandProcessor() = default;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_MOCK_BIOMETRICS_COMMAND_PROCESSOR_H_
