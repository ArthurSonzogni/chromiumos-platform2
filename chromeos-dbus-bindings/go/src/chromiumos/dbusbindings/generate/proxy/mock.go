// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package proxy

import (
	"io"
	"text/template"

	"chromiumos/dbusbindings/generate/genutil"
	"chromiumos/dbusbindings/introspect"
	"chromiumos/dbusbindings/serviceconfig"
)

const mockTemplateText = `// Automatic generation of D-Bus interface mock proxies for:
{{range .Introspects}}{{range .Interfaces -}}
//  - {{.Name}}
{{end}}{{end -}}

#ifndef {{.HeaderGuard}}
#define {{.HeaderGuard}}
#include <string>
#include <vector>

#include <base/callback_forward.h>
#include <base/logging.h>
#include <brillo/any.h>
#include <brillo/errors/error.h>
#include <brillo/variant_dictionary.h>
#include <gmock/gmock.h>
{{- if $.ProxyFilePath}}

#include "{{$.ProxyFilePath}}"
{{- end}}

{{range $introspect := .Introspects}}{{range $itf := .Interfaces -}}
{{- $itfName := makeProxyInterfaceName .Name -}}

{{- if (not $.ProxyFilePath)}}
{{- template "proxyInterface" (makeProxyInterfaceArgs . $.ObjectManagerName) }}
{{- end}}
{{range extractNameSpaces .Name -}}
namespace {{.}} {
{{end}}
// Mock object for {{$itfName}}.
{{- $mockName := makeProxyName .Name | printf "%sMock" }}
class {{$mockName}} : public {{$itfName}} {
 public:
  {{$mockName}}() = default;
  {{$mockName}}(const {{$mockName}}&) = delete;
  {{$mockName}}& operator=(const {{$mockName}}&) = delete;
{{- /* TODO(crbug.com/983008): add mock method API generation. */ -}}
{{- /* TODO(crbug.com/983008): add mock signal API generation. */ -}}
{{- /* TODO(crbug.com/983008): add mock properties generation. */}}

};
{{range extractNameSpaces .Name | reverse -}}
}  // namespace {{.}}
{{end}}
{{- end}}
{{- end}}

#endif  // {{.HeaderGuard}}
`

// GenerateMock outputs the header file containing gmock proxy interfaces into f.
// outputFilePath is used to make a unique header guard.
func GenerateMock(introspects []introspect.Introspection, f io.Writer, outputFilePath string, config serviceconfig.Config) error {
	tmpl, err := template.New("mock").Funcs(funcMap).Parse(mockTemplateText)
	if err != nil {
		return err
	}

	if _, err := tmpl.Parse(proxyInterfaceTemplate); err != nil {
		return err
	}

	headerGuard := genutil.GenerateHeaderGuard(outputFilePath)
	return tmpl.Execute(f, struct {
		Introspects       []introspect.Introspection
		HeaderGuard       string
		ProxyFilePath     string
		ServiceName       string
		ObjectManagerName string
	}{
		Introspects:       introspects,
		HeaderGuard:       headerGuard,
		ProxyFilePath:     "", // TODO(crbug.com/983008): Support ProxyFilePath.
		ServiceName:       config.ServiceName,
		ObjectManagerName: config.ObjectManager.Name,
	})
}
