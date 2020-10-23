package camera

import (
        "android/soong/android"
        "android/soong/cc"
        "strconv"
)

func init() {
    android.RegisterModuleType("imx_camera_defaults", cameraDefaultsFactory)
}

func cameraDefaultsFactory() (android.Module) {
    module := cc.DefaultsFactory()
    android.AddLoadHook(module, cameraDefaults)
    return module
}

func cameraDefaults(ctx android.LoadHookContext) {
    var cppflags []string
    type props struct {
        Cppflags []string
    }

    p := &props{}
    cppflags = append(cppflags, "-DANDROID_SDK_VERSION=" + strconv.Itoa(ctx.AConfig().PlatformSdkVersionInt()))
    if ctx.Config().VendorConfig("IMXPLUGIN").String("TARGET_GRALLOC_VERSION") == "v4" {
        cppflags = append(cppflags, "-DGRALLOC_VERSION=4")
    }
    p.Cppflags = cppflags
    ctx.AppendProperties(p)
}
