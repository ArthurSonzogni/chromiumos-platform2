// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tests for lorgnette_cli_utils.go.

package utils

import (
	"testing"
)

// Sample output from `lorgnette_cli list` with a valid airscan scanner.
const lorgnetteCLIListOutputAirscan = `Getting scanner list.
SANE scanners:
pixma:MF741C/743C_207.648.54.70: CANON Canon i-SENSYS MF741C/743C(multi-function peripheral)
1 SANE scanners found.
Detected scanners:
pixma:MF741C/743C_207.648.54.70
airscan:escl:Canon MF741C/743C (8d_29_6f) (4):http://207.648.54.70:99/eSCL/`

// Sample output from `lorgnette_cli list` with a valid IPP USB scanner.
const lorgnetteCLIListOutputIPPUSB = `Getting scanner list.
SANE scanners:
pixma:MF741C/743C_207.648.54.70: CANON Canon i-SENSYS MF741C/743C(multi-function peripheral)
1 SANE scanners found.
Detected scanners:
pixma:MF741C/743C_207.648.54.70
ippusb:escl:Canon TR8500 series:04a9_1823/eSCL/`

// Sample output from `lorgnette_cli list` with no valid scanner.
const lorgnetteCLIListOutputNoeSCLScanner = `Getting scanner list.
SANE scanners:
pixma:MF741C/743C_207.648.54.70: CANON Canon i-SENSYS MF741C/743C(multi-function peripheral)
1 SANE scanners found.
Detected scanners:
pixma:MF741C/743C_207.648.54.70`

// TestGetLorgnetteScannerInfo tests that scanner info can be parsed correctly.
func TestGetLorgnetteScannerInfo(t *testing.T) {
	tests := []struct {
		input    string
		model    string
		protocol string
		name     string
		address  string
	}{
		{
			input:    lorgnetteCLIListOutputAirscan,
			model:    "MF741C",
			protocol: "airscan",
			name:     "Canon MF741C/743C (8d_29_6f) (4)",
			address:  "http://207.648.54.70:99/eSCL/",
		},
		{
			input:    lorgnetteCLIListOutputIPPUSB,
			model:    "TR8500",
			protocol: "ippusb",
			name:     "Canon TR8500 series",
			address:  "04a9_1823/eSCL/",
		},
	}

	for _, tc := range tests {
		got, err := GetLorgnetteScannerInfo(tc.input, tc.model)

		if err != nil {
			t.Error(err)
		}

		if got.Protocol != tc.protocol {
			t.Errorf("Protocol: got %s, want %s", got.Protocol, tc.protocol)
		}

		if got.Name != tc.name {
			t.Errorf("Name: got %s, want %s", got.Name, tc.name)
		}

		if got.Address != tc.address {
			t.Errorf("Address: got %s, want %s", got.Address, tc.address)
		}
	}
}

// TestGetLorgnetteScannerInfoNoeSCLScanner tests that an error is returned when
// no valid scanner info is found.
func TestGetLorgnetteScannerInfoNoeSCLScanner(t *testing.T) {
	tests := []struct {
		input string
		model string
	}{
		{
			input: lorgnetteCLIListOutputNoeSCLScanner,
			model: "MF741C",
		},
		{
			input: lorgnetteCLIListOutputAirscan,
			model: "Bad Model",
		},
	}

	for _, tc := range tests {
		_, err := GetLorgnetteScannerInfo(tc.input, tc.model)

		if err == nil {
			t.Errorf("Expected error for no eSCL scanner found with input: %s and model: %s", tc.input, tc.model)
		}
	}
}
