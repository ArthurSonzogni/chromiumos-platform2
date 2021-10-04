// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package hwtests

import (
	"fmt"

	"chromiumos/scanning/utils"
)

// checkForSupportedResolution returns true if `sourceResolutions` advertises at
// least one supported resolution, which must be advertised for both X and Y
// resolutions.
func checkForSupportedResolution(sourceResolutions utils.SupportedResolutions) bool {
	lorgnetteResolutions := sourceResolutions.ToLorgnetteResolutions()

	if len(lorgnetteResolutions) == 0 {
		return false
	}

	return true
}

// HasSupportedResolutionTest checks that each supported document source
// advertises at least one supported resolution. One critical failure will be
// returned for each supported document source which does not advertise any of
// the supported resolutions.
func HasSupportedResolutionTest(platenCaps utils.SourceCapabilities, adfSimplexCaps utils.SourceCapabilities, adfDuplexCaps utils.SourceCapabilities) utils.TestFunction {
	return func() (result utils.TestResult, failures []utils.TestFailure, err error) {
		if !platenCaps.IsPopulated() && !adfSimplexCaps.IsPopulated() && !adfDuplexCaps.IsPopulated() {
			result = utils.Skipped
			return
		}

		if platenCaps.IsPopulated() && !checkForSupportedResolution(platenCaps.SettingProfile.SupportedResolutions) {
			failures = append(failures, utils.TestFailure{Type: utils.CriticalFailure, Message: fmt.Sprintf("Platen source advertises only unsupported resolutions: %v", platenCaps.SettingProfile.SupportedResolutions)})
		}
		if adfSimplexCaps.IsPopulated() && !checkForSupportedResolution(adfSimplexCaps.SettingProfile.SupportedResolutions) {
			failures = append(failures, utils.TestFailure{Type: utils.CriticalFailure, Message: fmt.Sprintf("ADF simplex source advertises only unsupported resolutions: %v", adfSimplexCaps.SettingProfile.SupportedResolutions)})
		}
		if adfDuplexCaps.IsPopulated() && !checkForSupportedResolution(adfDuplexCaps.SettingProfile.SupportedResolutions) {
			failures = append(failures, utils.TestFailure{Type: utils.CriticalFailure, Message: fmt.Sprintf("ADF duplex source advertises only unsupported resolutions: %v", adfDuplexCaps.SettingProfile.SupportedResolutions)})
		}

		if len(failures) == 0 {
			result = utils.Passed
		} else {
			result = utils.Failed
		}

		return
	}
}
