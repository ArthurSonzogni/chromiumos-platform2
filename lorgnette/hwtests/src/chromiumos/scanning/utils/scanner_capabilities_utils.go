// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Utilities related to a scanner's capabilities.

package utils

import (
	"crypto/tls"
	"encoding/json"
	"encoding/xml"
	"fmt"
	"io/ioutil"
	"net/http"

	"github.com/google/go-cmp/cmp"
)

// ScannableArea represents the maximum size a scanner source can scan.
type ScannableArea struct {
	Height float32 `json:"Height"`
	Width  float32 `json:"Width"`
}

// LorgnetteSource represents a scanner source's capabilities as read from
// Lorgnette.
type LorgnetteSource struct {
	ColorModes    []string      `json:"ColorModes"`
	Resolutions   []int         `json:"Resolutions"`
	ScannableArea ScannableArea `json:"ScannableArea"`
}

// LorgnetteCapabilities represents a scanner's capabilities as read from
// Lorgnette.
type LorgnetteCapabilities struct {
	PlatenCaps     LorgnetteSource `json:"SOURCE_PLATEN"`
	AdfSimplexCaps LorgnetteSource `json:"SOURCE_ADF_SIMPLEX"`
	AdfDuplexCaps  LorgnetteSource `json:"SOURCE_ADF_DUPLEX"`
}

// DiscreteResolution represents a pair of X and Y resolution values supported
// by a scanner.
type DiscreteResolution struct {
	XResolution int `xml:"XResolution"`
	YResolution int `xml:"YResolution"`
}

// ResolutionRange represents a range of resolutions supported by a scanner, for
// one of the X or Y resolutions.
type ResolutionRange struct {
	Min    int `xml:"Min"`
	Max    int `xml:"Max"`
	Normal int `xml:"Normal"`
	Step   int `xml:"Step"`
}

// SupportedResolutions represents all of the resolutions supported by a
// scanner.
type SupportedResolutions struct {
	DiscreteResolutions []DiscreteResolution `xml:"DiscreteResolutions>DiscreteResolution"`
	XResolutionRange    ResolutionRange      `xml:"ResolutionRange>XResolutionRange"`
	YResolutionRange    ResolutionRange      `xml:"ResolutionRange>YResolutionRange"`
}

// SettingProfile represents a group of settings common to one or more
// SourceCapabilities.
type SettingProfile struct {
	Name                 string               `xml:"name,attr"`
	Ref                  string               `xml:"ref,attr"`
	ColorModes           []string             `xml:"ColorModes>ColorMode"`
	DocumentFormats      []string             `xml:"DocumentFormats>DocumentFormat"`
	DocumentFormatsExt   []string             `xml:"DocumentFormats>DocumentFormatExt"`
	SupportedResolutions SupportedResolutions `xml:"SupportedResolutions"`
}

// SourceCapabilities represents the capabilities of a single scanner source:
// Platen, ADF simplex, ADF duplex or camera.
type SourceCapabilities struct {
	MaxWidth              int            `xml:"MaxWidth"`
	MinWidth              int            `xml:"MinWidth"`
	MaxHeight             int            `xml:"MaxHeight"`
	MinHeight             int            `xml:"MinHeight"`
	MaxScanRegions        int            `xml:"MaxScanRegions"`
	SettingProfile        SettingProfile `xml:"SettingProfiles>SettingProfile"`
	MaxOpticalXResolution int            `xml:"MaxOpticalXResolution"`
	MaxOpticalYResolution int            `xml:"MaxOpticalYResolution"`
	MaxPhysicalWidth      int            `xml:"MaxPhysicalWidth"`
	MaxPhysicalHeight     int            `xml:"MaxPhysicalHeight"`
}

// AdfCapabilities represents all of a scanner's ADF capabilities.
type AdfCapabilities struct {
	AdfSimplexInputCaps SourceCapabilities `xml:"AdfSimplexInputCaps"`
	AdfDuplexInputCaps  SourceCapabilities `xml:"AdfDuplexInputCaps"`
	AdfOptions          []string           `xml:"AdfOptions>AdfOption"`
}

// StoredJobRequestSupport represents a scanner's support for stored job
// requests.
type StoredJobRequestSupport struct {
	MaxStoredJobRequests int `xml:"MaxStoredJobRequests"`
	TimeoutInSeconds     int `xml:"TimeoutInSeconds"`
	PINLength            int `xml:"PINLength"`
	MaxJobNameLength     int `xml:"MaxJobNameLength"`
}

// ScannerCapabilities represents all of a scanner's capabilities.
type ScannerCapabilities struct {
	Version                 string                  `xml:"Version"`
	MakeAndModel            string                  `xml:"MakeAndModel"`
	Manufacturer            string                  `xml:"Manufacturer"`
	SettingProfiles         []SettingProfile        `xml:"SettingProfiles>SettingProfile"`
	PlatenInputCaps         SourceCapabilities      `xml:"Platen>PlatenInputCaps"`
	AdfCapabilities         AdfCapabilities         `xml:"Adf"`
	CameraInputCaps         SourceCapabilities      `xml:"Camera>CameraInputCaps"`
	StoredJobRequestSupport StoredJobRequestSupport `xml:"StoredJobRequestSupport"`
}

// setReferencedProfileIfNecessary checks to see if `outProfile` references
// another SettingProfile, and if so, finds that profile in `referencedProfiles`
// and copies its information into `outProfile`.
func setReferencedProfileIfNecessary(
	outProfile *SettingProfile, referencedProfiles []SettingProfile) error {
	if outProfile.Ref == "" {
		return nil
	}

	for _, profile := range referencedProfiles {
		if profile.Name == outProfile.Ref {
			*outProfile = profile
			return nil
		}
	}

	return fmt.Errorf("No profile found for reference: %s", outProfile.Ref)
}

// GetScannerCapabilities uses the HTTP address of the scanner to get its
// capabilities. `addr` should have a trailing slash. The returned
// ScannerCapabilities object is invalid when the returned error is non-nil. Any
// fields in ScannerCapabilities which were missing from the scanner's response
// will be left at their zero values.
func GetScannerCapabilities(addr string) (caps ScannerCapabilities, err error) {
	// Deliberately ignore certificate errors because printers normally
	// have self-signed certificates.
	tlsConfig := &tls.Config{
		MinVersion:         tls.VersionTLS12,
		InsecureSkipVerify: true,
	}
	tr := &http.Transport{
		TLSClientConfig: tlsConfig,
	}
	client := &http.Client{Transport: tr}
	resp, err := client.Get(addr + "ScannerCapabilities")
	if err != nil {
		return
	}
	defer resp.Body.Close()

	if resp.Status != "200 OK" {
		err = fmt.Errorf("Unexpected HTTP response status: %s", resp.Status)
		return
	}

	respbytes, err := ioutil.ReadAll(resp.Body)
	if err != nil {
		return
	}

	err = xml.Unmarshal(respbytes, &caps)
	if err != nil {
		return
	}

	// Replace any references to SettingProfiles with the referenced
	// SettingProfile.
	err = setReferencedProfileIfNecessary(&caps.PlatenInputCaps.SettingProfile, caps.SettingProfiles)
	if err != nil {
		return
	}
	err = setReferencedProfileIfNecessary(&caps.AdfCapabilities.AdfSimplexInputCaps.SettingProfile, caps.SettingProfiles)
	if err != nil {
		return
	}
	err = setReferencedProfileIfNecessary(&caps.AdfCapabilities.AdfDuplexInputCaps.SettingProfile, caps.SettingProfiles)
	if err != nil {
		return
	}
	err = setReferencedProfileIfNecessary(&caps.CameraInputCaps.SettingProfile, caps.SettingProfiles)
	if err != nil {
		return
	}

	return
}

// ParseLorgnetteCapabilities parses `rawData` into a structured format. It
// expects `rawData` to be JSON output from the command
// `lorgnette_cli get_json_caps --scanner=$SCANNER`. Any fields in
// LorgnetteCapabilities which were missing from `rawData` will be left at their
// zero values. If `err` is non-nill, `caps` is invalid.
func ParseLorgnetteCapabilities(rawData string) (caps LorgnetteCapabilities, err error) {
	err = json.Unmarshal([]byte(rawData), &caps)
	return
}

// IsPopulated returns returns true iff `caps` is non-empty.
func (caps SourceCapabilities) IsPopulated() bool {
	return !cmp.Equal(caps, SourceCapabilities{})
}
