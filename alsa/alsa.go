package alsa

import (
        "android/soong/android"
        "android/soong/cc"
        "strconv"
)

func init() {
    android.RegisterModuleType("audio_primary_defaults", alsaDefaultsFactory)
}

func alsaDefaultsFactory() (android.Module) {
    module := cc.DefaultsFactory()
    android.AddLoadHook(module, alsaDefaults)
    return module
}

func alsaDefaults(ctx android.LoadHookContext) {
    var cppflags []string
    type props struct {
        Cflags []string
    }

    p := &props{}
    sdkVersion := ctx.Config().PlatformSdkVersionInt()
    if ctx.Config().VendorConfig("IMXPLUGIN").Bool("IMX_CAR") && sdkVersion >= 28 {
        cppflags = append(cppflags, "-DCAR_AUDIO")
    }
    cppflags = append(cppflags, "-DANDROID_SDK_VERSION=" + strconv.Itoa(ctx.AConfig().PlatformSdkVersionInt()))
    p.Cflags = cppflags
    ctx.AppendProperties(p)
}
