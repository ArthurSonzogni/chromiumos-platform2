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
constexpr char kBaguetteVersion[] = "2025-08-16-000058_bcc50496a272e371384938f9be1e16fe09f0793c";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "bcd6787b89d6749b83d31f97642f6d3972c39549cceb7f9aeab4b3ea21c6b4a9";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "a083ef4a5408404647630bccdadd2a82be673f51de531e20336945904bf98165";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
