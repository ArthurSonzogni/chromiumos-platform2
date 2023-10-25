// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_BPF_CONSTANTS_H_
#define PATCHPANEL_BPF_CONSTANTS_H_

// This file contains constants which should be shared between patchpanel and
// the bpf_loader.
namespace patchpanel {

// The pinned eBPF program for WebRTC detection which is supposed to be used by
// iptables.
constexpr char kWebRTCMatcherPinPath[] = "/run/patchpanel/bpf/match_dtls_srtp";

}  // namespace patchpanel

#endif  // PATCHPANEL_BPF_CONSTANTS_H_
