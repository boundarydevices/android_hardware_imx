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

package opencl_2d

import (
        "android/soong/android"
        "android/soong/cc"
        "strings"
        "github.com/google/blueprint/proptools"
)

func init() {
    android.RegisterModuleType("libopencl_2d_defaults", libopencl_2dDefaultsFactory)
    android.RegisterModuleType("opencl_2d_test_defaults", opencl_2d_testDefaultsFactory)
}

func libopencl_2dDefaultsFactory() (android.Module) {
    module := cc.DefaultsFactory()
    android.AddLoadHook(module, libopencl_2dDefaults)
    return module
}

func libopencl_2dDefaults(ctx android.LoadHookContext) {
    type props struct {
        Target struct {
                Android struct {
                        Enabled *bool
                }
        }
    }

    p := &props{}
    var board string = ctx.Config().VendorConfig("IMXPLUGIN").String("BOARD_PLATFORM")
    var have_opencl_2d bool = ctx.Config().VendorConfig("IMXPLUGIN").Bool("TARGET_OPENCL_2D")
    if strings.Contains(board, "imx") && have_opencl_2d {
        p.Target.Android.Enabled = proptools.BoolPtr(true)
    } else {
        p.Target.Android.Enabled = proptools.BoolPtr(false)
    }
    ctx.AppendProperties(p)
}

func opencl_2d_testDefaultsFactory() (android.Module) {
    module := cc.DefaultsFactory()
    android.AddLoadHook(module, opencl_2d_testDefaults)
    return module
}

func opencl_2d_testDefaults(ctx android.LoadHookContext) {
    type props struct {
        Target struct {
                Android struct {
                        Enabled *bool
                }
        }
    }

    p := &props{}
    var board string = ctx.Config().VendorConfig("IMXPLUGIN").String("BOARD_PLATFORM")
    var have_opencl_2d bool = ctx.Config().VendorConfig("IMXPLUGIN").Bool("TARGET_OPENCL_2D")
    if strings.Contains(board, "imx") && have_opencl_2d {
        p.Target.Android.Enabled = proptools.BoolPtr(true)
    } else {
        p.Target.Android.Enabled = proptools.BoolPtr(false)
    }
    ctx.AppendProperties(p)
}
