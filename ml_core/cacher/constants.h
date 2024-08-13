// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ML_CORE_CACHER_CONSTANTS_H_
#define ML_CORE_CACHER_CONSTANTS_H_

namespace cros {
constexpr char kOpenCLCachingDir[] = "/var/lib/ml_core/opencl_cache";
#if USE_INTEL_OPENVINO_DELEGATE
constexpr char kOpenVinoCachingDir[] = "/var/lib/ml_core/openvino_cache";
#endif
}  // namespace cros

#endif  // ML_CORE_CACHER_CONSTANTS_H_
