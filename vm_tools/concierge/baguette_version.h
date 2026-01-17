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
constexpr char kBaguetteVersion[] = "2026-01-17-000102_37053b1e6ec806d6d03c2d33d3faa76cf311126d";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "40c92a97ee3b5e34597e1403d0519b4d7bd6cebf05ffedaeff662b219fa7153e";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "d90be5114e852c3cf4ef311bc693fb2f7b8a331b91fc838088cda3183983eff3";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
