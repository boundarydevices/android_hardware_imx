#ifndef _BLIT_GPU_H_
#define _BLIT_GPU_H_

#include <hardware/hardware.h>

#include <fcntl.h>
#include <errno.h>

#include <cutils/log.h>
#include <cutils/atomic.h>

#include <hardware/hwcomposer.h>

#include <EGL/egl.h>
#include "gralloc_priv.h"
#include "hwc_common.h"
/*****************************************************************************/

class blit_gpu : public blit_device{
public:  
    virtual int blit(hwc_layer_t *layer, hwc_buffer *out_buf);

		blit_gpu();
		virtual ~blit_gpu();
    
private:
		int init();
    int uninit();
	
		blit_gpu& operator = (blit_gpu& out);
		blit_gpu(const blit_gpu& out);  
    //add private members.		    
};


//int gpu_init(struct blit_device *dev);
//
//int gpu_uninit(struct blit_device*dev);
//
//int gpu_blit(struct blit_device *dev, hwc_layer_t *layer, hwc_buffer *out_buf);

#endif
