// license:BSD-3-Clause
// copyright-holders:Ryan Holtz

#ifndef _VIDEO_N64TYPES_H_
#define _VIDEO_N64TYPES_H_

#include "rgbutil.h"

struct misc_state_t {
	misc_state_t() {
		m_max_level = 0;
		m_min_level = 0;
	}

	int32_t m_fb_format;          // Framebuffer pixel format index (0 - I, 1 - IA, 2 - CI, 3 - RGBA)
	int32_t m_fb_size;            // Framebuffer pixel size index (0 - 4bpp, 1 - 8bpp, 2 - 16bpp, 3 - 32bpp)
	int32_t m_fb_width;           // Framebuffer width, in pixels
	int32_t m_fb_height;          // Framebuffer height, in pixels
	uint32_t m_fb_address;        // Framebuffer source address offset (in bytes) from start of RDRAM

	uint32_t m_zb_address;        // Z-buffer source address offset (in bytes) from start of RDRAM

	int32_t m_ti_format;          // Format for Texture Interface (TI) transfers
	int32_t m_ti_size;            // Size (in bytes) of TI transfers
	int32_t m_ti_width;           // Width (in pixels) of TI transfers
	uint32_t m_ti_address;        // Destination address for TI transfers

	int32_t m_max_level;         // Maximum LOD level for texture filtering
	int32_t m_min_level;         // Minimum LOD level for texture filtering

	uint16_t m_primitive_z;       // Forced Z value for current primitive, if applicable
	uint16_t m_primitive_dz;      // Forced Delta-Z value for current primitive, if applicable
};

#if 0
class color_t {
public:
	color_t() {
		c = 0;
	}

	color_t(uint32_t color) {
		set(color);
	}

	color_t(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
		set(a, r, g, b);
	}

	inline void set(color_t& other) {
		c = other.c;
	}

	inline void set(uint32_t color) {
		i.a = (color >> 24) & 0xff;
		i.r = (color >> 16) & 0xff;
		i.g = (color >> 8) & 0xff;
		i.b = color & 0xff;
	}

	void set(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
		i.a = a;
		i.r = r;
		i.g = g;
		i.b = b;
	}

	inline void set_direct(uint32_t color) {
		c = color;
	}

	uint32_t get() {
		return i.a << 24 | i.r << 16 | i.g << 8 | i.b;
	}

	union {
		uint32_t c;
#ifdef LSB_FIRST
		struct { uint8_t a, b, g, r; } i;
#else
		struct { uint8_t r, g, b, a; } i;
#endif
	};
};
#else
#define color_t rgbaint_t
#endif

enum {
	BIT_DEPTH_32 = 0,
	BIT_DEPTH_16,

	BIT_DEPTH_COUNT
};

struct n64_tile_t {
	int32_t format; // Image data format: RGBA, YUV, CI, IA, I
	int32_t size; // Size of texel element: 4b, 8b, 16b, 32b
	int32_t line; // Size of tile line in bytes
	int32_t tmem; // Starting tmem address for this tile in bytes
	int32_t palette; // Palette number for 4b CI texels
	int32_t ct, mt, cs, ms; // Clamp / mirror enable bits for S / T direction
	int32_t mask_t, shift_t, mask_s, shift_s; // Mask values / LOD shifts
	int32_t lshift_s, rshift_s, lshift_t, rshift_t;
	int32_t wrapped_mask_s, wrapped_mask_t;
	bool clamp_s, clamp_t;
	rgbaint_t mm, invmm;
	rgbaint_t wrapped_mask;
	rgbaint_t mask;
	rgbaint_t invmask;
	rgbaint_t lshift;
	rgbaint_t rshift;
	rgbaint_t sth;
	rgbaint_t stl;
	rgbaint_t clamp_st;
	uint16_t sl, tl, sh, th;      // 10.2 fixed-point, starting and ending texel row / column
	int32_t num;
};

struct span_base_t {
	int32_t m_span_dr;
	int32_t m_span_dg;
	int32_t m_span_db;
	int32_t m_span_da;
	int32_t m_span_ds;
	int32_t m_span_dt;
	int32_t m_span_dw;
	int32_t m_span_dz;
	int32_t m_span_dymax;
	int32_t m_span_dzpix;
	int32_t m_span_drdy;
	int32_t m_span_dgdy;
	int32_t m_span_dbdy;
	int32_t m_span_dady;
	int32_t m_span_dzdy;
};

struct combine_modes_t {
	int32_t sub_a_rgb0;
	int32_t sub_b_rgb0;
	int32_t mul_rgb0;
	int32_t add_rgb0;
	int32_t sub_a_a0;
	int32_t sub_b_a0;
	int32_t mul_a0;
	int32_t add_a0;

	int32_t sub_a_rgb1;
	int32_t sub_b_rgb1;
	int32_t mul_rgb1;
	int32_t add_rgb1;
	int32_t sub_a_a1;
	int32_t sub_b_a1;
	int32_t mul_a1;
	int32_t add_a1;
};

struct color_inputs_t {
	// combiner inputs
	color_t* combiner_rgbsub_a[2];
	color_t* combiner_rgbsub_b[2];
	color_t* combiner_rgbmul[2];
	color_t* combiner_rgbadd[2];

	color_t* combiner_alphasub_a[2];
	color_t* combiner_alphasub_b[2];
	color_t* combiner_alphamul[2];
	color_t* combiner_alphaadd[2];

	// blender input
	color_t* blender1a_rgb[2];
	color_t* blender1b_a[2];
	color_t* blender2a_rgb[2];
	color_t* blender2b_a[2];
};

struct other_modes_t {
	int32_t cycle_type;
	bool persp_tex_en;
	bool detail_tex_en;
	bool sharpen_tex_en;
	bool tex_lod_en;
	bool en_tlut;
	bool tlut_type;
	bool sample_type;
	bool mid_texel;
	bool bi_lerp0;
	bool bi_lerp1;
	bool convert_one;
	bool key_en;
	int32_t rgb_dither_sel;
	int32_t alpha_dither_sel;
	int32_t blend_m1a_0;
	int32_t blend_m1a_1;
	int32_t blend_m1b_0;
	int32_t blend_m1b_1;
	int32_t blend_m2a_0;
	int32_t blend_m2a_1;
	int32_t blend_m2b_0;
	int32_t blend_m2b_1;
	int32_t tex_edge;
	int32_t force_blend;
	int32_t blend_shift;
	bool alpha_cvg_select;
	bool cvg_times_alpha;
	int32_t z_mode;
	int32_t cvg_dest;
	bool color_on_cvg;
	uint8_t image_read_en;
	bool z_update_en;
	bool z_compare_en;
	bool antialias_en;
	bool z_source_sel;
	int32_t dither_alpha_en;
	int32_t alpha_compare_en;
	int32_t alpha_dither_mode;
};

struct rectangle_t {
	uint16_t m_xl;    // 10.2 fixed-point
	uint16_t m_yl;    // 10.2 fixed-point
	uint16_t m_xh;    // 10.2 fixed-point
	uint16_t m_yh;    // 10.2 fixed-point
};

struct z_decompress_entry_t {
	uint32_t shift;
	uint32_t add;
};

struct cv_mask_derivative_t {
	uint8_t cvg;
	uint8_t cvbit;
	uint8_t xoff;
	uint8_t yoff;
};

class span_param_t {
public:
	union {
		uint32_t w;
#ifdef LSB_FIRST
		struct { uint16_t l; int16_t h; } h;
#else
		struct { int16_t h; uint16_t l; } h;
#endif
	};
};

#endif // _VIDEO_N64TYPES_H_
