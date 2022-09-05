package hwsecure_client

import (
        "android/soong/android"
        "android/soong/cc"
)

func init() {
    android.RegisterModuleType("libhwsecureclient_defaults", libhwsecureclientDefaultsFactory)
}

func libhwsecureclientDefaultsFactory() (android.Module) {
    module := cc.DefaultsFactory()
    android.AddLoadHook(module, libhwsecureclientDefault)
    return module
}

func libhwsecureclientDefault(ctx android.LoadHookContext) {
    type props struct {
        Target struct {
                Android struct {
                        Cflags []string
                }
        }
    }
    p := &props{}
    if ctx.Config().VendorConfig("IMXPLUGIN").String("BOARD_SOC_TYPE") == "IMX8MP" {
         p.Target.Android.Cflags = append(p.Target.Android.Cflags, "-DSUPPORT_WIDEVINE_L1")
    }
    ctx.AppendProperties(p)
}
