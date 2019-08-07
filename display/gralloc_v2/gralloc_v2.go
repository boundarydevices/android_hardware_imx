package gralloc_v2

import (
        "android/soong/android"
        "android/soong/cc"
        "strings"
        "github.com/google/blueprint/proptools"
)

func init() {
    android.RegisterModuleType("gralloc_v2_defaults", gralloc_v2DefaultsFactory)
}

func gralloc_v2DefaultsFactory() (android.Module) {
    module := cc.DefaultsFactory()
    android.AddLoadHook(module, gralloc_v2Defaults)
    return module
}

func gralloc_v2Defaults(ctx android.LoadHookContext) {
    type props struct {
        Target struct {
                Android struct {
                        Enabled *bool
                }
        }
    }

    p := &props{}
    var board string = ctx.Config().VendorConfig("IMXPLUGIN").String("BOARD_PLATFORM")
    if strings.Contains(board, "imx") && ctx.Config().VendorConfig("IMXPLUGIN").String("TARGET_GRALLOC_VERSION")=="v2" {
        p.Target.Android.Enabled = proptools.BoolPtr(true)
    } else {
        p.Target.Android.Enabled = proptools.BoolPtr(false)
    }
    ctx.AppendProperties(p)
}
