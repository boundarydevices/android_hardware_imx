// Copyright 2021 NXP
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

package power

import (
        "android/soong/android"
        "android/soong/cc"
)

func init() {
    android.RegisterModuleType("imx_power_defaults", powerDefaultsFactory)
}

func powerDefaultsFactory() (android.Module) {
    module := cc.DefaultsFactory()
    android.AddLoadHook(module, powerDefaults)
    return module
}

func powerDefaults(ctx android.LoadHookContext) {
    var version string = ctx.AConfig().PlatformVersionName()
    type props struct {
        Target struct {
            Android struct {
                Shared_libs []string
            }
        }
    }
    p := &props{}
    if(version == "12") {
        p.Target.Android.Shared_libs = append(p.Target.Android.Shared_libs, "android.hardware.power-V1-ndk_platform")
    } else {
        p.Target.Android.Shared_libs = append(p.Target.Android.Shared_libs, "android.hardware.power-V1-ndk")
    }
    ctx.AppendProperties(p)
}
