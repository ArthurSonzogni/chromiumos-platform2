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
constexpr char kBaguetteVersion[] = "2026-10-19-000118_e9be23e08166f6ed79d101e63d35b6b2e37b5c8e";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "98bdab5a5ae50d80d7baf37eaf8991dc5fb13fdc03e5a63220a722ef39ed1bd6";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "82c0c51e4be2ff02fcd4c5a3fa4493eb43c7f7cc08a7c1ae6917dae16cd07cdb";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
