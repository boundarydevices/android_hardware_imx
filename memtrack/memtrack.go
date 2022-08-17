// Copyright 2019-2022 NXP
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
package memtrack

import (
        "android/soong/android"
        "android/soong/cc"
        "strings"
        "github.com/google/blueprint/proptools"
)

func init() {
    android.RegisterModuleType("memtrack_defaults", memtrackDefaultsFactory)
}

func memtrackDefaultsFactory() (android.Module) {
    module := cc.DefaultsFactory()
    android.AddLoadHook(module, memtrackDefaults)
    return module
}

func memtrackDefaults(ctx android.LoadHookContext) {
    type props struct {
        Target struct {
                Android struct {
                        Enabled *bool
                        Shared_libs []string
                }
        }
    }

    p := &props{}
    var board string = ctx.Config().VendorConfig("IMXPLUGIN").String("BOARD_PLATFORM")
    if strings.Contains(board, "imx") {
        p.Target.Android.Enabled = proptools.BoolPtr(true)
    } else {
        p.Target.Android.Enabled = proptools.BoolPtr(false)
    }

    var version string = ctx.AConfig().PlatformVersionName()
    if(version == "12") {
        p.Target.Android.Shared_libs = append(p.Target.Android.Shared_libs, "android.hardware.memtrack-V1-ndk_platform");
    } else {
        p.Target.Android.Shared_libs = append(p.Target.Android.Shared_libs, "android.hardware.memtrack-V1-ndk")
    }
    ctx.AppendProperties(p)
}

