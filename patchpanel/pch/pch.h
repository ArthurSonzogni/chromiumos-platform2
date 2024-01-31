// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if defined(PATCHPANEL_PCH_H_)
#error You should not include this header directly in the code.
#endif

#define PATCHPANEL_PCH_H_

// This is the precompiled header for building patchpanel.
// - This header will be prepend to each cc file directly by the compiler, so
//   the code should not include this header directly.
// - It's better not to include any patchpanel headers here, since any change to
//   the included header would trigger a full rebuild, which is not desired.

// C standard library headers used in patchpanel.
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// C++ standard library headers used in patchpanel.
#include <algorithm>
#include <bitset>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <queue>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Headers from other projects which are both commonly included in patchpanel
// and expensive to compile.
#include <base/files/file_descriptor_watcher_posix.h>
#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/time/time.h>
