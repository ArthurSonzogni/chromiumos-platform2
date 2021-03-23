// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_ENCRYPTION_SCOPED_ENCRYPTION_FEATURE_H_
#define MISSIVE_ENCRYPTION_SCOPED_ENCRYPTION_FEATURE_H_

#include <memory>

#include <base/feature_list.h>

namespace reporting {
namespace test {

// Replacement of base::test::ScopedFeatureList which is unavailable in
// ChromeOS.
class ScopedEncryptionFeature {
 public:
  explicit ScopedEncryptionFeature(bool enable);

  ~ScopedEncryptionFeature();

 private:
  std::unique_ptr<base::FeatureList> original_feature_list_;
};

}  // namespace test
}  // namespace reporting

#endif  // MISSIVE_ENCRYPTION_SCOPED_ENCRYPTION_FEATURE_H_
