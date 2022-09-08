/*
Copyright (C) 2022 DEV47APPS, github.com/dev47apps

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

void map_yuv420_yuyv(uint8_t** data, uint32_t *linesize, uint8_t* dst,
    int shift_x, int shift_y, int is_aligned_128b,
    const int dest_width, const int dest_height,
    const int width, const int height)
{
    uint8_t* src_y = data[0];
    uint8_t* src_u = data[1];
    uint8_t* src_v = data[2];
    const int linesize_dst = width<<1;
    int shift_x2;
    (void) dest_height;

    // dst can only shift in even amounts, pixels come in pairs: yu-yv
    // in case of odd pixel shifts, we need separate left & right shift amounts
    if (shift_x) {
        shift_x2 = dest_width - width - shift_x;
        shift_x  <<= 1;
        shift_x2 <<= 1;
    }

    if (shift_y) {
        dst += (shift_y * dest_width) << 1;
    }

    // Each row N and N+1 use the same UV values (4:2:0 -> 4:2:2)
    #ifdef __SSE2__
    if (is_aligned_128b)
    {
        for (int y = 0; y < (height>>1); ++y) {
            #define CONVERT_ROW \
            if (shift_x) dst += shift_x; \
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
            } \
            if (shift_x) dst += shift_x2;

            CONVERT_ROW
            dst += linesize_dst;
            src_y += linesize[0];

            CONVERT_ROW
            dst += linesize_dst;
            src_y += linesize[0];
            src_u += linesize[1];
            src_v += linesize[2];
        }

        return;
    }
    #else // not __SSE2__
    (void) is_aligned_128b;
    #endif

    for (int y = 0; y < (height>>1); y++) {
        #define CONVERT_ROW \
        if (shift_x) dst += shift_x; \
        for (int x = 0; x < (width>>1); x++) { \
            *dst++ = *src_y++; \
            *dst++ = *src_u++; \
            *dst++ = *src_y++; \
            *dst++ = *src_v++; \
        } \
        if (shift_x) dst += shift_x2;

        uint8_t* src_u0 = src_u;
        uint8_t* src_v0 = src_v;
        CONVERT_ROW

        src_u = src_u0;
        src_v = src_v0;
        CONVERT_ROW
    }

    return;
}

void clear_yuyv(uint8_t* dst, int size, int color) {
    int* ptr = (int*)dst;
    for (int i = 0; i < size / sizeof(int); i++) {
        *ptr++ = color;
    }
}
