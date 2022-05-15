/*
Copyright (C) 2021 DEV47APPS, github.com/dev47apps

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <stdint.h>

#ifdef _MSC_VER
#if (defined(_M_AMD64) || defined(_M_X64) || (_M_IX86_FP == 2))
#define __SSE2__ // MSVC does not have __SSE2__ macro
#endif
#include <emmintrin.h>
#else
#include <x86intrin.h>
#endif

void convert_yuv420_yuyv(uint8_t** data, uint32_t *linesize,
    uint8_t* dst, const int width, const int height)
{
    uint8_t* src_y = data[0];
    uint8_t* src_u = data[1];
    uint8_t* src_v = data[2];
    const int linesize_dst = width<<1;

    // Each row N and N+1 use the same UV values (4:2:0 -> 4:2:2)
    #ifdef __SSE2__
    for (int y = 0; y < (height>>1); ++y) {
        #define CONVERT_ROW \
        for (int x = 0; x < width; x += 16) {        \
            __m128i y = _mm_load_si128((__m128i*)(src_y + x));    \
            __m128i u = _mm_loadl_epi64((__m128i*)(src_u + (x>>1))); \
            __m128i v = _mm_loadl_epi64((__m128i*)(src_v + (x>>1))); \
            \
            __m128i uv = _mm_unpacklo_epi8(u, v);                 \
            __m128i yuv0 = _mm_unpacklo_epi8(y, uv);              \
            __m128i yuv1 = _mm_unpackhi_epi8(y, uv);              \
            _mm_stream_si128((__m128i*)(dst + (x<<1)), yuv0);      \
            \
            _mm_stream_si128((__m128i*)(dst + (x<<1) + 16), yuv1); \
        }


        CONVERT_ROW
        dst += linesize_dst;
        src_y += linesize[0];

        CONVERT_ROW
        dst += linesize_dst;
        src_y += linesize[0];
        src_u += linesize[1];
        src_v += linesize[2];
    }
    #else // not __SSE2__
    for (int y = 0; y < (height>>1); y++) {
        const uint8_t* src_u0 = src_u;
        const uint8_t* src_v0 = src_v;

        #define CONVERT_ROW \
        for (int x = 0; x < (width>>1); x++) { \
            *dst++ = *src_y++; \
            *dst++ = *src_u++; \
            *dst++ = *src_y++; \
            *dst++ = *src_v++; \
        }

        CONVERT_ROW

        src_u = src_u0;
        src_v = src_v0;
        CONVERT_ROW
    }
    #endif
}

void scale_yuv420_yuyv(uint8_t** data, uint32_t *linesize, uint8_t* dst,
    const int src_w, const int src_h,
    const int dst_w, const int dst_h,
    int shift_x, int shift_y)
{
    const uint8_t* src_y = data[0];
    const uint8_t* src_u = data[1];
    const uint8_t* src_v = data[2];

    const int src_w_d4 = src_w >> 2;
    const int src_h_d4 = src_h >> 2;
    const int dst_w_d4 = dst_w >> 2;
    const int dst_h_d4 = dst_h >> 2;
    register int uv_flip = 0;

    if (shift_x) {
        shift_x &= ~1; // force even
        shift_x <<= 1;
    }

    if (shift_y) {
        shift_y &= ~1;
        dst += (shift_y * dst_w ) << 1;
    }

    for (int y = 0; y < dst_h; y++) {
        const int src_line     = (y   ) * src_h    / dst_h    * src_w;
        const int src_line_d4  = (y>>2) * src_h_d4 / dst_h_d4 * src_w;
        dst += shift_x;

        for (int x = 0; x < dst_w; x++) {
            const int src_x    = (x   ) * src_w    / dst_w;
            const int src_x_d4 = (x>>2) * src_w_d4 / dst_w_d4;

            const uint8_t* src_uv = uv_flip ? src_v : src_u;
            *(dst++) = src_y  [src_line    + (src_x      )];
            *(dst++) = src_uv [src_line_d4 + (src_x_d4<<1)];
            uv_flip ^= 1;
        }
        dst += shift_x;
    }
}

void clear_yuyv(uint8_t* dst, int size, int color) {
    int* ptr = (int*)dst;
    for (int i = 0; i < size / sizeof(int); i++) {
        *ptr++ = color;
    }
}
