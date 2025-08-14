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
constexpr char kBaguetteVersion[] = "2025-08-14-000108_0de19eef7db09a5febb9613f0ea4ef639672c106";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "b83cca3e5a8d7205745f12802e3bee71a614c8ff73b39af25dccd2a43f39ff67";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "b9e6ab53b8f5736eff7f8cf053381d4d35340af1d4dd920eb7d1f75c6a35571a";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
