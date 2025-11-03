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
constexpr char kBaguetteVersion[] = "2025-11-03-000118_1e4774f45b456fe00843279a5338852bd480b410";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "53069185dee19c6414c36e7c60caa8e692b914f66e518d9180fab21c9553b91b";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "1103394cf1b2324451daf90ae65008e9fd7e84478b56132f0a029cfdc92a5aab";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
