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
constexpr char kBaguetteVersion[] = "2026-05-01-000123_873fdff1c4db10eb9dea6f351a5a87fa326e194b";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "53d31442bfcb0d5d4af6bbe748aa79fd0521244819e930d97ddbf25976bb5bb9";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "040fadfd34ca5418d6a7c718a46a2984c99dd50eca1db1d70ebf3c3f2bcd0894";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
