// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package scanning

import "log"

// TestFunction is the type used by RunTest. All test functions should return a
// TestFunction. Returned TestFailures indicate that the test was completed
// successfully, but a condition tested was out of compliance with WWCB. Errors
// indicate that the test was unable to complete. For example, consider a test
// that retrieves a scanner's XML capabilities then parses those to check for
// unsupported resolutions on Chrome OS. If the test is unable to retrieve the
// capabilities from the scanner, it should return an error. If it retrieves and
// parses the capabilities successfully, then finds an unsupported resolution,
// it should return a TestFailure.
type TestFunction func() ([]TestFailure, error)

// FailureType differentiates between different failure types.
type FailureType int

// Enumeration of different FailureTypes.
const (
	CriticalFailure FailureType = iota // Blocks WWCB certification.
	NeedsAudit                         // Needs auditing by a human - handled on a case-by-case basis.
)

// TestFailure represents a single failure caught by a test function.
type TestFailure struct {
	Type    FailureType // Type of the failure.
	Message string      // More details about the failure.
}

// RunTest wraps the execution of a TestFunction. It provides a standardized way
// of logging test execution, errors, and test results.
func RunTest(testName string, testFunction TestFunction) (passed bool) {
	log.Printf("===== START %s =====", testName)
	result, err := testFunction()
	if err != nil {
		log.Printf("Error during test execution: %v", err)
	} else if len(result) == 0 {
		log.Println("PASSED.")
		passed = true
	} else {
		for _, failure := range result {
			switch failureType := failure.Type; failureType {
			case CriticalFailure:
				log.Println("CRITICAL FAILURE:", failure.Message)
			case NeedsAudit:
				log.Println("NEEDS AUDIT:", failure.Message)
			default:
				log.Printf("Unrecognized failure type: %d", failureType)
			}
		}
	}
	log.Printf("===== END %s =====", testName)
	return
}
