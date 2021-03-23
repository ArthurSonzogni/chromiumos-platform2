// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/encryption/scoped_encryption_feature.h"

#include <memory>
#include <utility>

#include <base/feature_list.h>

#include "missive/encryption/encryption_module_interface.h"

namespace reporting {
namespace test {

ScopedEncryptionFeature::ScopedEncryptionFeature(bool enable) {
  std::unique_ptr<base::FeatureList> feature_list(new base::FeatureList);
  if (enable) {
    feature_list->InitializeFromCommandLine(
        {EncryptionModuleInterface::kEncryptedReporting}, {});
  } else {
    feature_list->InitializeFromCommandLine(
        {}, {EncryptionModuleInterface::kEncryptedReporting});
  }
  original_feature_list_ = base::FeatureList::ClearInstanceForTesting();
  base::FeatureList::SetInstance(std::move(feature_list));
}

ScopedEncryptionFeature::~ScopedEncryptionFeature() {
  base::FeatureList::ClearInstanceForTesting();
  base::FeatureList::RestoreInstanceForTesting(
      std::move(original_feature_list_));
}

}  // namespace test
}  // namespace reporting
