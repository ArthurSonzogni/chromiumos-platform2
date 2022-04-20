// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package proxy outputs client-side bindings classes based on introspects.
package proxy

import (
	"io"
	"text/template"

	"chromiumos/dbusbindings/generate/genutil"
	"chromiumos/dbusbindings/introspect"
	"chromiumos/dbusbindings/serviceconfig"
)

var funcMap = template.FuncMap{
	"extractNameSpaces": genutil.ExtractNameSpaces,
	"formatComment":     genutil.FormatComment,
	"makeFullItfName":   genutil.MakeFullItfName,
	"makeFullProxyName": genutil.MakeFullProxyName,
	"makeProxyName":     genutil.MakeProxyName,
	"reverse":           genutil.Reverse,
}

const (
	templateText = `// Automatic generation of D-Bus interfaces:
{{range .Introspects}}{{range .Interfaces -}}
//  - {{.Name}}
{{end}}{{end -}}

#ifndef {{.HeaderGuard}}
#define {{.HeaderGuard}}
#include <memory>
#include <string>
#include <vector>

#include <base/bind.h>
#include <base/callback.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <brillo/any.h>
#include <brillo/dbus/dbus_method_invoker.h>
#include <brillo/dbus/dbus_property.h>
#include <brillo/dbus/dbus_signal_handler.h>
#include <brillo/dbus/file_descriptor.h>
#include <brillo/errors/error.h>
#include <brillo/variant_dictionary.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/object_manager.h>
#include <dbus/object_path.h>
#include <dbus/object_proxy.h>

{{if .ObjectManagerName -}}
{{range extractNameSpaces .ObjectManagerName -}}
namespace {{.}} {
{{end -}}
class {{makeProxyName .ObjectManagerName}};
{{range extractNameSpaces .ObjectManagerName | reverse -}}
}  // namespace {{.}}
{{end}}
{{end -}}
{{range $introspect := .Introspects}}{{range .Interfaces -}}

{{range extractNameSpaces .Name -}}
namespace {{.}} {
{{end}}
// Abstract interface proxy for {{makeFullItfName .Name}}.
{{formatComment .DocString 0 -}}
{{- $itfName := makeProxyName .Name | printf "%sInterface" -}}
class {{$itfName}} {
 public:
  virtual ~{{$itfName}}() = default;
{{range .Methods -}}
{{- /* TODO(crbug.com/983008): Add method proxies */ -}}
{{- /* TODO(crbug.com/983008): Add asyn method proxies */ -}}
{{end -}}
{{range .Signals -}}
{{- /* TODO(crbug.com/983008): Add signal handler registration */ -}}
{{end -}}
{{- /* TODO(crbug.com/983008): Add properties */}}
  virtual const dbus::ObjectPath& GetObjectPath() const = 0;
  virtual dbus::ObjectProxy* GetObjectProxy() const = 0;
{{- /* TODO(crbug.com/983008): Add initialization of properties */}}
};

{{range extractNameSpaces .Name | reverse -}}
}  // namespace {{.}}
{{end}}
{{range extractNameSpaces .Name -}}
namespace {{.}} {
{{end}}
// Interface proxy for {{makeFullItfName .Name}}.
{{formatComment .DocString 0 -}}
{{- $proxyName := makeProxyName .Name -}}
class {{$proxyName}} final : public {{$itfName}} {
 public:
{{- /* TODO(crbug.com/983008): Add property set */ -}}
{{- /* TODO(crbug.com/983008): Add constructor */}}
  {{$proxyName}}(const {{$proxyName}}&) = delete;
  {{$proxyName}}& operator=(const {{$proxyName}}&) = delete;

  ~{{$proxyName}}() override {
  }

{{- range .Signals}}
{{- /* TODO(crbug.com/983008): Add signal andler registration. */ -}}
{{- end}}
  void ReleaseObjectProxy(base::OnceClosure callback) {
    bus_->RemoveObjectProxy(service_name_, object_path_, std::move(callback));
  }

  const dbus::ObjectPath& GetObjectPath() const override {
    return object_path_;
  }

  dbus::ObjectProxy* GetObjectProxy() const override {
    return dbus_object_proxy_;
  }

{{- /* TODO(crbug.com/983008): Add initialization of properties */ -}}

{{range .Methods}}
{{- /* TODO(crbug.com/983008): Add method proxy. */ -}}
{{- /* TODO(crbug.com/983008): Add async method proxy. */ -}}
{{end}}

{{- /* TODO(crbug.com/983008): Add properties. */}}

 private:
{{- if and $.ObjectManagerName .Properties}}
{{- /* TODO(crbug.com/983008): Add OnPropertyChanged. */ -}}
{{- end}}
  scoped_refptr<dbus::Bus> bus_;
{{- if $.ServiceName}}
  const std::string service_name_{"{{$.ServiceName}}"};
{{- else}}
  std::string service_name_;
{{- end}}

{{- if $introspect.Name}}
  const dbus::ObjectPath object_path_{"{{$introspect.Name}}"};
{{- else}}
  dbus::ObjectPath object_path_;
{{- end}}
{{- if and $.ObjectManagerName .Properties}}
  PropertySet* property_set_;
  base::RepeatingCallback<void({{$itfName}}*, const std::string&)> on_property_changed_;
{{- end}}
  dbus::ObjectProxy* dbus_object_proxy_;
{{- if and (not $.ObjectManagerName) .Properties}}
  std::unique_ptr<PropertySet> property_set_;
{{- end}}
{{- if and $.ObjectManagerName .Properties}}
  friend class {{makeFullProxyName $.ObjectManagerName}};
{{- end}}

};

{{range extractNameSpaces .Name | reverse -}}
}  // namespace {{.}}
{{end}}
{{end}}{{end -}}
{{- /* TODO(crbug.com/983008): Convert ObjectManager::GenerateProxy */ -}}
#endif  // {{.HeaderGuard}}
`
)

// Generate outputs the header file containing proxy interfaces into f.
// outputFilePath is used to make a unique header guard.
func Generate(introspects []introspect.Introspection, f io.Writer, outputFilePath string, config serviceconfig.Config) error {
	tmpl, err := template.New("proxy").Funcs(funcMap).Parse(templateText)
	if err != nil {
		return err
	}

	headerGuard := genutil.GenerateHeaderGuard(outputFilePath)
	return tmpl.Execute(f, struct {
		Introspects       []introspect.Introspection
		HeaderGuard       string
		ServiceName       string
		ObjectManagerName string
	}{
		Introspects:       introspects,
		HeaderGuard:       headerGuard,
		ServiceName:       config.ServiceName,
		ObjectManagerName: config.ObjectManager.Name,
	})
}
