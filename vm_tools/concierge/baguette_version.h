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
constexpr char kBaguetteVersion[] = "2025-04-07-000129_c8d1492ac33083bc403cebe874ae511e9e635f7b";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "d8c8e7a6060b37f37919700fa7b56b651139e4bd3d21138e382b8f90870c42ca";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "cac5f07048f6b13d38550665b999925ddd8770d13edb824d682472bde2367941";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
