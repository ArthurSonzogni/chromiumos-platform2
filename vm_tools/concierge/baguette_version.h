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
constexpr char kBaguetteVersion[] = "2026-04-13-000201_70ca604926f843743dacf2730ec692fa4b5257a0";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "304da690d32cef3c8e6a712cf88907efc2d5569d815d730adbc6c2a2a29aecb8";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "ce387c291b4c58e7bd8a74055a88ae0f0a8b046692c5b18c8b1feb8fc22c830f";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
