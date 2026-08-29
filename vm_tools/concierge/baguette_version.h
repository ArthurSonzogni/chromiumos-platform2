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
constexpr char kBaguetteVersion[] = "2026-08-29-000032_e3646ad6703a67b157afdf8421314be521c7fe30";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "27e4cad4dd0027c0c61bdace449e0069a074ec9306829b919f71f46bae09d39f";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "8780237aa20f289c7ba20228fa8e3a2b6071f99ae52d27fea2c8407528c284a2";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
