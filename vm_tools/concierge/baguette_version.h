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
constexpr char kBaguetteVersion[] = "2025-12-25-000133_b6ec07c8a84da2a3e1b207944297865c9389eef6";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "8c7b78288b60df68926099635afe9c1bb6ea6781d3adb19488c2545a80bbfbdf";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "9c08c49b071e9b6d5f9e41b16bda967e54a100ee1926452830044c2e4cd5d28f";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
