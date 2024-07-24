// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BPF_MONS_CONTRIB_MINION_DECODER_H_
#define BPF_MONS_CONTRIB_MINION_DECODER_H_

namespace minion_decoder {

void decode_ustack(pid_t pid, uintptr_t* ents, uint32_t num_ents);
void decode_kstack(uintptr_t* ents, uint32_t num_ents);

}  // namespace minion_decoder

#endif  // BPF_MONS_CONTRIB_MINION_DECODER_H_
