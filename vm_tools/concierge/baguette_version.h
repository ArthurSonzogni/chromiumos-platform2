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
constexpr char kBaguetteVersion[] = "2025-09-14-000122_f8a4dfb00c3c9b873c4fae898be726cc2f73a144";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "4c14d0b3ed291471f64ce33b00b5ea783d503a7baf86c3ef8639dd6d115c8cd1";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "f61a533999aaeca621a5a8811df3b9075432a736dd9447f6d234485e4156b140";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
