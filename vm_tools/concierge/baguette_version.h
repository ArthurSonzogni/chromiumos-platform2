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
constexpr char kBaguetteVersion[] = "2026-05-24-000129_9aa945e6776a210cf927e93efb7149acc40d473e";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "3f3572efdb0d238b47f75817a909bbc52fc052f34bbc28c3efa2497da99e2af6";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "9e58a3119d751e0a5706eb1dc1e9b13abb1ef11b2bf08d6eb6527e109f377559";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
