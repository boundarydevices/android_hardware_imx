// Copyright 2024 NXP
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package neuralnetworks

import (
	"android/soong/android"
	"android/soong/cc"
)

func init() {
	android.RegisterModuleType("neuralnetworks_imx_defaults", neuralnetworksDefaultsFactory)
}

func neuralnetworksDefaultsFactory() android.Module {
	module := cc.DefaultsFactory()
	android.AddLoadHook(module, neuralnetworksDefaults)
	return module
}

func neuralnetworksDefaults(ctx android.LoadHookContext) {
	type props struct {
		Target struct {
			Android struct {
				Cflags []string
			}
		}
	}
	p := &props{}
	if ctx.Config().VendorConfig("IMXPLUGIN").String("BOARD_SOC_CLASS") == "IMX8" {
		p.Target.Android.Cflags = append(p.Target.Android.Cflags, "-DIMX8")
	}
	ctx.AppendProperties(p)
}
