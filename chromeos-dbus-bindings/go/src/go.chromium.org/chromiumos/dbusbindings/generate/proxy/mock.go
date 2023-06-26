// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package proxy

import (
	"fmt"
	"io"
	"strings"
	"text/template"

	"go.chromium.org/chromiumos/dbusbindings/generate/genutil"
	"go.chromium.org/chromiumos/dbusbindings/introspect"
	"go.chromium.org/chromiumos/dbusbindings/serviceconfig"
)

const mockTemplateText = `// Automatic generation of D-Bus interface mock proxies for:
{{range .Introspects}}{{range .Interfaces -}}
//  - {{.Name}}
{{end}}{{end -}}

#ifndef {{.HeaderGuard}}
#define {{.HeaderGuard}}
#include <string>
#include <vector>

#include <base/functional/callback_forward.h>
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
{{template "proxyInterface" (makeProxyInterfaceArgs . $.ObjectManagerName) }}
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
{{- range .Methods}}
{{- $inParams := makeMockMethodParams .InputArguments}}
{{- $outParams := makeMockMethodParams .OutputArguments}}

  MOCK_METHOD(bool,
              {{.Name}},
              ({{- range $inParams}}{{maybeWrap .Type}}{{if .Name}} {{.Name}}{{end}},
               {{end -}}
               {{- range $outParams}}{{maybeWrap .Type}}{{if .Name}} {{.Name}}{{end}},
               {{end -}}
               brillo::ErrorPtr* /*error*/,
               int /*timeout_ms*/),
              (override));
  MOCK_METHOD(void,
              {{.Name}}Async,
              ({{- range $inParams}}{{maybeWrap .Type}}{{if .Name}} {{.Name}}{{end}},
               {{end -}}
               {{- makeMethodCallbackType .OutputArguments | maybeWrap}} /*success_callback*/,
               base::OnceCallback<void(brillo::Error*)> /*error_callback*/,
               int /*timeout_ms*/),
              (override));
{{- end}}

{{- range .Signals}}

  {{/* TODO(b/288402584): get rid of DoRegister* function */ -}}
  void Register{{.Name}}SignalHandler(
    {{- /* TODO(crbug.com/983008): fix the indent to meet style guide. */ -}}
    {{- makeSignalCallbackType .Args | nindent 4}} signal_callback,
    dbus::ObjectProxy::OnConnectedCallback on_connected_callback) override {
    DoRegister{{.Name}}SignalHandler(signal_callback, &on_connected_callback);
  }
  MOCK_METHOD(void,
              DoRegister{{.Name}}SignalHandler,
              ({{makeSignalCallbackType .Args | nindent 15 | trimLeft " \n"}} /*signal_callback*/,
               dbus::ObjectProxy::OnConnectedCallback* /*on_connected_callback*/));
{{- end}}

{{- range .Properties}}
{{- $name := makePropertyVariableName . | makeVariableName -}}
{{- $type := makeProxyInArgTypeProxy . }}

  MOCK_METHOD({{$type}}, {{$name}}, (), (const, override));
  MOCK_METHOD(bool, is_{{$name}}_valid, (), (const, override));

{{- if eq .Access "readwrite"}}
  MOCK_METHOD(void,
              set_{{$name}},
              ({{maybeWrap $type}}, base::OnceCallback<void(bool)>),
              (override));
{{- end}}
{{- end}}

  MOCK_METHOD(const dbus::ObjectPath&, GetObjectPath, (), (const, override));
  MOCK_METHOD(dbus::ObjectProxy*, GetObjectProxy, (), (const, override));
{{- if .Properties}}
{{- if $.ObjectManagerName }}

  MOCK_METHOD(void,
              SetPropertyChangedCallback,
              ((const base::RepeatingCallback<void({{$itfName}}*,
                                                   const std::string&)>&)),
              (override));
{{- else}}

  MOCK_METHOD(void,
              InitializeProperties,
              ((const base::RepeatingCallback<void({{$itfName}}*,
                                                   const std::string&)>&)),
              (override));
{{- end}}
{{- end}}
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
func GenerateMock(introspects []introspect.Introspection, f io.Writer, outputFilePath string, proxyFilePath string, config serviceconfig.Config) error {
	mockFuncMap := make(template.FuncMap)
	for k, v := range funcMap {
		mockFuncMap[k] = v
	}

	// Mock argument type must not contain commas, or needs to be wrapped
	// by parens. E.g., "std::pair<int, int>" needs to be "(std::pair<int, int>)".
	mockFuncMap["maybeWrap"] = func(typ string) string {
		if !strings.Contains(typ, ",") {
			return typ
		}
		// Wrap with a pair of parens. Also, tweak the indent.
		return fmt.Sprintf("(%s)", strings.ReplaceAll(typ, "\n", "\n "))
	}
	tmpl, err := template.New("mock").Funcs(mockFuncMap).Parse(mockTemplateText)
	if err != nil {
		return err
	}

	if _, err := tmpl.Parse(proxyInterfaceTemplate); err != nil {
		return err
	}

	var omName string
	if config.ObjectManager != nil {
		omName = config.ObjectManager.Name
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
		ProxyFilePath:     proxyFilePath,
		ServiceName:       config.ServiceName,
		ObjectManagerName: omName,
	})
}
