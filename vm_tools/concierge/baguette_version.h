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
constexpr char kBaguetteVersion[] = "2025-10-27-000137_010c492479e81b4fe37a47f902c8c8c95b42b745";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "57589ab00f5428a92002224a8653617d3bc35c8a1dfe2f14a23869a26bdbb502";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "dec3daee9b38055fcea46a43d098d0b7ab2349d7b6d5f5bd7809d1c8c7626d41";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
