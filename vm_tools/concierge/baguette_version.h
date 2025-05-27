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
constexpr char kBaguetteVersion[] = "2025-05-27-000138_39b0318bdb2b18d29daa37a5fe6b063457e6df1b";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "66e63948251e0596772eeefc0db70f1bba883f3e41a7b09132848bf9b14886e2";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "40acded2f9c56c012a818cda5f7a98ca9898cd99cad3cb709be5201e090b766e";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
