// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package scanning

import (
	"fmt"
	"os/exec"
	"regexp"
	"strings"
)

// Name of the lorgnette_cli executable.
const lorgnetteCLI = "lorgnette_cli"

// Regex which matches an HTTP or HTTPS scanner address.
var scannerRegex = regexp.MustCompile(`^(?P<protocol>airscan|ippusb):escl:(?P<name>[^:]+):(?P<address>.*/eSCL/)$`)

// LorgnetteScannerInfo aggregates a scanner's information as reported by
// lorgnette.
type LorgnetteScannerInfo struct {
	Protocol string
	Name     string
	Address  string
}

// LorgnetteCLIList runs the command `lorgnette_cli list` and returns its
// stdout.
func LorgnetteCLIList() (string, error) {
	cmd := exec.Command(lorgnetteCLI, "list")
	outputBytes, err := cmd.Output()
	return string(outputBytes), err
}

// LorgnetteCLIGetJSONCaps runs the command
// `lorgnette_cli get_json_caps --scanner=`scanner`` and returns its stdout.
func LorgnetteCLIGetJSONCaps(scanner string) (string, error) {
	cmd := exec.Command(lorgnetteCLI, "get_json_caps", "--scanner="+scanner)
	outputBytes, err := cmd.Output()
	return string(outputBytes), err
}

// GetLorgnetteScannerInfo parses `listOutput` to find the lorgnette scanner
// information for the first scanner in `listOutput` which matches `model`.
// `listOutput` is expected to be the output from `lorgnette_cli list`.
func GetLorgnetteScannerInfo(listOutput string, model string) (info LorgnetteScannerInfo, err error) {
	lines := strings.Split(listOutput, "\n")
	for _, line := range lines {
		modelMatch, _ := regexp.MatchString(model, line)
		if !modelMatch {
			continue
		}

		match := scannerRegex.FindStringSubmatch(line)
		if match == nil || len(match) < 4 {
			continue
		}

		for i, name := range scannerRegex.SubexpNames() {
			if name == "protocol" {
				info.Protocol = match[i]
			}

			if name == "name" {
				info.Name = match[i]
			}

			if name == "address" {
				info.Address = match[i]
			}
		}

		return
	}

	err = fmt.Errorf("No scanner info found for model: %s", model)
	return
}
