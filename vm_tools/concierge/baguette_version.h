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
constexpr char kBaguetteVersion[] = "2026-05-12-000112_0e09dcc3bae0b5158496be8f64467dc7a8473180";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "13ce1cf33f5ab76c254bed08bfd37a7e0cdf141ab4c0251333409f7abc80b384";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "84b54c4be46ef800d57eda25eee0330775eaa6ee6c7b3e5da1fb7e8c09db48e5";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
