/*
 * Copyright (C) 2010 The Android Open Source Project
 *
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

#include <hardware/hardware.h>
#include <hardware/overlay.h>

#include <fcntl.h>
#include <errno.h>

#include <cutils/log.h>
#include <cutils/atomic.h>
#include <cutils/properties.h>

#include <hardware/hwcomposer.h>

#include <EGL/egl.h>
#include "gralloc_priv.h"
#include "hwc_common.h"
/*****************************************************************************/
using namespace android;

struct hwc_context_t {
    hwc_composer_device_t device;
    /* our private state goes below here */
    //now the blit device may only changed in hwc_composer_device open or close.
    blit_device *blit;

    output_device *m_out[MAX_OUTPUT_DISPLAY];
    char m_using[MAX_OUTPUT_DISPLAY]; //0 indicates no output_device, 1 indicates related index;

    //the system property for dual display and overlay switch.
    int display_mode;
    int display_mode_changed; //the initial value is 0
};

static int hwc_device_open(const struct hw_module_t* module, const char* name,
        struct hw_device_t** device);

static struct hw_module_methods_t hwc_module_methods = {
    open: hwc_device_open
};

hwc_module_t HAL_MODULE_INFO_SYM = {
    common: {
        tag: HARDWARE_MODULE_TAG,
        version_major: 1,
        version_minor: 0,
        id: HWC_HARDWARE_MODULE_ID,
        name: "Sample hwcomposer module",
        author: "The Android Open Source Project",
        methods: &hwc_module_methods,
    }
};

/*****************************************************************************/

static void dump_layer(hwc_layer_t const* l) {
    LOGD("\ttype=%d, flags=%08x, handle=%p, tr=%02x, blend=%04x, {%d,%d,%d,%d}, {%d,%d,%d,%d}",
            l->compositionType, l->flags, l->handle, l->transform, l->blending,
            l->sourceCrop.left,
            l->sourceCrop.top,
            l->sourceCrop.right,
            l->sourceCrop.bottom,
            l->displayFrame.left,
            l->displayFrame.top,
            l->displayFrame.right,
            l->displayFrame.bottom);
}

static int hwc_check_property(hwc_context_t *dev)
{
    //bool bValue = false;
    char value[10];
    int orignMode = dev->display_mode;
    /*note:rw.VIDEO_OVERLAY_DISPLAY means the overlay will be combined to which display.
     *the default value is 0 and it indicates nothing.
     *if the value is 1 and it indicates combined to display0.
     *if the value is 2 and it indicates combined to display1.
    */
    property_get("rw.VIDEO_OVERLAY_DISPLAY", value, "");
    dev->display_mode &= ~(DISPLAY_MODE_OVERLAY_DISP0 | DISPLAY_MODE_OVERLAY_DISP1 |
        				DISPLAY_MODE_OVERLAY_DISP2 | DISPLAY_MODE_OVERLAY_DISP3);
    if (strcmp(value, "1") == 0){
        dev->display_mode |= DISPLAY_MODE_OVERLAY_DISP0;
    }
    else if (strcmp(value, "2") == 0){
        dev->display_mode |= DISPLAY_MODE_OVERLAY_DISP1;
    }

		if (strcmp(value, "3") == 0){
        dev->display_mode |= DISPLAY_MODE_OVERLAY_DISP2;
    }
    else if (strcmp(value, "4") == 0){
        dev->display_mode |= DISPLAY_MODE_OVERLAY_DISP3;
    }
    /*note:rw.VIDEO_DISPLAY means the display device.
     *the default value is 0 and it indicates nothing.
     *if the value is 1 and it indicates display1.
     *if the value is 2 and it indicates display2.
    */
    property_get("sys.VIDEO_DISPLAY", value, "");
    dev->display_mode &= ~(DISPLAY_MODE_DISP1 | DISPLAY_MODE_DISP2);
    if (strcmp(value, "1") == 0){
        dev->display_mode |= DISPLAY_MODE_DISP1;
    }
    if (strcmp(value, "2") == 0){
        dev->display_mode |= DISPLAY_MODE_DISP2;
    }

		if(dev->display_mode ^ orignMode) {
				dev->display_mode_changed = 1;
		}
//HWCOMPOSER_LOG_RUNTIME("*********display_mode=%x, display_mode_changed=%d\n", dev->display_mode, dev->display_mode_changed);
		return 0;
}

static int hwc_modify_property(hwc_context_t *dev, private_handle_t *handle)
{
	handle->usage &= ~GRALLOC_USAGE_OVERLAY_DISPLAY_MASK;

	if(dev->display_mode & DISPLAY_MODE_OVERLAY_DISP0){
			handle->usage |= GRALLOC_USAGE_HWC_OVERLAY_DISP0;
			dev->display_mode &= ~DISPLAY_MODE_OVERLAY_DISP0;
			return 0;
	}
	else if(dev->display_mode & DISPLAY_MODE_OVERLAY_DISP1)
			handle->usage |= GRALLOC_USAGE_HWC_OVERLAY_DISP1;

	if(dev->display_mode & DISPLAY_MODE_OVERLAY_DISP2)
			handle->usage |= GRALLOC_USAGE_HWC_OVERLAY_DISP2;
	else if(dev->display_mode & DISPLAY_MODE_OVERLAY_DISP3)
			handle->usage |= GRALLOC_USAGE_HWC_OVERLAY_DISP3;

	if(dev->display_mode & DISPLAY_MODE_DISP1){
			handle->usage |= GRALLOC_USAGE_HWC_DISP1;
			dev->display_mode &= ~DISPLAY_MODE_DISP1;
	}
	if(dev->display_mode & DISPLAY_MODE_DISP2)
			handle->usage |= GRALLOC_USAGE_HWC_DISP2;
//HWCOMPOSER_LOG_RUNTIME("************handle->usage=%x", handle->usage);
	return 0;
}

/*paramters:
 * usage: devices need to open.
 * ufg:devices not open.
 * puse:index array when device open it need set.
 *check if the output device is exist.
 *return 0 indicates exist; 1 indicates not exist.
*/
static int checkOutputDevice(struct hwc_context_t *ctx, char *puse, int usage, int *ufg)//return -1 indicate not exist.
{
	output_device *out;
	int uFlag = 0;
	int usg = 0;

	for(int i = 0; i < MAX_OUTPUT_DISPLAY; i++) {
		if(ctx->m_using[i]) {
			out = ctx->m_out[i];
			usg = out->getUsage();
			if(usg & usage) {
				uFlag |= (usg & usage);
				if(puse) puse[i] = 1;
			}
		}
	}
	if(ufg != NULL)
		*ufg = usage & ~uFlag;

	return uFlag ^ usage;
}

static int findOutputDevice(struct hwc_context_t *ctx, int *index, int usage, int *ufg)
{
	output_device *out;
	int uFlag = 0;
	int usg = 0;

	for(int i = 0; i < MAX_OUTPUT_DISPLAY; i++) {
		if(ctx->m_using[i]) {
			out = ctx->m_out[i];
			usg = out->getUsage();
			if(usg & usage) {
				uFlag = (usg & usage);
				*index = i;
				break;
			}
		}
	}
	if(ufg != NULL)
		*ufg |= uFlag;

	return (*ufg) ^ usage;
}

static int findEmpytIndex(struct hwc_context_t *ctx)
{
	for(int i = 0; i < MAX_OUTPUT_DISPLAY; i++) {
		if(!ctx->m_using[i])
			return i;
	}

	HWCOMPOSER_LOG_ERR("the output device array not enough big.\n");
	return -1;
}

//check the output device and delete unused device instance.
static void deleteEmtpyIndex(struct hwc_context_t *ctx)
{
	for(int i = 0; i < MAX_OUTPUT_DISPLAY; i++) {
		if(!ctx->m_using[i]) {
			if(ctx->m_out[i]) {
				output_dev_close(ctx->m_out[i]);
				ctx->m_out[i] = NULL;
			}
		}
	}
}

static char* getDeviceName(int usage, int *pUse)
{
    if(usage & GRALLOC_USAGE_HWC_DISP1){
    		*pUse = GRALLOC_USAGE_HWC_DISP1;
    		return (char *)FB2_DEV_NAME;
    }
    if(usage & GRALLOC_USAGE_HWC_OVERLAY_DISP0) {
    		*pUse = GRALLOC_USAGE_HWC_OVERLAY_DISP0;
    		return (char *)FB1_DEV_NAME;
    }
    if(usage & GRALLOC_USAGE_HWC_OVERLAY_DISP1) {
        *pUse = GRALLOC_USAGE_HWC_OVERLAY_DISP1;
        return (char *)FB1_DEV_NAME;
    }//end else if

    return NULL;
}

#if 0
static void setLayerFrame(hwc_layer_t *layer, output_device *out, int usage)
{
    if(usage & GRALLOC_USAGE_HWC_DISP1){
    		layer->displayFrame.left = 0;
    		layer->displayFrame.top = 0;
    		layer->displayFrame.right = out->getWidth();
    		layer->displayFrame.bottom = out->getHeight();
    }
//    if(handle->usage & GRALLOC_USAGE_HWC_OVERLAY0_DISP0) {
//    		display_frame =;
//    }
//    if(handle->usage & GRALLOC_USAGE_HWC_OVERLAY0_DISP1) {
//        display_frame =;
//    }//end else if
}
#endif

static int validate_displayFrame(hwc_layer_t *layer)
{
    int isValid = 0;
    hwc_rect_t *disFrame = &(layer->displayFrame);
    isValid = ((disFrame->left >= 0) && (disFrame->right >= 0) && (disFrame->top >= 0) &&
            (disFrame->bottom >= 0) && ((disFrame->right - disFrame->left) >= 0) &&
            ((disFrame->bottom  - disFrame->top) >= 0));
    return isValid;
}

static int hwc_prepare(hwc_composer_device_t *dev, hwc_layer_list_t* list) {
//#if 1
		//HWCOMPOSER_LOG_RUNTIME("<<<<<<<<<<<<<<<hwc_prepare---1>>>>>>>>>>>>>>>>>\n");
		char out_using[MAX_OUTPUT_DISPLAY] = {0};

//		for(int i = 0; i < MAX_OUTPUT_DISPLAY; i++) {
//				out_using[i] = m_using[i];
//		}

		struct hwc_context_t *ctx = (struct hwc_context_t *)dev;
#if 1
		if(ctx) {
			hwc_check_property(ctx);
		}
#endif
    if (list && dev && ((list->flags & HWC_GEOMETRY_CHANGED) || ctx->display_mode_changed)) {
        for (size_t i=0 ; i<list->numHwLayers ; i++) {
            //dump_layer(&list->hwLayers[i]);
            //list->hwLayers[i].compositionType = HWC_FRAMEBUFFER;
            hwc_layer_t *layer = &list->hwLayers[i];
            /*
             *the private_handle_t should expand to have usage and format member.
            */
		    if (private_handle_t::validate(layer->handle) < 0) {
	    		//HWCOMPOSER_LOG_ERR("it is not a valide buffer handle\n");
	    		continue;
		    }
		    //HWCOMPOSER_LOG_RUNTIME("<<<<<<<<<<<<<<<hwc_prepare---2>>>>>>>>>>>>>>>>>\n");
            private_handle_t *handle = (private_handle_t *)(layer->handle);
            if(!(handle->usage & GRALLOC_USAGE_HWC_OVERLAY)) {
            	//HWCOMPOSER_LOG_RUNTIME("<<<<<<<<<<<<<<<hwc_prepare---usage=%x>>phy=%x>>>>>>>>>>>>>>>\n", handle->usage, handle->phys);
            	continue;
            }
            HWCOMPOSER_LOG_RUNTIME("<<<<<<<<<<<<<<<hwc_prepare---3>usage=%x, phy=%x>>>>>>>>>>>>>>>>\n", handle->usage, handle->phys);
#if 1
        	layer->compositionType = HWC_OVERLAY;
    		//if(handle->usage & GRALLOC_USAGE_HWC_DISP1)
    		//handle the display frame position for tv out.
#endif
#if 1
        	hwc_modify_property(ctx, handle);

            if(!validate_displayFrame(layer)) {
                HWCOMPOSER_LOG_INFO("<<<<<<<<<<<<<<<hwc_prepare---3-2>>>>>>>>>>>>>>>>\n");
                continue;
            }

            int status = -EINVAL;
            int index = 0;
            int retv = 0;
            int m_usage = 0;
            int i_usage = handle->usage & GRALLOC_USAGE_OVERLAY_DISPLAY_MASK;
            //HWCOMPOSER_LOG_ERR("<<<<<<<<<<<<<<<hwc_prepare---3-3>>>>>>>>>>>>>>>>\n");
            retv = checkOutputDevice(ctx, out_using, i_usage, &m_usage);
            while(retv && m_usage) {
		        int ruse = 0;
		        char *dev_name = NULL;
				dev_name = getDeviceName(m_usage, &ruse);
	            m_usage &= ~ruse;
	            HWCOMPOSER_LOG_RUNTIME("<<<<<<<<<<<<<<<hwc_prepare---4>>>>>>>>>>>>>>>>>\n");
	            if(dev_name == NULL) {
					HWCOMPOSER_LOG_INFO("****Warnning: layer buffer usage(%x) does not support!", handle->usage);
					HWCOMPOSER_LOG_INFO("****Warnning:  the layer buffer will be handled in surfaceflinger");
					layer->compositionType = HWC_FRAMEBUFFER;
					continue;
	            }//end else

	            index = findEmpytIndex(ctx);
	            if(index == -1) {
            		HWCOMPOSER_LOG_ERR("Error:findEmpytIndex failed");
            		return HWC_EGL_ERROR;
	            }
	            if(ctx->m_out[index])
	            		deleteEmtpyIndex(ctx);

		        status = output_dev_open(dev_name, &(ctx->m_out[index]), ruse);
		        if(status < 0){
		        	  HWCOMPOSER_LOG_ERR("Error! open output device failed!");
		        	  continue;
		        }//end if
		        out_using[index] = 1;
		        ctx->m_using[index] = 1;
		        //setLayerFrame(layer, ctx->m_out[index], ruse);
            }//end while
#endif
        }//end for
#if 1
        ctx->display_mode_changed = 0;
	    for(int i = 0; i < MAX_OUTPUT_DISPLAY; i++) {
			if(!out_using[i] && ctx->m_using[i]) {
				ctx->m_using[i] = 0;
				deleteEmtpyIndex(ctx);
			}
			//ctx->m_using[i] = out_using[i];
		}
#endif
    }//end if

//#endif
    return 0;
}

static int releaseAllOutput(struct hwc_context_t *ctx)
{
		for(int i = 0; i < MAX_OUTPUT_DISPLAY; i++) {
				if(ctx->m_using[i]) {
						output_dev_close(ctx->m_out[i]);
						ctx->m_using[i] = 0;
						ctx->m_out[i] = NULL;
				}
		}

		return 0;
}

static int getActiveOuputDevice(struct hwc_context_t *ctx)
{
		int num = 0;
		for(int i = 0; i < MAX_OUTPUT_DISPLAY; i++) {
				if(ctx->m_out[i] && ctx->m_using[i])
						num ++;
		}

		return num;
}

static int hwc_set(hwc_composer_device_t *dev,
        hwc_display_t dpy,
        hwc_surface_t sur,
        hwc_layer_list_t* list)
{
		//HWCOMPOSER_LOG_RUNTIME("==============hwc_set=1==============\n");
    struct hwc_context_t *ctx = (struct hwc_context_t *)dev;
    //for (size_t i=0 ; i<list->numHwLayers ; i++) {
    //    dump_layer(&list->hwLayers[i]);
    //}
    //hwc_buffer *outBuff[MAX_OUTPUT_DISPLAY];
    //when displayhardware do releas function, it will come here.
#if 1
    if(ctx && (dpy == NULL) && (sur == NULL) && (list == NULL)) {
		//close the output device.
		releaseAllOutput(ctx);
		ctx->display_mode_changed = 1;

		return 0;
    }
#endif
		//HWCOMPOSER_LOG_RUNTIME("==============hwc_set=2==============\n");
#if 1 
    EGLBoolean sucess = eglSwapBuffers((EGLDisplay)dpy, (EGLSurface)sur);
    if (!sucess) {
        return HWC_EGL_ERROR;
    }
#endif
    if(list == NULL || dev == NULL) {
    	return 0;
    }
#if 1
 		//HWCOMPOSER_LOG_RUNTIME("==============hwc_set=3==============\n");
    if(getActiveOuputDevice(ctx) == 0) {return 0;}//eglSwapBuffers((EGLDisplay)dpy, (EGLSurface)sur); return 0;}

    int status = -EINVAL;
	HWCOMPOSER_LOG_RUNTIME("==============hwc_set=4==============\n");
	hwc_buffer out_buffer[MAX_OUTPUT_DISPLAY];
	memset(out_buffer, 0, sizeof(out_buffer));
	for(int i = 0; i < MAX_OUTPUT_DISPLAY; i++) {
		if(ctx->m_using[i] && ctx->m_out[i])
			status = ctx->m_out[i]->fetch(&out_buffer[i]);
	}

    blit_device *bltdev = ctx->blit;
    for (size_t i=0 ; i<list->numHwLayers ; i++){
		hwc_layer_t *layer = &list->hwLayers[i];
	    if (private_handle_t::validate(layer->handle) < 0) {
    		//HWCOMPOSER_LOG_INFO("2--it is not a valide buffer handle\n");
    		continue;
	    }

        if(!validate_displayFrame(layer)) {
            continue;
        }

		private_handle_t *handle = (private_handle_t *)(layer->handle);
		if(handle->usage & GRALLOC_USAGE_HWC_OVERLAY){
            int retv = 0;
            int m_usage = 0;
            int i_usage = handle->usage & GRALLOC_USAGE_OVERLAY_DISPLAY_MASK;
    	    HWCOMPOSER_LOG_RUNTIME("==============hwc_set=5==============\n");
            do {
    			output_device *outdev = NULL;
    			int index = 0;
        		retv = findOutputDevice(ctx, &index, i_usage, &m_usage);
                if((index >= 0) && (index < MAX_OUTPUT_DISPLAY)) {
                	outdev = ctx->m_out[index];
                }
    			if(outdev != NULL) {
    				status = bltdev->blit(layer, &(out_buffer[index]));
    				if(status < 0){
    					HWCOMPOSER_LOG_ERR("Error! bltdev->blit() failed!");
    					continue;
    				}
    			}//end if(outdev != NULL)
            }while(retv);

		}//end if
    }//end for

    for(int i = 0; i < MAX_OUTPUT_DISPLAY; i++) {
				if(ctx->m_using[i]) {
						status = ctx->m_out[i]->post(&out_buffer[i]);
						if(status < 0){
								HWCOMPOSER_LOG_ERR("Error! output device post buffer failed!");
								continue;
						}
				}
		}
#endif
    return 0;
}

static int hwc_device_close(struct hw_device_t *dev)
{
    struct hwc_context_t* ctx = (struct hwc_context_t*)dev;
    if (ctx) {
    		if(ctx->blit)
    				blit_dev_close(ctx->blit);
        releaseAllOutput(ctx);
        free(ctx);
    }
    return 0;
}

/*****************************************************************************/

static int hwc_device_open(const struct hw_module_t* module, const char* name,
        struct hw_device_t** device)
{
    int status = -EINVAL;
    if (!strcmp(name, HWC_HARDWARE_COMPOSER)) {
        struct hwc_context_t *dev;
        dev = (hwc_context_t*)malloc(sizeof(*dev));

        /* initialize our state here */
        memset(dev, 0, sizeof(*dev));

        /* initialize the procs */
        dev->device.common.tag = HARDWARE_DEVICE_TAG;
        dev->device.common.version = 0;
        dev->device.common.module = const_cast<hw_module_t*>(module);
        dev->device.common.close = hwc_device_close;

        dev->device.prepare = hwc_prepare;
        dev->device.set = hwc_set;

        *device = &dev->device.common;

        /* our private state goes below here */
        status = blit_dev_open(BLIT_IPU, &(dev->blit));
        if(status < 0){
        	  HWCOMPOSER_LOG_ERR("Error! blit_dev_open failed!");
        	  goto err_exit;
        }
HWCOMPOSER_LOG_RUNTIME("<<<<<<<<<<<<<<<hwc_device_open>>>>>>>>>>>>>>>>>\n");
        return 0;
err_exit:
				if(dev){
						if(dev->blit) {
								blit_dev_close(dev->blit);
						}
					  free(dev);
				}
				//status = -EINVAL;
        /****************************************/
    }
    return status;
}
