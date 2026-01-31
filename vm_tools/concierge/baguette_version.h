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
constexpr char kBaguetteVersion[] = "2026-01-31-000112_804c4019a664ca941b4d9e9c23960c54474fadab";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "c11555d3598ad9fe773f73b159ce366a617661e563b31f49cd04f7d33ed33813";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "23841588d204defb1f5896da8e2bc1a3809dfb2f60569053e9d3c46242114442";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
