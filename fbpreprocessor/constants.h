// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBPREPROCESSOR_CONSTANTS_H_
#define FBPREPROCESSOR_CONSTANTS_H_

namespace fbpreprocessor {
// VLOG verbosity used for local debugging. The log level is its opposite value.
// E.g. VLOG(kLocalDebugVerbosity) has a log level of -kLocalDebugVerbosity.
constexpr int kLocalDebugVerbosity = 2;
// VLOG verbosity used ONLY for local debugging with test accounts. This
// verbosity level could log data such as individual file names.
constexpr int kLocalOnlyDebugVerbosity = 4;
}  // namespace fbpreprocessor

#endif  // FBPREPROCESSOR_CONSTANTS_H_
