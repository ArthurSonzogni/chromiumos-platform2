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
constexpr char kBaguetteVersion[] = "2026-08-18-000107_361223eaafdfcde9a02585ed9c5c33707c06ac9f";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "6e42319927c98ac4b3957b411dc62b299b600fd8ee004f1c524c45114e57664e";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "50b6a28d7a0e5d04cee481acc73c68f8fd80e5da7c713f88e4b83c5f350c1bf4";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
