// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package hwtests

import (
	"chromiumos/scanning/utils"
)

// NoCameraSourceTest checks if `cameraCapabilities` is the zero value. If it
// isn't, the test returns a critical failure. Else it returns no failures.
func NoCameraSourceTest(cameraCapabilities utils.SourceCapabilities) utils.TestFunction {
	return func() (failures []utils.TestFailure, err error) {
		if cameraCapabilities.IsPopulated() {
			failures = append(failures, utils.TestFailure{Type: utils.CriticalFailure, Message: "Scanner advertises camera capabilities."})
		}
		return
	}
}
