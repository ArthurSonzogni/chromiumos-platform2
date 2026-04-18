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
constexpr char kBaguetteVersion[] = "2026-04-18-000105_5db7cec12db5635da0218517fc5154b3098b38d6";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "4c0ad9beb750d5732312e0bc6733902f77e13637792275902157015610deef60";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "0c61bafa6b90ce719481614190e2b055658adf49efcfe63e695a754f78ca8c45";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
