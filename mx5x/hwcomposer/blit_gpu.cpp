

#include <hardware/hardware.h>

#include <fcntl.h>
#include <errno.h>

#include <cutils/log.h>
#include <cutils/atomic.h>

#include <hardware/hwcomposer.h>

#include <EGL/egl.h>
#include "gralloc_priv.h"
#include "hwc_common.h"
#include "blit_gpu.h"
/*****************************************************************************/
using namespace android;

blit_gpu::blit_gpu()
{
		init();
}

blit_gpu::~blit_gpu()
{
		uninit();
}

int blit_gpu::init()
{
		return 0;
}

int blit_gpu::uninit()
{
		return 0;
}

int blit_gpu::blit(hwc_layer_t *layer, hwc_buffer *out_buf)
{
		return 0;
}
