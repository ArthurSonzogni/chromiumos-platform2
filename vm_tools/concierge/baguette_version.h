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
constexpr char kBaguetteVersion[] = "2025-05-20-000122_9fb68b61bf1dbad73f366a9cd006ab11357e2f5b";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "dff8a7bb89a3f9eb601728fbc7aedd7e2f298766521d161f73e436144798342f";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "79d9983a3170a12cda2847ebc48de41e2e0258c842ad43d0baf561444ff74113";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
