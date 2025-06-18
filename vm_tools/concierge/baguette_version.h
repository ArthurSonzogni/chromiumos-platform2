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
constexpr char kBaguetteVersion[] = "2025-06-18-000139_0d80e9e7a327f69e13896d920aefc3f306d592c2";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "fec1f616472c371197088ecdbb5dcfbee34dbcd949b29a26cb41cfdaa8f6e7ec";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "d4759bd9e11e02eb92e174cb6371eab67ebb51ca57bdf100d02aa808c52090b8";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
