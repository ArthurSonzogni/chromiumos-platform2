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
constexpr char kBaguetteVersion[] = "2025-09-23-000120_16f4d9f3f01b27d770702c11f654ba54fc2288e4";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "3493661cddb13b167f9eb60c2dfe5297b49fd087a1b08445be601a3b07ce0dbb";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "eddc8db8bb9866f81b554bdf27d5eebe3460821f28690f73156ad46b7388c249";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
