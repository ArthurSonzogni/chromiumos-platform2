// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef OOBE_CONFIG_FEATURES_FEATURES_H_
#define OOBE_CONFIG_FEATURES_FEATURES_H_

namespace oobe_config {

// Returns true if the feature to run TPM-based encryption is enabled.
bool TpmEncryptionFeatureEnabled();

}  // namespace oobe_config

#endif  // OOBE_CONFIG_FEATURES_FEATURES_H_
