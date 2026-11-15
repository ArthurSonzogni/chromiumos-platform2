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
constexpr char kBaguetteVersion[] = "2026-11-15-000128_c2c6eedd4d40a38bbd2428542793ffeff7716c90";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "9b819c47f56f113a5808bafa2e14778ec7947ae9a469227eb1ae71766e1a8404";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "4b5e70c5a422a3d1d8fd920ddc284867576471ffa90b4d386693fe9638e816d3";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
