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
constexpr char kBaguetteVersion[] = "2025-07-20-000124_459883c04884e13e7db3623bd4e0f1a5588638a7";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "9a9a86f6814ca9ea76279248ba4e056b6821cc8204bbf25be86642b7d0005398";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "5ac8fb078039b3a441479fc714999676d22fd30aab52b9ae7ae2d677c5622fef";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
