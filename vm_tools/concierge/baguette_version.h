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
constexpr char kBaguetteVersion[] = "2026-07-24-000125_9125500defe0573b2003aa5337ef042ee57bba2d";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "90b1d628a3e66344a4432694694ba781a5cc772f93bf25d10e47692aa4f7ee4a";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "6fef0a3978cecc4193f9ac01afb00e82ccaf90fcb91ae3afce09ce6c131bceb2";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
