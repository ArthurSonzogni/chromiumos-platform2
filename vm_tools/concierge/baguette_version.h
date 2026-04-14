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
constexpr char kBaguetteVersion[] = "2026-04-14-000108_24d88be675b51d34f16bb634f7d1aaddbaf1e6b0";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "c289765bb625970f3fefda8752b847b0062018d8fe9ad7e7d8bb27c91e6a0821";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "8f29486892712d550a6b5428c8821f48dd9592ffb584485b2757154240ae2b97";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
