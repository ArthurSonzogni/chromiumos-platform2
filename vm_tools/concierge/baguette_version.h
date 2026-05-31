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
constexpr char kBaguetteVersion[] = "2026-05-31-000135_611c26edd47598bd8768a32d800e0643b5c053f5";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "afb3a269e977db40cc6e5a763b409fe752671999ada409099b32bea5903c1a72";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "c5dda8621ed4159ed300fddd0c41dbaf1ea40413cd3c1fd538bac36ecacb6b10";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
