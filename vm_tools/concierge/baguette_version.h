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
constexpr char kBaguetteVersion[] = "2026-01-18-000130_e7c1fc0bcf642b4614bb27a9ee22697746ed6881";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "b6a358579ec0eacbe676b3299fdd0ac238dac8585376a36e0c12ed28412104cf";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "5621a19d1cd0bd73420955be4d641132023c688a0f8b89ec4592dbef12c04b11";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
