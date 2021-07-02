// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_SOMMELIER_SOMMELIER_TIMING_H_
#define VM_TOOLS_SOMMELIER_SOMMELIER_TIMING_H_

#include <time.h>

const int kUnknownBufferId = -1;
const int kUnknownSurfaceId = -1;

class Timing {
 public:
  explicit Timing(const char* fname) : filename(fname) {}
  void UpdateLastAttach(int surface_id, int buffer_id);
  void UpdateLastCommit(int surface_id);
  void UpdateLastRelease(int buffer_id);
  void OutputLog();

 private:
  // 10 min * 60 sec/min * 60 frames/sec * 3 actions/frame = 108000 actions
  static const int kMaxNumActions = 10 * 60 * 60 * 3;

  struct BufferAction {
    enum Type { UNKNOWN, ATTACH, COMMIT, RELEASE };
    timespec time;
    int surface_id;
    int buffer_id;
    Type action_type;
    BufferAction()
        : surface_id(kUnknownSurfaceId),
          buffer_id(kUnknownBufferId),
          action_type(UNKNOWN) {}
    explicit BufferAction(timespec t,
                          int sid = kUnknownSurfaceId,
                          int bid = kUnknownBufferId,
                          Type type = UNKNOWN)
        : time(t), surface_id(sid), buffer_id(bid), action_type(type) {}
  };

  BufferAction actions[kMaxNumActions];
  int actions_idx = 0;
  int saves = 0;
  const char* filename;
};      // class Timing
#endif  // VM_TOOLS_SOMMELIER_SOMMELIER_TIMING_H_
