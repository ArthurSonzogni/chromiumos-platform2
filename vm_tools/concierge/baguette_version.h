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
constexpr char kBaguetteVersion[] = "2026-06-29-000156_3b8dbfb0ba6bca3032dcbf78c12f609ed6a8df89";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "f11a5dd3b8775b5df96467560596f3da4befca547ed87259b9e8e912c5087ddb";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "434e352ece222b11ed43529975e684416867e890cb0847f14d0e142aa37f3440";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
