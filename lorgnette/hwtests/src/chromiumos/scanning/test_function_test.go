// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package scanning

import (
	"bytes"
	"fmt"
	"log"
	"strings"
	"testing"
)

const testName = "testInt"

const criticalFailureMessage = "Critical failure."
const needsAuditFailureMessage = "Needs audit failure."
const errorMessage = "Bad integer: 1"

var criticalFailure = TestFailure{Type: CriticalFailure, Message: criticalFailureMessage}
var needsAuditFailure = TestFailure{Type: NeedsAudit, Message: needsAuditFailureMessage}

// integerTest returns a TestFunction used in the TestRunTest unit test.
func integerTest(testInt int) TestFunction {
	return func() (failures []TestFailure, err error) {
		switch testInt {
		case 1:
			err = fmt.Errorf(errorMessage)
		case 2:
			failures = append(failures, criticalFailure)
		case 3:
			failures = append(failures, needsAuditFailure)
		case 4:
			failures = append(failures, criticalFailure, needsAuditFailure)
		}
		return
	}
}

// TestRunTest tests that we can run a TestFunction via the RunTest wrapper.
func TestRunTest(t *testing.T) {
	tests := []struct {
		testInt  int
		passed   bool
		failures []TestFailure
		errText  string
	}{
		{
			testInt:  1,
			passed:   false,
			failures: []TestFailure{},
			errText:  errorMessage,
		},
		{
			testInt:  2,
			passed:   false,
			failures: []TestFailure{criticalFailure},
			errText:  "",
		},
		{
			testInt:  3,
			passed:   false,
			failures: []TestFailure{needsAuditFailure},
			errText:  "",
		},
		{
			testInt:  4,
			passed:   false,
			failures: []TestFailure{criticalFailure, needsAuditFailure},
			errText:  "",
		},
		{
			testInt:  5,
			passed:   true,
			failures: []TestFailure{},
			errText:  "",
		},
	}

	for _, tc := range tests {
		var logBuf bytes.Buffer
		log.SetOutput(&logBuf)

		got := RunTest(testName, integerTest(tc.testInt))

		if got != tc.passed {
			t.Errorf("Passed: got %t, want %t", got, tc.passed)
		}

		lines := strings.Split(strings.TrimSuffix(logBuf.String(), "\n"), "\n")

		var expectedNumLines int
		// All tests should have the starting and finished lines. Additionally:
		// Tests with errors should have a single line with the error.
		// Tests with failures but no errors should have a line for each error.
		// Tests with no errors and no failures should have a single "PASSED"
		// line.
		if tc.errText != "" {
			expectedNumLines = 3
		} else if len(tc.failures) != 0 {
			expectedNumLines = 2 + len(tc.failures)
		} else {
			expectedNumLines = 3
		}

		if len(lines) != expectedNumLines {
			t.Errorf("Number of log lines: got %d, want %d", len(lines), expectedNumLines)
		}

		for lineNum, line := range lines {
			var expectedLine string
			if lineNum == 0 {
				expectedLine = "===== START " + testName + " ====="
			} else if lineNum == expectedNumLines-1 {
				expectedLine = "===== END " + testName + " ====="
			} else if tc.errText != "" {
				expectedLine = "Error during test execution: " + tc.errText
			} else if len(tc.failures) == 0 {
				expectedLine = "PASSED."
			} else if tc.failures[lineNum-1] == criticalFailure {
				expectedLine = "CRITICAL FAILURE: " + criticalFailureMessage
			} else {
				expectedLine = "NEEDS AUDIT: " + needsAuditFailureMessage
			}

			// Logged lines will also contain timestamps, so we can't check for
			// direct equality with the expected line.
			if !strings.Contains(line, expectedLine) {
				t.Errorf("Line: %s does not contain: %s", line, expectedLine)
			}
		}
	}
}
