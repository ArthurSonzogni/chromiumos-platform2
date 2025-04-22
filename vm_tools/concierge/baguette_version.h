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
constexpr char kBaguetteVersion[] = "2025-04-22-000103_75b31e5a44a4a6b4872f0af7616715c9a98aa2ef";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "84c1540bccdb23a6d3467adf47b49eec1168ed166658eb0de55e73423cbaad16";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "70a00cecf767fb4f3c13893ca1a7b05aa76580526fdc96002a54bcf6edd22ccf";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
