// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBPREPROCESSOR_STORAGE_H_
#define FBPREPROCESSOR_STORAGE_H_

namespace fbpreprocessor {
constexpr char kDaemonStorageRoot[] = "/run/daemon-store/fbpreprocessord";
constexpr char kInputDirectory[] = "raw_dumps";
constexpr char kProcessedDirectory[] = "processed_dumps";
}  // namespace fbpreprocessor

#endif  // FBPREPROCESSOR_STORAGE_H_
