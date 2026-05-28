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
constexpr char kBaguetteVersion[] = "2026-05-28-000107_1b7a3e54d8c660cddc38d7869af656d801d22b19";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "4d9715baf7f3e3952800052fde58c6504f31fd15d5e1970c8213b5249ea82606";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "74b1797d4ea74e0077521f6098ed9f354fa8fb9ec74200fd467dd539ea8e40fd";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
