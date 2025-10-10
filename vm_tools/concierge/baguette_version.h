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
constexpr char kBaguetteVersion[] = "2025-10-10-000354_0735f20c33aa51727028fe345d82a9e2e5ef7bb9";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "ee0c11fffd01c98cf55cf00ace220b05deeb2401eba10d3721b07ff4c979c25a";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "1c6c13d1b49e546e725a702a6c80e73a7dd080c4fe4fc77c83b914e092418112";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
