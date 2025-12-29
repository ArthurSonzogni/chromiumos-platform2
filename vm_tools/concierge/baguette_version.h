// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
#define VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_

// This constant points to the image downloaded for new installations of
// Baguette.
// TODO(crbug.com/393151776): Point to luci recipe and builders that update this
// URL when new images are available.

// clang-format off
constexpr char kBaguetteVersion[] = "2025-12-29-000139_f31b041e8f67e52fefff93809f8cf0a58a1decd2";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "91ca3795f65d4df4f1946ab82b556e29d7a5369222a3ad0d4b54bbc97f3cc072";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "2c3c7cfebc3aa434d3f31cccddf42afd2988c5a9ce5267d41ebeac49670d53be";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
