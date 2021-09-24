// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package hwtests

import (
	"testing"

	"chromiumos/scanning/utils"
)

// TestNoCameraSourceTest tests that the NoCameraSourceTest functions correctly.
func TestNoCameraSourceTest(t *testing.T) {
	tests := []struct {
		cameraCapabilities utils.SourceCapabilities
		result             utils.TestResult
		failures           []utils.FailureType
	}{
		{
			cameraCapabilities: utils.SourceCapabilities{
				MaxWidth:       1200,
				MinWidth:       16,
				MaxHeight:      2800,
				MinHeight:      32,
				MaxScanRegions: 2,
				SettingProfile: utils.SettingProfile{
					Name:            "",
					Ref:             "",
					ColorModes:      []string{"RGB24"},
					DocumentFormats: []string{"application/octet-stream"},
					SupportedResolutions: utils.SupportedResolutions{
						XResolutionRange: utils.ResolutionRange{
							Min:    75,
							Max:    800,
							Normal: 300,
							Step:   10},
						YResolutionRange: utils.ResolutionRange{
							Min:    150,
							Max:    1200,
							Normal: 600,
							Step:   50}}},
				MaxOpticalXResolution: 800,
				MaxOpticalYResolution: 1200,
				MaxPhysicalWidth:      1200,
				MaxPhysicalHeight:     2800},
			result:   utils.Failed,
			failures: []utils.FailureType{utils.CriticalFailure},
		},
		{
			cameraCapabilities: utils.SourceCapabilities{},
			result:             utils.Passed,
			failures:           []utils.FailureType{},
		},
	}

	for _, tc := range tests {
		result, failures, err := NoCameraSourceTest(tc.cameraCapabilities)()

		if err != nil {
			t.Errorf("Unexpected error: %v", err)
		}

		if result != tc.result {
			t.Errorf("Result: expected %d, got %d", tc.result, result)
		}

		if len(failures) != len(tc.failures) {
			t.Errorf("Number of failures: expected %d, got %d", len(tc.failures), len(failures))
		}
		for i, failure := range failures {
			if failure.Type != tc.failures[i] {
				t.Errorf("FailureType: expected %d, got %d", tc.failures[i], failure.Type)
			}
		}
	}
}
