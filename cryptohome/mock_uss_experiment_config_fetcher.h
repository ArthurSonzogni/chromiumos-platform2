// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_MOCK_USS_EXPERIMENT_CONFIG_FETCHER_H_
#define CRYPTOHOME_MOCK_USS_EXPERIMENT_CONFIG_FETCHER_H_

#include "cryptohome/uss_experiment_config_fetcher.h"

namespace cryptohome {

class MockUssExperimentConfigFetcher : public UssExperimentConfigFetcher {
 public:
  MockUssExperimentConfigFetcher() = default;
  virtual ~MockUssExperimentConfigFetcher() = default;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_MOCK_USS_EXPERIMENT_CONFIG_FETCHER_H_
