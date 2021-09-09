// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package hwtests

import (
	"fmt"

	"github.com/google/go-cmp/cmp"

	"chromiumos/scanning/utils"
)

var supportedResolutions = []int{75, 100, 150, 200, 300, 600}

// checkForSupportedResolution returns true if `sourceResolutions` advertises at
// least one supported resolution, which must be advertised for both X and Y
// resolutions.
func checkForSupportedResolution(sourceResolutions utils.SupportedResolutions) bool {
	for _, discreteResolution := range sourceResolutions.DiscreteResolutions {
		if discreteResolution.XResolution != discreteResolution.YResolution {
			continue
		}

		for _, supportedResolution := range supportedResolutions {
			if discreteResolution.XResolution == supportedResolution {
				return true
			}
		}
	}

	for _, supportedResolution := range supportedResolutions {
		if supportedResolution < sourceResolutions.XResolutionRange.Min || supportedResolution > sourceResolutions.XResolutionRange.Max {
			continue
		}

		if supportedResolution < sourceResolutions.YResolutionRange.Min || supportedResolution > sourceResolutions.YResolutionRange.Max {
			continue
		}

		if (supportedResolution-sourceResolutions.XResolutionRange.Min)%sourceResolutions.XResolutionRange.Step == 0 && (supportedResolution-sourceResolutions.YResolutionRange.Min)%sourceResolutions.YResolutionRange.Step == 0 {
			return true
		}
	}

	return false
}

// HasSupportedResolutionTest checks that each supported document source
// advertises at least one supported resolution. One critical failure will be
// returned for each supported document source which does not advertise any of
// the supported resolutions.
func HasSupportedResolutionTest(platenCaps utils.SourceCapabilities, adfSimplexCaps utils.SourceCapabilities, adfDuplexCaps utils.SourceCapabilities) utils.TestFunction {
	return func() (failures []utils.TestFailure, err error) {
		if !cmp.Equal(platenCaps, utils.SourceCapabilities{}) && !checkForSupportedResolution(platenCaps.SettingProfile.SupportedResolutions) {
			failures = append(failures, utils.TestFailure{Type: utils.CriticalFailure, Message: fmt.Sprintf("Platen source advertises only unsupported resolutions: %v", platenCaps.SettingProfile.SupportedResolutions)})
		}
		if !cmp.Equal(adfSimplexCaps, utils.SourceCapabilities{}) && !checkForSupportedResolution(adfSimplexCaps.SettingProfile.SupportedResolutions) {
			failures = append(failures, utils.TestFailure{Type: utils.CriticalFailure, Message: fmt.Sprintf("ADF simplex source advertises only unsupported resolutions: %v", adfSimplexCaps.SettingProfile.SupportedResolutions)})
		}
		if !cmp.Equal(adfDuplexCaps, utils.SourceCapabilities{}) && !checkForSupportedResolution(adfDuplexCaps.SettingProfile.SupportedResolutions) {
			failures = append(failures, utils.TestFailure{Type: utils.CriticalFailure, Message: fmt.Sprintf("ADF duplex source advertises only unsupported resolutions: %v", adfDuplexCaps.SettingProfile.SupportedResolutions)})
		}
		return
	}
}
