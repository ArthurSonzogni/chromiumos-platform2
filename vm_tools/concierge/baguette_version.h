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
constexpr char kBaguetteVersion[] = "2026-11-11-000119_138cd27358b0c7aaeadc790457c3497a8ffbd04c";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "023f724305ed78545c2f7f54aa906aeace78a2fc408c10fac64e8bc4e36ca671";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "c141e8fb76ba28e80b3cf9570bfb7a558ff41967a289a8bc89fbf7be479c7976";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
