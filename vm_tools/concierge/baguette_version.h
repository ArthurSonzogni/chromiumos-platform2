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
constexpr char kBaguetteVersion[] = "2025-05-31-000107_39b229ebb92f1af0246b7e26f3f7d1512b2657f3";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "e91045c1aba3e89baa12cc64145409ded13ab5e5767621ac496fd19d733941ce";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "49e87983ffdedd45780102c9a4e0f22a192e9524dc76692fa37975e8dfc6ee85";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
