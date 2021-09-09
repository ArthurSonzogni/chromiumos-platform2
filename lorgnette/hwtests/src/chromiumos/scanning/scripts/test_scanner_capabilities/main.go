// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"flag"
	"fmt"
	"log"

	"chromiumos/scanning/hwtests"
	"chromiumos/scanning/utils"
)

// Runs various tests to verify that a scanner's reported capabilities satisfy
// the WWCB specification.
func main() {
	identifierFlag := flag.String("identifier", "", "Substring of the identifier printed by lorgnette_cli of the scanner to test.")
	flag.Parse()

	logFile, err := utils.CreateLogFile()
	if err != nil {
		log.Fatal(err)
	}

	log.SetOutput(logFile)
	fmt.Printf("Created log file at: %s\n", logFile.Name())

	listOutput, err := utils.LorgnetteCLIList()
	if err != nil {
		log.Fatal(err)
	}

	scannerInfo, err := utils.GetLorgnetteScannerInfo(listOutput, *identifierFlag)
	if err != nil {
		log.Fatal(err)
	}

	caps, err := utils.GetScannerCapabilities(scannerInfo.Address)
	if err != nil {
		log.Fatal(err)
	}

	tests := map[string]utils.TestFunction{
		"NoCameraSource":         hwtests.NoCameraSourceTest(caps.CameraInputCaps),
		"HasSupportedResolution": hwtests.HasSupportedResolutionTest(caps.PlatenInputCaps, caps.AdfCapabilities.AdfSimplexInputCaps, caps.AdfCapabilities.AdfDuplexInputCaps)}
	failed := []string{}

	for name, test := range tests {
		if !utils.RunTest(name, test) {
			failed = append(failed, name)
		}
	}

	if len(failed) != 0 {
		fmt.Printf("%d tests failed:\n", len(failed))
		for _, failedTest := range failed {
			fmt.Println(failedTest)
		}
	} else {
		fmt.Println("All tests passed.")
	}

}
