__kernel void g2d_yuyv_to_nv12(__global const uchar8 *input,
        __global uchar4 *output_y,
        __global uchar4 *output_uv,
        int src_width,
        int src_height,
        int dst_width,
        int dst_height)
{
    int x = get_global_id(0);
    int y = get_global_id(1);
    int index = y*src_width + x;
    uchar4 *uv_buf = output_uv + index/2;
    uchar4 *y_buf = output_y + index;
    uchar8 p_yuyv = *(input + index);
    /*
     *y_buf = p_yuyv.even;
     */
    (*y_buf).x = p_yuyv.s0;
    (*y_buf).y = p_yuyv.s2;
    (*y_buf).z = p_yuyv.s4;
    (*y_buf).w = p_yuyv.s6;
    if (!(y & 0x1)){
       /*
        *uv_buf = p_yuyv.odd;
       */
        int uv_index = y/2*src_width + x;
        uchar4 *uv_buf = output_uv + uv_index;
        (*uv_buf).x = p_yuyv.s1;
        (*uv_buf).y = p_yuyv.s3;
        (*uv_buf).z = p_yuyv.s5;
        (*uv_buf).w = p_yuyv.s7;
    }
}

__kernel void g2d_yuyv_to_yuyv(__global const uint4 *input,
        __global uint4 *output,
        int src_width,
        int src_height,
        int dst_width,
        int dts_height)
{
    int x = get_global_id(0);
    int y = get_global_id(1);
    int output_index = y*dst_width + x;
    uint4 *output_buf = output + output_index;
    if (x >= src_width){
        *output_buf = (uint4)(0x80008000, 0x80008000, 0x80008000, 0x80008000);
    }
    else {
        int input_index = y*src_width + x;
        uint4 *input_buf = input + input_index;
        *output_buf = *input_buf;
    }
}

__kernel void g2d_mem_copy(__global const uint16 *input,
        __global uint16 *output,
        int size)
{
    int x = get_global_id(0);
    *(output + x) = *(input + x);
}

