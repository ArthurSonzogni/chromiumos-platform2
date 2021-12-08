// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/mojom/external/time_mojom_data_generators.h"

namespace diagnostics {

base::Time BaseTimeGenerator::Generate() {
  has_next_ = false;
  return base::Time::UnixEpoch();
}

base::TimeDelta BaseTimeDeltaGenerator::Generate() {
  has_next_ = false;
  return base::TimeDelta::FromSeconds(1);
}

}  // namespace diagnostics
