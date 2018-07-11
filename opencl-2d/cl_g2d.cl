
typedef struct _pix5
{
    uchar pix[5];
} pix5;

__kernel void nv12_10bit_tiled_to_linear(__global const uchar *input_y,
        __global const uchar *input_uv, __global const uchar *output_y,
        __global const uchar *output_uv, int src_stride, int width, int height)
{
    short x1 = get_global_id(0);
    short y = get_global_id(1);
    pix5 ypix, upix;
    short x = x1 * 5;
    y = y << 1;
    int dst_index = x1 * 8 + y * width;
    int dst_index2 = x1 * 8 + (y+1) * width;
    x = x << 1;
    int src_index1 = (x/8) * 8 * 128 + (y&127) * 8 + (y / 128) * src_stride * 128;
    uchar byte_loc = (x&7);
    uchar byte_cnt = 8 - byte_loc;
    uchar i;
    if (y < (height >> 1)) {
        for(i=0; i<byte_cnt&&i<5; i++) {
            ypix.pix[i] = *(input_y + src_index1 + byte_loc + i);
            upix.pix[i] = *(input_uv + src_index1 + byte_loc + i);
        }
    }
    else {
        for(i=0; i<byte_cnt&&i<5; i++) {
            ypix.pix[i] = *(input_y + src_index1 + byte_loc + i);
        }
    }

    if (byte_cnt < 5) {
        int src_index2 = (x/8+1) * 8 * 128 + (y&127) * 8 + (y / 128) * src_stride * 128;
        if (y < (height >> 1)) {
            for(uchar k=0;i<5;i++,k++) {
                ypix.pix[i] = *(input_y + src_index2 + k);
                upix.pix[i] = *(input_uv + src_index2 + k);
            }
        }
        else {
            for(uchar k=0;i<5;i++,k++) {
                ypix.pix[i] = *(input_y + src_index2 + k);
            }
        }
    }
    ushort pix = 0;
    uchar bit_loc = 0;
    uchar bit_pos = 0;
    if (y < (height >> 1)) {
        for (i=0; i<4; i++) {
            bit_pos = i * 10;
            byte_loc = bit_pos >> 3;
            bit_loc = bit_pos - (byte_loc << 3);
            pix = ((ypix.pix[byte_loc] << 8) |
                 ypix.pix[(byte_loc + 1)]);
            pix = pix >> (8 - bit_loc);
            *(output_y + dst_index + 2*i) = pix & 0xff;
            *(output_y + dst_index + 2*i + 1) = pix & 0xff;
            *(output_y + dst_index2 + 2*i) = pix & 0xff;
            *(output_y + dst_index2 + 2*i+1) = pix & 0xff;

            pix = (upix.pix[byte_loc] << 8) |
                 (upix.pix[byte_loc + 1]);
            pix = pix >> (8 - bit_loc);
            *(output_uv + dst_index + i) = pix & 0xff;
            *(output_uv + dst_index + i + 4) = pix & 0xff;
            *(output_uv + dst_index2 + i) = pix & 0xff;
            *(output_uv + dst_index2 + i + 4) = pix & 0xff;
        }
    }
    else {
        for (i=0; i<4; i++) {
            bit_pos = i * 10;
            byte_loc = bit_pos >> 3;
            bit_loc = bit_pos - (byte_loc << 3);
            pix = ((ypix.pix[byte_loc] << 8) |
                 ypix.pix[(byte_loc + 1)]);
            pix = pix >> (8 - bit_loc);
            *(output_y + dst_index + 2*i) = pix & 0xff;
            *(output_y + dst_index + 2*i + 1) = pix & 0xff;
            *(output_y + dst_index2 + 2*i) = pix & 0xff;
            *(output_y + dst_index2 + 2*i+1) = pix & 0xff;
        }
    }
}

__kernel void nv12_tiled_to_linear(__global const uchar8 *input_y,
        __global const uchar8 *input_uv, __global uchar8 *output_y,
        __global uchar8 *output_uv, int src_stride, int width, int height)
{
    int x = get_global_id(0);
    int y = get_global_id(1) * 2;
    int src_index = x * 8 * 128 + (y&127) * 8 + (y / 128) * src_stride * 128;
    int dst_index1 = x * 8 + y * width;
    int dst_index2 = x * 8 + (y+1) * width;

    *(output_y + (dst_index1 >> 3)) = *(input_y + (src_index >> 3));
    *(output_y + (dst_index2 >> 3)) = *(input_y + (src_index >> 3) + 1);
    if (y < (height >> 1)) {
        *(output_uv + (dst_index1 >> 3)) = *(input_uv + (src_index >> 3));
        *(output_uv + (dst_index2 >> 3)) = *(input_uv + (src_index >> 3) + 1);
    }
}

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

__kernel void g2d_nv12_to_nv21(__global const uchar8 *input_y,
        __global const uchar4 *input_uv,
        __global uchar8 *output_y,
        __global uchar4 *output_uv,
        int width,
        int height,
        int src_stride,
        int dst_stride)
{
    int x = get_global_id(0);
    int y = get_global_id(1);
    if((x+1)*8 <= width) {
        int src_index = y*src_stride + x*8;
        int dst_index = y*dst_stride + x*8;
        uchar4 *dst_uv_buf = output_uv + dst_index/8;
        uchar8 *dst_y_buf = output_y + dst_index/8;
        uchar4 *src_uv_buf = input_uv + src_index/8;
        uchar8 *src_y_buf = input_y + src_index/8;

        (*dst_y_buf) = (*src_y_buf);
        (*dst_uv_buf).x = (*src_uv_buf).y;
        (*dst_uv_buf).y = (*src_uv_buf).x;
        (*dst_uv_buf).z = (*src_uv_buf).w;
        (*dst_uv_buf).w = (*src_uv_buf).z;
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

