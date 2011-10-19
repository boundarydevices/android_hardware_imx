/*
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*Copyright 2009-2011 Freescale Semiconductor, Inc. All Rights Reserved.*/


#include <hardware/hardware.h>

#include <fcntl.h>
#include <errno.h>

#include <cutils/log.h>
#include <cutils/atomic.h>

#include <hardware/hwcomposer.h>

#include <EGL/egl.h>
#include "gralloc_priv.h"
#include "hwc_common.h"
#include "blit_ipu.h"

/*****************************************************************************/
using namespace android;

int blit_device::isIPUDevice(const char *dev_name)
{
		return !strcmp(dev_name, BLIT_IPU);
}

int blit_device::isGPUDevice(const char *dev_name)
{
		return !strcmp(dev_name, BLIT_GPU);
}

blit_ipu::blit_ipu()
{
    memset(&mTask, 0, sizeof(mTask));
	init();
}

blit_ipu::~blit_ipu()
{
	uninit();
}

int blit_ipu::init()//, hwc_layer_t *layer, struct output_device *output
{
	int status = -EINVAL;
    mIpuFd = open("/dev/mxc_ipu", O_RDWR, 0);
    if(mIpuFd < 0) {
        HWCOMPOSER_LOG_ERR("%s:%d,open ipu dev failed", __FUNCTION__, __LINE__);
        return status;
    }

    return 0;
}

int blit_ipu::uninit()
{
	//int status = -EINVAL;
    if(mIpuFd)
        close(mIpuFd);

	return 0;
}

static void fill_buffer(char *pbuf, int len)
{
    static int k = 0;
    short * pframe = (short *)pbuf;
    if(k == 0) {
        for(int i=0; i<len; i+=2) {
            *pframe = 0xf800;
        }
    }

    if(k == 1){
        for(int i=0; i<len; i+=2) {
            *pframe = 0x001f;
        }
    }

    if(k == 2){
        for(int i=0; i<len; i+=2) {
            *pframe = 0x07E0;
        }
    }

    k = (k+1)%3;
}

int blit_ipu::blit(hwc_layer_t *layer, hwc_buffer *out_buf)
{
	  int status = -EINVAL;
	  if(mIpuFd < 0 || layer == NULL || out_buf == NULL){
	  	  HWCOMPOSER_LOG_ERR("Error!invalid parameters!");
	  	  return status;
	  }
	  //struct blit_ipu *ipu = (struct blit_ipu *)dev;

      HWCOMPOSER_LOG_RUNTIME("^^^^^^^^^^^^^^^blit_ipu::blit()^^^^^^^^^^^^^^^^^^^^^^");
	  hwc_rect_t *src_crop = &(layer->sourceCrop);
	  hwc_rect_t *disp_frame = &(layer->displayFrame);
	  private_handle_t *handle = (private_handle_t *)(layer->handle);

    //fill_buffer((char *)(handle->base), handle->size);

    mTask.input.width = handle->width;//src_crop->right - src_crop->left;
    mTask.input.height = handle->height;//src_crop->bottom - src_crop->top;
    mTask.input.crop.pos.x = src_crop->left;
    mTask.input.crop.pos.y = src_crop->top;
    mTask.input.crop.w = src_crop->right - src_crop->left;
    mTask.input.crop.h = src_crop->bottom - src_crop->top;

    if(handle->format == HAL_PIXEL_FORMAT_YCbCr_420_SP || handle->format == BLIT_PIXEL_FORMAT_RGB_565) {
        HWCOMPOSER_LOG_RUNTIME("^^^^^^^^handle->format= NV12");
        mTask.input.format = v4l2_fourcc('N', 'V', '1', '2');
    }
    else if(handle->format == HAL_PIXEL_FORMAT_YCbCr_420_I) {
        HWCOMPOSER_LOG_RUNTIME("^^^^^^^^handle->format= I420");
        mTask.input.format = v4l2_fourcc('I', '4', '2', '0');
    }
    else if((handle->format == HAL_PIXEL_FORMAT_RGB_565) || (handle->format == BLIT_PIXEL_FORMAT_RGB_565)) {
        HWCOMPOSER_LOG_RUNTIME("^^^^^^^^handle->format= RGBP");
        mTask.input.format = v4l2_fourcc('R', 'G', 'B', 'P');
        //mTask.input.fmt = v4l2_fourcc('N', 'V', '1', '2');
    }else{
        HWCOMPOSER_LOG_ERR("Error!Not supported input format %d",handle->format);
        return status;
    }

    mTask.input.paddr = handle->phys;
    //out_buf should has width and height to be checked with the display_frame.
    mTask.output.format = out_buf->format;//v4l2_fourcc('U', 'Y', 'V', 'Y');
    if(out_buf->usage & GRALLOC_USAGE_DISPLAY_MASK) {
	    mTask.output.width = out_buf->width;
	    mTask.output.height = out_buf->height;
		mTask.output.crop.pos.x = 0;
		mTask.output.crop.pos.y = 0;
		mTask.output.crop.w = out_buf->width;
		mTask.output.crop.h = out_buf->height;
    }
    else {
	    mTask.output.width = out_buf->width;//disp_frame->right - disp_frame->left;
	    mTask.output.height = out_buf->height;//disp_frame->bottom - disp_frame->top;
	    mTask.output.crop.pos.x = (disp_frame->left >> 3) << 3;
	    mTask.output.crop.pos.y = (disp_frame->top >> 3) << 3;
	    mTask.output.crop.w = ((disp_frame->right - disp_frame->left) >> 3) << 3;
	    mTask.output.crop.h = ((disp_frame->bottom - disp_frame->top) >> 3) << 3;
  	}
    mTask.output.rotate = layer->transform;
    mTask.output.paddr = out_buf->phy_addr;
    int ret = IPU_CHECK_ERR_INPUT_CROP; 
    
    while(ret != IPU_CHECK_OK && ret > IPU_CHECK_ERR_MIN) {
        ret = ioctl(mIpuFd, IPU_CHECK_TASK, &mTask);
        HWCOMPOSER_LOG_RUNTIME("%s:%d, IPU_CHECK_TASK ret=%d", __FUNCTION__, __LINE__, ret);
        switch(ret) {
            case IPU_CHECK_OK:
                break;
            case IPU_CHECK_ERR_SPLIT_INPUTW_OVER:
                mTask.input.crop.w -= 8;
                break;
            case IPU_CHECK_ERR_SPLIT_INPUTH_OVER:
                mTask.input.crop.h -= 8;
                break;
            case IPU_CHECK_ERR_SPLIT_OUTPUTW_OVER:
                mTask.output.crop.w -= 8;
                break;
            case IPU_CHECK_ERR_SPLIT_OUTPUTH_OVER:
                mTask.output.crop.h -= 8;;
                break;
            default:
                HWCOMPOSER_LOG_ERR("%s:%d, IPU_CHECK_TASK ret=%d", __FUNCTION__, __LINE__, ret);
                return status;
        }
    }

    //if(out_buf->usage & GRALLOC_USAGE_DISPLAY_MASK)
        //status = mxc_ipu_lib_task_init(&mTask.input,NULL,&mTask.output,OP_NORMAL_MODE|TASK_PP_MODE,&mIPUHandle);
    //else
        //status = mxc_ipu_lib_task_init(&mTask.input,NULL,&mTask.output,OP_NORMAL_MODE|TASK_ENC_MODE,&mIPUHandle);
      status = ioctl(mIpuFd, IPU_QUEUE_TASK, &mTask);
	  if(status < 0) {
	  		HWCOMPOSER_LOG_ERR("%s:%d, IPU_QUEUE_TASK failed %d", __FUNCTION__, __LINE__ ,status);
	  		return status;
	  }
	  status = 0;
      HWCOMPOSER_LOG_RUNTIME("^^^^^^^^^^^^^^^blit_ipu::blit()^^end^^^^^^^^^^^^^^^^^^^^");
	  return status;
}
