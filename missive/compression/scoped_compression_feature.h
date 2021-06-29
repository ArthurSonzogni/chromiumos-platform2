// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_COMPRESSION_SCOPED_COMPRESSION_FEATURE_H_
#define MISSIVE_COMPRESSION_SCOPED_COMPRESSION_FEATURE_H_

#include <memory>

#include <base/feature_list.h>

namespace reporting {
namespace test {

// Replacement of base::test::ScopedFeatureList which is unavailable in
// ChromeOS.
class ScopedCompressionFeature {
 public:
  explicit ScopedCompressionFeature(bool enable);

  ~ScopedCompressionFeature();

 private:
  std::unique_ptr<base::FeatureList> original_feature_list_;
};

}  // namespace test
}  // namespace reporting

#endif  // MISSIVE_COMPRESSION_SCOPED_COMPRESSION_FEATURE_H_
