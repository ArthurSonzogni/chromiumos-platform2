// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package introspect

import (
	"encoding/xml"
)

// Parse converts introspection from the XML to a structure.
func Parse(content []byte) (Introspection, error) {
	var i Introspection
	err := xml.Unmarshal(content, &i)
	return i, err
}
