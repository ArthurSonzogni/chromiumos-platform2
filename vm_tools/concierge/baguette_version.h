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
constexpr char kBaguetteVersion[] = "2025-06-20-000123_9b61a8436c54a872979688fb3da2c4b141379f35";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "d44677864c427f540e88c6933cb4c239f1f65e36184da635a2ae61f0b742236a";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "7a5a433daf0b1d4870a8b183682c631d7a874ce05ee1b06e6649993b24a6b860";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
