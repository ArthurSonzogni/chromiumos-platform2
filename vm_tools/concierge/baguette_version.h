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
constexpr char kBaguetteVersion[] = "2026-03-06-000111_9e6300aaa836d5532114b56a8f092724fb005a81";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "f734d4af27fafe02b7c7aec2e729841e275ca9cfdbfe5f3cd84637c7301f914f";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "effbbea1d3abd8235df80afef5ed07706e39ffd7697523688f45aea5992c5fc2";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
