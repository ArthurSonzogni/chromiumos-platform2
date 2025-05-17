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
constexpr char kBaguetteVersion[] = "2025-05-17-000120_9280ba7a5569d5d3fdd5a055eaeca91618e26d6d";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "67a19535512992a433d11db8926af0894ab7214052dcbe3845c7f56abbb9e91d";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "3722439b1c0565abe656d8e25229e2154215a4cf5da0a3f82e1388a1c88255a1";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
