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
constexpr char kBaguetteVersion[] = "2025-11-16-000115_c9666cf7e4f62d7e519cb102c288710bd819eab4";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "d6ccd1c503fa816862777c8f8658daf05edfc7dca1c1a3fe7bf7a4fa19740926";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "a047deae51035ce3fd29a602e67665a34498f92264621813cc29cf5732852024";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
