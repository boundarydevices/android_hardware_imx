#ifndef _BLIT_IPU_H_
#define _BLIT_IPU_H_

#include <hardware/hardware.h>

#include <fcntl.h>
#include <errno.h>

#include <cutils/log.h>
#include <cutils/atomic.h>

#include <hardware/hwcomposer.h>

#include <EGL/egl.h>
#include "gralloc_priv.h"
#include "hwc_common.h"
extern "C" {
#include "mxc_ipu_hl_lib.h"
}
/*****************************************************************************/

#define BLIT_PIXEL_FORMAT_RGB_565  209

class blit_ipu : public blit_device
{
public:
    virtual int blit(hwc_layer_t *layer, hwc_buffer *out_buf);

		blit_ipu();
		virtual ~blit_ipu();

private:
		ipu_lib_input_param_t  mIPUInputParam;
    ipu_lib_output_param_t mIPUOutputParam;
    ipu_lib_handle_t       mIPUHandle;
//    int                    mIPURet;
private:
		int init();
    int uninit();

		blit_ipu& operator = (blit_ipu& out);
		blit_ipu(const blit_ipu& out);
};


//int ipu_init(struct blit_device *dev);
//
//int ipu_uninit(struct blit_device*dev);
//
//int ipu_blit(struct blit_device *dev, hwc_layer_t *layer, hwc_buffer *out_buf);

#endif // _BLIT_IPU_H_
