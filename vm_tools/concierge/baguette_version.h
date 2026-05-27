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
constexpr char kBaguetteVersion[] = "2026-05-27-000101_ca947c3b7c31ac865c0e9f77739990c22d87a4a4";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "af3768e90cdd3aa66530f5c91f0906daf9f379dd69a53c638748ab8a2595da25";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "e8d3687050551ec557e3964da997e180d4911ea2b8c4b2d89dcf7da57c9741da";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
