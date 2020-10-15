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
        Cflags []string
    }

    p := &props{}
    cppflags = append(cppflags, "-DANDROID_SDK_VERSION=" + strconv.Itoa(ctx.AConfig().PlatformSdkVersionInt()))
    p.Cflags = cppflags
    ctx.AppendProperties(p)
}
