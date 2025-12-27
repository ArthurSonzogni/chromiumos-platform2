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
constexpr char kBaguetteVersion[] = "2025-12-27-000121_d35d9b25b236bd1521a41e8d48d38a9f3c83de30";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "35cd0f2d67e9b14a58f984db9fbcc61fd16cbf0e3e01b0441a516f1aa3f890ca";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "5f8ea8b0791c2a6572a88f2b9349c336b52a98e9dec0219b7336114ce2ecf826";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
