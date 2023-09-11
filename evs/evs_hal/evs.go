// Copyright 2019 NXP
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

package evs

import (
	"android/soong/android"
	"android/soong/cc"
	"github.com/google/blueprint/proptools"
	"strconv"
)

func init() {
	android.RegisterModuleType("evs_defaults", evsDefaultsFactory)
}

func evsDefaultsFactory() android.Module {
	module := cc.DefaultsFactory()
	android.AddLoadHook(module, evsDefaults)
	return module
}

func evsDefaults(ctx android.LoadHookContext) {
	type props struct {
		Target struct {
			Android struct {
				Enabled  *bool
				Cflags   []string
				Cppflags []string
			}
		}
	}
	p := &props{}
	if ctx.Config().VendorConfig("IMXPLUGIN").Bool("BOARD_HAVE_IMX_EVS") {
		p.Target.Android.Enabled = proptools.BoolPtr(true)
	} else {
		p.Target.Android.Enabled = proptools.BoolPtr(false)
	}
	if ctx.Config().VendorConfig("IMXPLUGIN").String("TARGET_GRALLOC_VERSION") == "v4" {
		p.Target.Android.Cppflags = append(p.Target.Android.Cppflags, "-DGRALLOC_VERSION=4")
	}
	p.Target.Android.Cflags = append(p.Target.Android.Cflags, "-DANDROID_SDK_VERSION="+strconv.Itoa(ctx.AConfig().PlatformSdkVersion().FinalOrFutureInt()))
	ctx.AppendProperties(p)
}
