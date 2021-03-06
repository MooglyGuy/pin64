// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
/******************************************************************************


SGI/Nintendo Reality Display Processor
-------------------

by Ryan Holtz
based on initial C code by Ville Linde
contains additional improvements from angrylion, Ziggy, Gonetz and Orkin


*******************************************************************************

STATUS:

Much behavior needs verification against real hardware.  Many edge cases must
be verified on real hardware as well.

TODO:

- Further re-work class structure to avoid dependencies

*******************************************************************************/

#include "../emu.h"
#include "n64.h"
#include "rdpblend.h"
#include "rdptpipe.h"
#include "../Logger.h"

#include <algorithm>

#define LOG_RDP_EXECUTION       0

static FILE* rdp_exec;

uint32_t n64_rdp::s_special_9bit_clamptable[512];

bool n64_rdp::rdp_range_check(uint32_t addr) {
	if (m_misc_state.m_fb_size == 0) return false;

	uint32_t fbcount = ((m_misc_state.m_fb_width * m_scissor.m_yl) << (m_misc_state.m_fb_size - 1)) * 3;
	uint32_t fbaddr = m_misc_state.m_fb_address & 0x007fffff;
	if ((addr >= fbaddr) && (addr < (fbaddr + fbcount))) {
		return false;
	}

	uint32_t zbcount = m_misc_state.m_fb_width * m_scissor.m_yl * 2;
	uint32_t zbaddr = m_misc_state.m_zb_address & 0x007fffff;
	if ((addr >= zbaddr) && (addr < (zbaddr + zbcount))) {
		return false;
	}

	printf("Check failed: %08x vs. %08x-%08x, %08x-%08x (%d, %d)\n", addr, fbaddr, fbaddr + fbcount, zbaddr, zbaddr + zbcount, m_misc_state.m_fb_width, m_scissor.m_yl);
	fflush(stdout);
	return true;
}

/*****************************************************************************/

// The functions in this file should be moved into the parent Processor class.
//#include "rdpfiltr.hxx"

int32_t n64_rdp::get_alpha_cvg(int32_t comb_alpha) {
	int32_t temp = comb_alpha;
	int32_t temp2 = m_current_pix_cvg;
	int32_t temp3 = 0;

	if (m_other_modes.cvg_times_alpha) {
		temp3 = (temp * temp2) + 4;
		m_current_pix_cvg = (temp3 >> 8) & 0xf;
	}
	if (m_other_modes.alpha_cvg_select) {
		temp = (m_other_modes.cvg_times_alpha) ? (temp3 >> 3) : (temp2 << 5);
	}
	if (temp > 0xff) {
		temp = 0xff;
	}
	return temp;
}

/*****************************************************************************/

void n64_rdp::screen_update(uint32_t* outbuf) {
	if (m_vi_blank)
		return;

	video_update(outbuf);
}

void n64_rdp::video_update(uint32_t* outbuf) {

	//if (m_capture->vi_control() & 0x40) /* Interlace */
	//{
	//	field ^= 1;
	//} else {
	//	field = 0;
	//}

	switch (m_capture->vi_control() & 0x3) {
	case PIXEL_SIZE_16BIT:
		video_update16(outbuf);
		break;

	case PIXEL_SIZE_32BIT:
		video_update32(outbuf);
		break;

	default:
		//fatalerror("Unsupported framebuffer depth: m_fb_size=%d\n", m_misc_state.m_fb_size);
		break;
	}
}

void n64_rdp::video_update16(uint32_t* outbuf) {
	//int32_t fsaa = (((m_capture->vi_control() >> 8) & 3) < 2);
	//int32_t divot = (m_capture->vi_control() >> 4) & 1;

	//uint32_t prev_cvg = 0;
	//uint32_t next_cvg = 0;
	//int32_t dither_filter = (m_capture->vi_control() >> 16) & 1;
	//int32_t vibuffering = ((m_capture->vi_control() & 2) && fsaa && divot);

	uint16_t* frame_buffer = (uint16_t*)&m_rdram[(m_capture->vi_origin() & 0xffffff) >> 2];
	//uint32_t hb = ((n64->vi_origin & 0xffffff) >> 2) >> 1;
	//uint8_t* hidden_buffer = &m_hidden_bits[hb];

	int32_t hdiff = (m_capture->vi_hstart() & 0x3ff) - ((m_capture->vi_hstart() >> 16) & 0x3ff);
	float hcoeff = ((float)(m_capture->vi_xscale() & 0xfff) / (1 << 10));
	uint32_t hres = (uint32_t)((float)hdiff * hcoeff);
	int32_t invisiblewidth = m_capture->vi_width() - hres;

	int32_t vdiff = ((m_capture->vi_vstart() & 0x3ff) - ((m_capture->vi_vstart() >> 16) & 0x3ff)) >> 1;
	float vcoeff = ((float)(m_capture->vi_yscale() & 0xfff) / (1 << 10));
	uint32_t vres = (uint32_t)((float)vdiff * vcoeff);

	if (vdiff <= 0 || hdiff <= 0) {
		return;
	}

	//if (hres > 640) // Needed by Top Gear Overdrive (E)
	//{
	//  invisiblewidth += (hres - 640);
	//  hres = 640;
	//}

	if (vres > 480) { // makes Perfect Dark boot w/o crashing
		vres = 480;
	}

	uint32_t pixels = 0;

	if (frame_buffer) {
		for (uint32_t j = 0; j < vres && j < 480; j++) {
			uint32_t* outline = outbuf + j * 640;
			for (uint32_t i = 0; i < hres; i++) {
				uint16_t pix = frame_buffer[pixels ^ WORD_ADDR_XOR];

				const uint8_t r = ((pix >> 8) & 0xf8) | (pix >> 13);
				const uint8_t g = ((pix >> 3) & 0xf8) | ((pix >> 8) & 0x07);
				const uint8_t b = ((pix << 2) & 0xf8) | ((pix >> 3) & 0x07);
				*outline = (r << 24) | (g << 16) | (b << 8) | 0xff;
				
				outline++;
				pixels++;
			}
			pixels += invisiblewidth;
		}
	}
}

void n64_rdp::video_update32(uint32_t* outbuf) {
	int32_t gamma = (m_capture->vi_control() >> 3) & 1;
	int32_t gamma_dither = (m_capture->vi_control() >> 2) & 1;
	//int32_t vibuffering = ((m_capture->vi_control() & 2) && fsaa && divot);

	uint32_t* frame_buffer = (uint32_t*)&m_rdram[(m_capture->vi_origin() & 0xffffff) >> 2];

	const int32_t hdiff = (m_capture->vi_hstart() & 0x3ff) - ((m_capture->vi_hstart() >> 16) & 0x3ff);
	const float hcoeff = ((float)(m_capture->vi_xscale() & 0xfff) / (1 << 10));
	uint32_t hres = (uint32_t)((float)hdiff * hcoeff);
	int32_t invisiblewidth = m_capture->vi_width() - hres;

	const int32_t vdiff = ((m_capture->vi_vstart() & 0x3ff) - ((m_capture->vi_vstart() >> 16) & 0x3ff)) >> 1;
	const float vcoeff = ((float)(m_capture->vi_yscale() & 0xfff) / (1 << 10));
	const uint32_t vres = (uint32_t)((float)vdiff * vcoeff);

	if (vdiff <= 0 || hdiff <= 0) {
		return;
	}

	//if (hres > 640) // Needed by Top Gear Overdrive (E)
	//{
	//  invisiblewidth += (hres - 640);
	//  hres = 640;
	//}

	if (frame_buffer) {
		for (uint32_t j = 0; j < vres && j < 480; j++) {
			uint32_t* outline = outbuf + j * 640;
			for (uint32_t i = 0; i < hres; i++) {
				uint32_t pix = *frame_buffer++;
				if (gamma || gamma_dither) {
					int32_t r = (pix >> 24) & 0xff;
					int32_t g = (pix >> 16) & 0xff;
					int32_t b = (pix >> 8) & 0xff;
					int32_t dith = 0;
					if (gamma_dither) {
						dith = rand() & 0x3f;
					}
					if (gamma) {
						if (gamma_dither) {
							r = m_gamma_dither_table[(r << 6) | dith];
							g = m_gamma_dither_table[(g << 6) | dith];
							b = m_gamma_dither_table[(b << 6) | dith];
						} else {
							r = m_gamma_table[r];
							g = m_gamma_table[g];
							b = m_gamma_table[b];
						}
					} else if (gamma_dither) {
						if (r < 255)
							r += (dith & 1);
						if (g < 255)
							g += (dith & 1);
						if (b < 255)
							b += (dith & 1);
					}
					pix = (r << 24) | (g << 16) | (b << 8);
				}

				*outline = (pix >> 8);
				outline++;
			}
			frame_buffer += invisiblewidth;
		}
	}
}

/*****************************************************************************/

void n64_rdp::tc_div_no_perspective(int32_t ss, int32_t st, int32_t sw, int32_t* sss, int32_t* sst) {
	*sss = (SIGN16(ss)) & 0x1ffff;
	*sst = (SIGN16(st)) & 0x1ffff;
}

void n64_rdp::tc_div(int32_t ss, int32_t st, int32_t sw, int32_t* sss, int32_t* sst) {
	int32_t w_carry = 0;
	if ((sw & 0x8000) || !(sw & 0x7fff)) {
		w_carry = 1;
	}

	sw &= 0x7fff;

	int32_t shift;
	for (shift = 1; shift <= 14 && !((sw << shift) & 0x8000); shift++);
	shift -= 1;

	int32_t normout = (sw << shift) & 0x3fff;
	int32_t wnorm = (normout & 0xff) << 2;
	normout >>= 8;

	int32_t temppoint = m_norm_point_rom[normout];
	int32_t tempslope = m_norm_slope_rom[normout];

	int32_t tlu_rcp = ((-(tempslope * wnorm)) >> 10) + temppoint;

	int32_t sprod = SIGN16(ss) * tlu_rcp;
	int32_t tprod = SIGN16(st) * tlu_rcp;
	int32_t tempmask = ((1 << (shift + 1)) - 1) << (29 - shift);
	int32_t shift_value = 13 - shift;

	int32_t outofbounds_s = sprod & tempmask;
	int32_t outofbounds_t = tprod & tempmask;
	if (shift == 0xe) {
		*sss = sprod << 1;
		*sst = tprod << 1;
	} else {
		*sss = sprod = (sprod >> shift_value);
		*sst = tprod = (tprod >> shift_value);
	}
	//compute clamp flags
	int32_t under_s = 0;
	int32_t under_t = 0;
	int32_t over_s = 0;
	int32_t over_t = 0;

	if (outofbounds_s != tempmask && outofbounds_s != 0) {
		if (sprod & (1 << 29)) {
			under_s = 1;
		} else {
			over_s = 1;
		}
	}

	if (outofbounds_t != tempmask && outofbounds_t != 0) {
		if (tprod & (1 << 29)) {
			under_t = 1;
		} else {
			over_t = 1;
		}
	}

	over_s |= w_carry;
	over_t |= w_carry;

	*sss = (*sss & 0x1ffff) | (over_s << 18) | (under_s << 17);
	*sst = (*sst & 0x1ffff) | (over_t << 18) | (under_t << 17);
}

int32_t n64_rdp::color_combiner_equation(int32_t a, int32_t b, int32_t c, int32_t d) {
	a = KURT_AKELEY_SIGN9(a);
	b = KURT_AKELEY_SIGN9(b);
	c = SIGN9(c);
	d = KURT_AKELEY_SIGN9(d);
	a = (((a - b) * c) + (d << 8) + 0x80);
	a = SIGN17(a) >> 8;
	a = s_special_9bit_clamptable[a & 0x1ff];
	return a;
}

int32_t n64_rdp::alpha_combiner_equation(int32_t a, int32_t b, int32_t c, int32_t d) {
	a = KURT_AKELEY_SIGN9(a);
	b = KURT_AKELEY_SIGN9(b);
	c = SIGN9(c);
	d = KURT_AKELEY_SIGN9(d);
	a = (((a - b) * c) + (d << 8) + 0x80) >> 8;
	a = SIGN9(a);
	a = s_special_9bit_clamptable[a & 0x1ff];
	return a;
}

void n64_rdp::set_suba_input_rgb(color_t** input, int32_t code) {
	switch (code & 0xf) {
	case 0:     *input = &m_combined_color; break;
	case 1:     *input = &m_texel0_color; break;
	case 2:     *input = &m_texel1_color; break;
	case 3:     *input = &m_prim_color; break;
	case 4:     *input = &m_shade_color; break;
	case 5:     *input = &m_env_color; break;
	case 6:     *input = &m_one; break;
	case 7:     *input = &m_noise_color; break;
	case 8: case 9: case 10: case 11: case 12: case 13: case 14: case 15:
	{
		*input = &m_zero; break;
	}
	}
}

void n64_rdp::set_subb_input_rgb(color_t** input, int32_t code) {
	switch (code & 0xf) {
	case 0:     *input = &m_combined_color; break;
	case 1:     *input = &m_texel0_color; break;
	case 2:     *input = &m_texel1_color; break;
	case 3:     *input = &m_prim_color; break;
	case 4:     *input = &m_shade_color; break;
	case 5:     *input = &m_env_color; break;
	case 6:     printf("SET_SUBB_RGB_INPUT: key_center\n");
	case 7:     *input = &m_k4; break;
	case 8: case 9: case 10: case 11: case 12: case 13: case 14: case 15:
	{
		*input = &m_zero; break;
	}
	}
}

void n64_rdp::set_mul_input_rgb(color_t** input, int32_t code) {
	switch (code & 0x1f) {
	case 0:     *input = &m_combined_color; break;
	case 1:     *input = &m_texel0_color; break;
	case 2:     *input = &m_texel1_color; break;
	case 3:     *input = &m_prim_color; break;
	case 4:     *input = &m_shade_color; break;
	case 5:     *input = &m_env_color; break;
	case 6:     *input = &m_key_scale; break;
	case 7:     *input = &m_combined_alpha; break;
	case 8:     *input = &m_texel0_alpha; break;
	case 9:     *input = &m_texel1_alpha; break;
	case 10:    *input = &m_prim_alpha; break;
	case 11:    *input = &m_shade_alpha; break;
	case 12:    *input = &m_env_alpha; break;
	case 13:    *input = &m_lod_fraction; break;
	case 14:    *input = &m_prim_lod_fraction; break;
	case 15:    *input = &m_k5; break;
	case 16: case 17: case 18: case 19: case 20: case 21: case 22: case 23:
	case 24: case 25: case 26: case 27: case 28: case 29: case 30: case 31:
	{
		*input = &m_zero; break;
	}
	}
}

void n64_rdp::set_add_input_rgb(color_t** input, int32_t code) {
	switch (code & 0x7) {
	case 0:     *input = &m_combined_color; break;
	case 1:     *input = &m_texel0_color; break;
	case 2:     *input = &m_texel1_color; break;
	case 3:     *input = &m_prim_color; break;
	case 4:     *input = &m_shade_color; break;
	case 5:     *input = &m_env_color; break;
	case 6:     *input = &m_one; break;
	case 7:     *input = &m_zero; break;
	}
}

void n64_rdp::set_sub_input_alpha(color_t** input, int32_t code) {
	switch (code & 0x7) {
	case 0:     *input = &m_combined_alpha; break;
	case 1:     *input = &m_texel0_alpha; break;
	case 2:     *input = &m_texel1_alpha; break;
	case 3:     *input = &m_prim_alpha; break;
	case 4:     *input = &m_shade_alpha; break;
	case 5:     *input = &m_env_alpha; break;
	case 6:     *input = &m_one; break;
	case 7:     *input = &m_zero; break;
	}
}

void n64_rdp::set_mul_input_alpha(color_t** input, int32_t code) {
	switch (code & 0x7) {
	case 0:     *input = &m_lod_fraction; break;
	case 1:     *input = &m_texel0_alpha; break;
	case 2:     *input = &m_texel1_alpha; break;
	case 3:     *input = &m_prim_alpha; break;
	case 4:     *input = &m_shade_alpha; break;
	case 5:     *input = &m_env_alpha; break;
	case 6:     *input = &m_prim_lod_fraction; break;
	case 7:     *input = &m_zero; break;
	}
}

void n64_rdp::set_blender_input(int32_t cycle, int32_t which, color_t** input_rgb, color_t** input_a, int32_t a, int32_t b) {
	switch (a & 0x3) {
	case 0:
		*input_rgb = cycle == 0 ? &m_pixel_color : &m_blended_pixel_color;
		break;

	case 1:
		*input_rgb = &m_memory_color;
		break;

	case 2:
		*input_rgb = &m_blend_color;
		break;

	case 3:
		*input_rgb = &m_fog_color;
		break;
	}

	if (which == 0) {
		switch (b & 0x3) {
		case 0:
			*input_a = &m_pixel_color;
			break;
		case 1:
			*input_a = &m_fog_color;
			break;
		case 2:
			*input_a = &m_shade_color;
			break;
		case 3:
			*input_a = &m_zero;
			break;
		}
	} else {
		switch (b & 0x3) {
		case 0:     *input_a = &m_inv_pixel_color; break;
		case 1:     *input_a = &m_memory_color; break;
		case 2:     *input_a = &m_one; break;
		case 3:     *input_a = &m_zero; break;
		}
	}
}

const uint8_t n64_rdp::s_bayer_matrix[16] =
{ /* Bayer matrix */
	0,  4,  1, 5,
	6,  2,  7, 3,
	1,   5,  0, 4,
	7,  3,  6, 2
};

const uint8_t n64_rdp::s_magic_matrix[16] =
{ /* Magic square matrix */
	0,  6,  1, 7,
	4,  2,  5, 3,
	3,   5,  2, 4,
	7,  1,  6, 0
};

const z_decompress_entry_t n64_rdp::m_z_dec_table[8] =
{
	{ 6, 0x00000 },
	{ 5, 0x20000 },
	{ 4, 0x30000 },
	{ 3, 0x38000 },
	{ 2, 0x3c000 },
	{ 1, 0x3e000 },
	{ 0, 0x3f000 },
	{ 0, 0x3f800 },
};

/*****************************************************************************/

void n64_rdp::z_build_com_table(void) {
	uint16_t altmem = 0;
	for (int32_t z = 0; z < 0x40000; z++) {
		switch ((z >> 11) & 0x7f) {
		case 0x00:
		case 0x01:
		case 0x02:
		case 0x03:
		case 0x04:
		case 0x05:
		case 0x06:
		case 0x07:
		case 0x08:
		case 0x09:
		case 0x0a:
		case 0x0b:
		case 0x0c:
		case 0x0d:
		case 0x0e:
		case 0x0f:
		case 0x10:
		case 0x11:
		case 0x12:
		case 0x13:
		case 0x14:
		case 0x15:
		case 0x16:
		case 0x17:
		case 0x18:
		case 0x19:
		case 0x1a:
		case 0x1b:
		case 0x1c:
		case 0x1d:
		case 0x1e:
		case 0x1f:
		case 0x20:
		case 0x21:
		case 0x22:
		case 0x23:
		case 0x24:
		case 0x25:
		case 0x26:
		case 0x27:
		case 0x28:
		case 0x29:
		case 0x2a:
		case 0x2b:
		case 0x2c:
		case 0x2d:
		case 0x2e:
		case 0x2f:
		case 0x30:
		case 0x31:
		case 0x32:
		case 0x33:
		case 0x34:
		case 0x35:
		case 0x36:
		case 0x37:
		case 0x38:
		case 0x39:
		case 0x3a:
		case 0x3b:
		case 0x3c:
		case 0x3d:
		case 0x3e:
		case 0x3f:
			altmem = (z >> 4) & 0x1ffc;
			break;
		case 0x40:
		case 0x41:
		case 0x42:
		case 0x43:
		case 0x44:
		case 0x45:
		case 0x46:
		case 0x47:
		case 0x48:
		case 0x49:
		case 0x4a:
		case 0x4b:
		case 0x4c:
		case 0x4d:
		case 0x4e:
		case 0x4f:
		case 0x50:
		case 0x51:
		case 0x52:
		case 0x53:
		case 0x54:
		case 0x55:
		case 0x56:
		case 0x57:
		case 0x58:
		case 0x59:
		case 0x5a:
		case 0x5b:
		case 0x5c:
		case 0x5d:
		case 0x5e:
		case 0x5f:
			altmem = ((z >> 3) & 0x1ffc) | 0x2000;
			break;
		case 0x60:
		case 0x61:
		case 0x62:
		case 0x63:
		case 0x64:
		case 0x65:
		case 0x66:
		case 0x67:
		case 0x68:
		case 0x69:
		case 0x6a:
		case 0x6b:
		case 0x6c:
		case 0x6d:
		case 0x6e:
		case 0x6f:
			altmem = ((z >> 2) & 0x1ffc) | 0x4000;
			break;
		case 0x70:
		case 0x71:
		case 0x72:
		case 0x73:
		case 0x74:
		case 0x75:
		case 0x76:
		case 0x77:
			altmem = ((z >> 1) & 0x1ffc) | 0x6000;
			break;
		case 0x78://uncompressed z = 0x3c000
		case 0x79:
		case 0x7a:
		case 0x7b:
			altmem = (z & 0x1ffc) | 0x8000;
			break;
		case 0x7c://uncompressed z = 0x3e000
		case 0x7d:
			altmem = ((z << 1) & 0x1ffc) | 0xa000;
			break;
		case 0x7e://uncompressed z = 0x3f000
			altmem = ((z << 2) & 0x1ffc) | 0xc000;
			break;
		case 0x7f://uncompressed z = 0x3f000
			altmem = ((z << 2) & 0x1ffc) | 0xe000;
			break;
		}

		m_z_com_table[z] = altmem;

	}
}

void n64_rdp::precalc_cvmask_derivatives(void) {
	const uint8_t yarray[16] = { 0, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0 };
	const uint8_t xarray[16] = { 0, 3, 2, 2, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0 };

	for (int32_t i = 0; i < 0x10000; i++) {
		m_compressed_cvmasks[i] = (i & 1) | ((i & 4) >> 1) | ((i & 0x20) >> 3) | ((i & 0x80) >> 4) |
			((i & 0x100) >> 4) | ((i & 0x400) >> 5) | ((i & 0x2000) >> 7) | ((i & 0x8000) >> 8);
	}

	for (int32_t i = 0; i < 0x100; i++) {
		uint16_t mask = decompress_cvmask_frombyte(i);
		cvarray[i].cvg = cvarray[i].cvbit = 0;
		cvarray[i].cvbit = (i >> 7) & 1;
		for (int32_t k = 0; k < 8; k++) {
			cvarray[i].cvg += ((i >> k) & 1);
		}

		uint16_t masky = 0;
		for (int32_t k = 0; k < 4; k++) {
			masky |= ((mask & (0xf000 >> (k << 2))) > 0) << k;
		}
		uint8_t offy = yarray[masky];

		uint16_t maskx = (mask & (0xf000 >> (offy << 2))) >> ((offy ^ 3) << 2);
		uint8_t offx = xarray[maskx];

		cvarray[i].xoff = offx;
		cvarray[i].yoff = offy;
	}
}

uint16_t n64_rdp::decompress_cvmask_frombyte(uint8_t x) {
	uint16_t y = (x & 1) | ((x & 2) << 1) | ((x & 4) << 3) | ((x & 8) << 4) |
		((x & 0x10) << 4) | ((x & 0x20) << 5) | ((x & 0x40) << 7) | ((x & 0x80) << 8);
	return y;
}

void n64_rdp::lookup_cvmask_derivatives(uint32_t mask, uint8_t* offx, uint8_t* offy) {
	const uint32_t index = m_compressed_cvmasks[mask];
	m_current_pix_cvg = cvarray[index].cvg;
	m_current_cvg_bit = cvarray[index].cvbit;
	*offx = cvarray[index].xoff;
	*offy = cvarray[index].yoff;
}

void n64_rdp::z_store(uint32_t zcurpixel, uint32_t dzcurpixel, uint32_t z, uint32_t enc) {
	uint16_t zval = m_z_com_table[z & 0x3ffff] | (enc >> 2);
	if (zcurpixel <= MEM16_LIMIT) {
		((uint16_t*)m_rdram)[zcurpixel ^ WORD_ADDR_XOR] = zval;
	}
	if (dzcurpixel <= MEM8_LIMIT) {
		m_hidden_bits[dzcurpixel ^ BYTE_ADDR_XOR] = enc & 3;
	}
}

int32_t n64_rdp::normalize_dzpix(int32_t sum) {
	if (sum & 0xc000) {
		return 0x8000;
	}
	if (!(sum & 0xffff)) {
		return 1;
	}
	for (int32_t count = 0x2000; count > 0; count >>= 1) {
		if (sum & count) {
			return(count << 1);
		}
	}
	return 0;
}

uint32_t n64_rdp::z_decompress(uint32_t zcurpixel) {
	return m_z_complete_dec_table[(RREADIDX16(zcurpixel) >> 2) & 0x3fff];
}

uint32_t n64_rdp::dz_decompress(uint32_t zcurpixel, uint32_t dzcurpixel) {
	const uint16_t zval = RREADIDX16(zcurpixel);
	const uint8_t dzval = (((dzcurpixel) <= 0x7fffff) ? (m_hidden_bits[(dzcurpixel) ^ BYTE_ADDR_XOR]) : 0);
	const uint32_t dz_compressed = ((zval & 3) << 2) | (dzval & 3);
	return (1 << dz_compressed);
}

uint32_t n64_rdp::dz_compress(uint32_t value) {
	int32_t j = 0;
	for (; value > 1; j++, value >>= 1);
	return j;
}

void n64_rdp::get_dither_values(int32_t x, int32_t y, int32_t* cdith, int32_t* adith) {
	const int32_t dithindex = ((y & 3) << 2) | (x & 3);
	switch ((m_other_modes.rgb_dither_sel << 2) | m_other_modes.alpha_dither_sel) {
	case 0:
		*adith = *cdith = s_magic_matrix[dithindex];
		break;
	case 1:
		*cdith = s_magic_matrix[dithindex];
		*adith = (~(*cdith)) & 7;
		break;
	case 2:
		*cdith = s_magic_matrix[dithindex];
		*adith = rand() & 7;
		break;
	case 3:
		*cdith = s_magic_matrix[dithindex];
		*adith = 0;
		break;
	case 4:
		*adith = *cdith = s_bayer_matrix[dithindex];
		break;
	case 5:
		*cdith = s_bayer_matrix[dithindex];
		*adith = (~(*cdith)) & 7;
		break;
	case 6:
		*cdith = s_bayer_matrix[dithindex];
		*adith = rand() & 7;
		break;
	case 7:
		*cdith = s_bayer_matrix[dithindex];
		*adith = 0;
		break;
	case 8:
		*cdith = rand() & 7;
		*adith = s_magic_matrix[dithindex];
		break;
	case 9:
		*cdith = rand() & 7;
		*adith = (~s_magic_matrix[dithindex]) & 7;
		break;
	case 10:
		*cdith = rand() & 7;
		*adith = (*cdith + 17) & 7;
		break;
	case 11:
		*cdith = rand() & 7;
		*adith = 0;
		break;
	case 12:
		*cdith = 0;
		*adith = s_bayer_matrix[dithindex];
		break;
	case 13:
		*cdith = 0;
		*adith = (~s_bayer_matrix[dithindex]) & 7;
		break;
	case 14:
		*cdith = 0;
		*adith = rand() & 7;
		break;
	case 15:
		*adith = *cdith = 0;
		break;
	}
}

int32_t CLAMP(int32_t in, int32_t min, int32_t max) {
	if (in < min) return min;
	if (in > max) return max;
	return in;
}

bool n64_rdp::z_compare(uint32_t zcurpixel, uint32_t dzcurpixel, uint32_t sz, uint16_t dzpix) {
	bool force_coplanar = false;
	sz &= 0x3ffff;

	uint32_t oz;
	uint32_t dzmem;
	uint32_t zval;
	int32_t rawdzmem;

	if (m_other_modes.z_compare_en) {
		oz = z_decompress(zcurpixel);
		dzmem = dz_decompress(zcurpixel, dzcurpixel);
		zval = RREADIDX16(zcurpixel);
		rawdzmem = ((zval & 3) << 2) | ((((dzcurpixel) <= 0x3fffff) ? (m_hidden_bits[(dzcurpixel) ^ BYTE_ADDR_XOR]) : 0) & 3);
	} else {
		oz = 0;
		dzmem = 1 << 0xf;
		zval = 0x3;
		rawdzmem = 0xf;
	}

	m_dzpix_enc = dz_compress(dzpix & 0xffff);
	m_shift_a = CLAMP(m_dzpix_enc - rawdzmem, 0, 4);
	m_shift_b = CLAMP(rawdzmem - m_dzpix_enc, 0, 4);

	uint32_t precision_factor = (zval >> 13) & 0xf;
	if (precision_factor < 3) {
		uint32_t dzmemmodifier = 16 >> precision_factor;
		if (dzmem == 0x8000) {
			force_coplanar = true;
		}
		dzmem <<= 1;
		if (dzmem <= dzmemmodifier) {
			dzmem = dzmemmodifier;
		}
		if (!dzmem) {
			dzmem = 0xffff;
		}
	}
	if (dzmem > 0x8000) {
		dzmem = 0xffff;
	}

	uint32_t dznew = (dzmem > dzpix) ? dzmem : (uint32_t)dzpix;
	uint32_t dznotshift = dznew;
	dznew <<= 3;

	bool farther = (sz + dznew) >= oz;
	bool infront = sz < oz;

	if (force_coplanar) {
		farther = true;
	}

	bool overflow = ((m_current_mem_cvg + m_current_pix_cvg) & 8) > 0;
	m_blend_enable = (m_other_modes.force_blend || (!overflow && m_other_modes.antialias_en && farther)) ? 1 : 0;
	m_pre_wrap = overflow;

	int32_t cvgcoeff = 0;
	uint32_t dzenc = 0;

	if (m_other_modes.z_mode == 1 && infront && farther && overflow) {
		dzenc = dz_compress(dznotshift & 0xffff);
		cvgcoeff = ((oz >> dzenc) - (sz >> dzenc)) & 0xf;
		m_current_pix_cvg = ((cvgcoeff * m_current_pix_cvg) >> 3) & 0xf;
	}

	if (!m_other_modes.z_compare_en) {
		return true;
	}

	int32_t diff = (int32_t)sz - (int32_t)dznew;
	bool nearer = diff <= (int32_t)oz;
	bool max = (oz == 0x3ffff);
	if (force_coplanar) {
		nearer = true;
	}

	switch (m_other_modes.z_mode) {
	case 0:
		return (max || (overflow ? infront : nearer));
	case 1:
		return (max || (overflow ? infront : nearer));
	case 2:
		return (infront || max);
	case 3:
		return (farther && nearer && !max);
	}

	return false;
}

uint32_t n64_rdp::get_log2(uint32_t lod_clamp) {
	if (lod_clamp < 2) {
		return 0;
	} else {
		for (int32_t i = 7; i > 0; i--) {
			if ((lod_clamp >> i) & 1) {
				return i;
			}
		}
	}

	return 0;
}

/*****************************************************************************/

uint64_t n64_rdp::read_data(uint32_t address) {
	return 0;
}

const char* n64_rdp::s_image_format[] = { "RGBA", "YUV", "CI", "IA", "I", "???", "???", "???" };
const char* n64_rdp::s_image_size[] = { "4-bit", "8-bit", "16-bit", "32-bit" };

const size_t n64_rdp::s_element_buf_size = 32;

const uint32_t n64_rdp::s_rdp_command_length[64] =
{
	8,          // 0x00, No Op
	8,          // 0x01, ???
	8,          // 0x02, ???
	8,          // 0x03, ???
	8,          // 0x04, ???
	8,          // 0x05, ???
	8,          // 0x06, ???
	8,          // 0x07, ???
	32,         // 0x08, Non-Shaded Triangle
	32 + 16,      // 0x09, Non-Shaded, Z-Buffered Triangle
	32 + 64,      // 0x0a, Textured Triangle
	32 + 64 + 16,   // 0x0b, Textured, Z-Buffered Triangle
	32 + 64,      // 0x0c, Shaded Triangle
	32 + 64 + 16,   // 0x0d, Shaded, Z-Buffered Triangle
	32 + 64 + 64,   // 0x0e, Shaded+Textured Triangle
	32 + 64 + 64 + 16,// 0x0f, Shaded+Textured, Z-Buffered Triangle
	8,          // 0x10, ???
	8,          // 0x11, ???
	8,          // 0x12, ???
	8,          // 0x13, ???
	8,          // 0x14, ???
	8,          // 0x15, ???
	8,          // 0x16, ???
	8,          // 0x17, ???
	8,          // 0x18, ???
	8,          // 0x19, ???
	8,          // 0x1a, ???
	8,          // 0x1b, ???
	8,          // 0x1c, ???
	8,          // 0x1d, ???
	8,          // 0x1e, ???
	8,          // 0x1f, ???
	8,          // 0x20, ???
	8,          // 0x21, ???
	8,          // 0x22, ???
	8,          // 0x23, ???
	16,         // 0x24, Texture_Rectangle
	16,         // 0x25, Texture_Rectangle_Flip
	8,          // 0x26, Sync_Load
	8,          // 0x27, Sync_Pipe
	8,          // 0x28, Sync_Tile
	8,          // 0x29, Sync_Full
	8,          // 0x2a, Set_Key_GB
	8,          // 0x2b, Set_Key_R
	8,          // 0x2c, Set_Convert
	8,          // 0x2d, Set_Scissor
	8,          // 0x2e, Set_Prim_Depth
	8,          // 0x2f, Set_Other_Modes
	8,          // 0x30, Load_TLUT
	8,          // 0x31, ???
	8,          // 0x32, Set_Tile_Size
	8,          // 0x33, Load_Block
	8,          // 0x34, Load_Tile
	8,          // 0x35, Set_Tile
	8,          // 0x36, Fill_Rectangle
	8,          // 0x37, Set_Fill_Color
	8,          // 0x38, Set_Fog_Color
	8,          // 0x39, Set_Blend_Color
	8,          // 0x3a, Set_Prim_Color
	8,          // 0x3b, Set_Env_Color
	8,          // 0x3c, Set_Combine
	8,          // 0x3d, Set_Texture_Image
	8,          // 0x3e, Set_Mask_Image
	8           // 0x3f, Set_Color_Image
};

void n64_rdp::disassemble(char* buffer, size_t buf_size) {
	char sl[32], tl[32], sh[32], th[32];
	char s[32], t[32], w[32];
	char dsdx[32], dtdx[32], dwdx[32];
	char dsdy[32], dtdy[32], dwdy[32];
	char dsde[32], dtde[32], dwde[32];
	char yl[32], yh[32], ym[32], xl[32], xh[32], xm[32];
	char dxldy[32], dxhdy[32], dxmdy[32];
	char rt[32], gt[32], bt[32], at[32];
	char drdx[32], dgdx[32], dbdx[32], dadx[32];
	char drdy[32], dgdy[32], dbdy[32], dady[32];
	char drde[32], dgde[32], dbde[32], dade[32];

	uint64_t cmd[32];

	const uint32_t length = m_cmd_ptr * 8;
	if (length < 8) {
		snprintf(buffer, buf_size, "ERROR: length = %d\n", length);
		return;
	}

	cmd[0] = m_cmd_data[m_cmd_cur];

	const int32_t tile = (cmd[0] >> 56) & 0x7;
	snprintf(sl, s_element_buf_size, "%4.2f", (float)((cmd[0] >> 44) & 0xfff) / 4.0f);
	snprintf(tl, s_element_buf_size, "%4.2f", (float)((cmd[0] >> 32) & 0xfff) / 4.0f);
	snprintf(sh, s_element_buf_size, "%4.2f", (float)((cmd[0] >> 12) & 0xfff) / 4.0f);
	snprintf(th, s_element_buf_size, "%4.2f", (float)((cmd[0] >> 0) & 0xfff) / 4.0f);

	const char* format = s_image_format[(cmd[0] >> 53) & 0x7];
	const char* size = s_image_size[(cmd[0] >> 51) & 0x3];

	const uint32_t r = (cmd[0] >> 24) & 0xff;
	const uint32_t g = (cmd[0] >> 16) & 0xff;
	const uint32_t b = (cmd[0] >> 8) & 0xff;
	const uint32_t a = (cmd[0] >> 0) & 0xff;

	const uint32_t command = (cmd[0] >> 56) & 0x3f;
	switch (command) {
	case 0x00:  snprintf(buffer, buf_size, "No Op"); break;
	case 0x08:      // Tri_NoShade
	{
		const int32_t lft = (cmd[0] >> 55) & 0x1;

		if (length != s_rdp_command_length[command]) {
			snprintf(buffer, buf_size, "ERROR: Tri_NoShade length = %d\n", length);
			return;
		}

		cmd[1] = m_cmd_data[m_cmd_cur + 1];
		cmd[2] = m_cmd_data[m_cmd_cur + 2];
		cmd[3] = m_cmd_data[m_cmd_cur + 3];

		snprintf(yl, s_element_buf_size, "%4.4f", (float)((cmd[0] >> 32) & 0x1fff) / 4.0f);
		snprintf(ym, s_element_buf_size, "%4.4f", (float)((cmd[0] >> 16) & 0x1fff) / 4.0f);
		snprintf(yh, s_element_buf_size, "%4.4f", (float)((cmd[0] >> 0) & 0x1fff) / 4.0f);
		snprintf(xl, s_element_buf_size, "%4.4f", (float)int32_t(cmd[1] >> 32) / 65536.0f);
		snprintf(dxldy, s_element_buf_size, "%4.4f", (float)int32_t(cmd[1]) / 65536.0f);
		snprintf(xh, s_element_buf_size, "%4.4f", (float)int32_t(cmd[2] >> 32) / 65536.0f);
		snprintf(dxhdy, s_element_buf_size, "%4.4f", (float)int32_t(cmd[2]) / 65536.0f);
		snprintf(xm, s_element_buf_size, "%4.4f", (float)int32_t(cmd[3] >> 32) / 65536.0f);
		snprintf(dxmdy, s_element_buf_size, "%4.4f", (float)int32_t(cmd[3]) / 65536.0f);

		snprintf(buffer, buf_size, "Tri_NoShade            %d, XL: %s, XM: %s, XH: %s, YL: %s, YM: %s, YH: %s\n", lft, xl, xm, xh, yl, ym, yh);
		break;
	}
	case 0x09:      // Tri_NoShadeZ
	{
		const int32_t lft = (cmd[0] >> 55) & 0x1;

		if (length != s_rdp_command_length[command]) {
			snprintf(buffer, buf_size, "ERROR: Tri_NoShadeZ length = %d\n", length);
			return;
		}

		cmd[1] = m_cmd_data[m_cmd_cur + 1];
		cmd[2] = m_cmd_data[m_cmd_cur + 2];
		cmd[3] = m_cmd_data[m_cmd_cur + 3];

		snprintf(yl, s_element_buf_size, "%4.4f", (float)((cmd[0] >> 32) & 0x1fff) / 4.0f);
		snprintf(ym, s_element_buf_size, "%4.4f", (float)((cmd[0] >> 16) & 0x1fff) / 4.0f);
		snprintf(yh, s_element_buf_size, "%4.4f", (float)((cmd[0] >> 0) & 0x1fff) / 4.0f);
		snprintf(xl, s_element_buf_size, "%4.4f", (float)int32_t(cmd[1] >> 32) / 65536.0f);
		snprintf(dxldy, s_element_buf_size, "%4.4f", (float)int32_t(cmd[1]) / 65536.0f);
		snprintf(xh, s_element_buf_size, "%4.4f", (float)int32_t(cmd[2] >> 32) / 65536.0f);
		snprintf(dxhdy, s_element_buf_size, "%4.4f", (float)int32_t(cmd[2]) / 65536.0f);
		snprintf(xm, s_element_buf_size, "%4.4f", (float)int32_t(cmd[3] >> 32) / 65536.0f);
		snprintf(dxmdy, s_element_buf_size, "%4.4f", (float)int32_t(cmd[3]) / 65536.0f);

		snprintf(buffer, buf_size, "Tri_NoShadeZ            %d, XL: %s, XM: %s, XH: %s, YL: %s, YM: %s, YH: %s\n", lft, xl, xm, xh, yl, ym, yh);
		break;
	}
	case 0x0a:      // Tri_Tex
	{
		const int32_t lft = (cmd[0] >> 55) & 0x1;

		if (length < s_rdp_command_length[command]) {
			snprintf(buffer, buf_size, "ERROR: Tri_Tex length = %d\n", length);
			return;
		}

		for (int32_t i = 1; i < 12; i++) {
			cmd[i] = m_cmd_data[m_cmd_cur + i];
		}

		snprintf(yl, s_element_buf_size, "%4.4f", (float)((cmd[0] >> 32) & 0x1fff) / 4.0f);
		snprintf(ym, s_element_buf_size, "%4.4f", (float)((cmd[0] >> 16) & 0x1fff) / 4.0f);
		snprintf(yh, s_element_buf_size, "%4.4f", (float)((cmd[0] >> 0) & 0x1fff) / 4.0f);
		snprintf(xl, s_element_buf_size, "%4.4f", (float)int32_t(cmd[1] >> 32) / 65536.0f);
		snprintf(dxldy, s_element_buf_size, "%4.4f", (float)int32_t(cmd[1]) / 65536.0f);
		snprintf(xh, s_element_buf_size, "%4.4f", (float)int32_t(cmd[2] >> 32) / 65536.0f);
		snprintf(dxhdy, s_element_buf_size, "%4.4f", (float)int32_t(cmd[2]) / 65536.0f);
		snprintf(xm, s_element_buf_size, "%4.4f", (float)int32_t(cmd[3] >> 32) / 65536.0f);
		snprintf(dxmdy, s_element_buf_size, "%4.4f", (float)int32_t(cmd[3]) / 65536.0f);

		snprintf(s, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[4] >> 32) & 0xffff0000) | ((cmd[6] >> 48) & 0xffff)) / 65536.0f);
		snprintf(t, s_element_buf_size, "%4.4f", (float)int32_t((((cmd[4] >> 32) & 0x0000ffff) << 16) | ((cmd[6] >> 32) & 0xffff)) / 65536.0f);
		snprintf(w, s_element_buf_size, "%4.4f", (float)int32_t((cmd[4] & 0xffff0000) | ((cmd[6] >> 16) & 0xffff)) / 65536.0f);
		snprintf(dsdx, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[5] >> 32) & 0xffff0000) | ((cmd[7] >> 48) & 0xffff)) / 65536.0f);
		snprintf(dtdx, s_element_buf_size, "%4.4f", (float)int32_t((((cmd[5] >> 32) & 0x0000ffff) << 16) | ((cmd[7] >> 32) & 0xffff)) / 65536.0f);
		snprintf(dwdx, s_element_buf_size, "%4.4f", (float)int32_t((cmd[5] & 0xffff0000) | ((cmd[7] >> 16) & 0xffff)) / 65536.0f);
		snprintf(dsde, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[8] >> 32) & 0xffff0000) | ((cmd[10] >> 48) & 0xffff)) / 65536.0f);
		snprintf(dtde, s_element_buf_size, "%4.4f", (float)int32_t((((cmd[8] >> 32) & 0x0000ffff) << 16) | ((cmd[10] >> 32) & 0xffff)) / 65536.0f);
		snprintf(dwde, s_element_buf_size, "%4.4f", (float)int32_t((cmd[8] & 0xffff0000) | ((cmd[10] >> 16) & 0xffff)) / 65536.0f);
		snprintf(dsdy, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[9] >> 32) & 0xffff0000) | ((cmd[11] >> 48) & 0xffff)) / 65536.0f);
		snprintf(dtdy, s_element_buf_size, "%4.4f", (float)int32_t((((cmd[9] >> 32) & 0x0000ffff) << 16) | ((cmd[11] >> 32) & 0xffff)) / 65536.0f);
		snprintf(dwdy, s_element_buf_size, "%4.4f", (float)int32_t((cmd[9] & 0xffff0000) | ((cmd[11] >> 16) & 0xffff)) / 65536.0f);

		snprintf(buffer, buf_size, "Tri_Tex               %d, XL: %s, XM: %s, XH: %s, YL: %s, YM: %s, YH: %s\n"
		                           "                              "
		                           "                       S: %s, T: %s, W: %s\n"
		                           "                              "
		                           "                       DSDX: %s, DTDX: %s, DWDX: %s\n"
		                           "                              "
		                           "                       DSDE: %s, DTDE: %s, DWDE: %s\n"
		                           "                              "
		                           "                       DSDY: %s, DTDY: %s, DWDY: %s\n",
			lft, xl, xm, xh, yl, ym, yh,
			s, t, w,
			dsdx, dtdx, dwdx,
			dsde, dtde, dwde,
			dsdy, dtdy, dwdy);
		break;
	}
	case 0x0b:      // Tri_TexZ
	{
		const int32_t lft = (cmd[0] >> 55) & 0x1;

		if (length < s_rdp_command_length[command]) {
			snprintf(buffer, buf_size, "ERROR: Tri_TexZ length = %d\n", length);
			return;
		}

		for (int32_t i = 1; i < 12; i++) {
			cmd[i] = m_cmd_data[m_cmd_cur + i];
		}

		snprintf(yl, s_element_buf_size, "%4.4f", (float)((cmd[0] >> 32) & 0x1fff) / 4.0f);
		snprintf(ym, s_element_buf_size, "%4.4f", (float)((cmd[0] >> 16) & 0x1fff) / 4.0f);
		snprintf(yh, s_element_buf_size, "%4.4f", (float)((cmd[0] >> 0) & 0x1fff) / 4.0f);
		snprintf(xl, s_element_buf_size, "%4.4f", (float)int32_t(cmd[1] >> 32) / 65536.0f);
		snprintf(dxldy, s_element_buf_size, "%4.4f", (float)int32_t(cmd[1]) / 65536.0f);
		snprintf(xh, s_element_buf_size, "%4.4f", (float)int32_t(cmd[2] >> 32) / 65536.0f);
		snprintf(dxhdy, s_element_buf_size, "%4.4f", (float)int32_t(cmd[2]) / 65536.0f);
		snprintf(xm, s_element_buf_size, "%4.4f", (float)int32_t(cmd[3] >> 32) / 65536.0f);
		snprintf(dxmdy, s_element_buf_size, "%4.4f", (float)int32_t(cmd[3]) / 65536.0f);

		snprintf(s, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[4] >> 32) & 0xffff0000) | ((cmd[6] >> 48) & 0xffff)) / 65536.0f);
		snprintf(t, s_element_buf_size, "%4.4f", (float)int32_t((((cmd[4] >> 32) & 0x0000ffff) << 16) | ((cmd[6] >> 32) & 0xffff)) / 65536.0f);
		snprintf(w, s_element_buf_size, "%4.4f", (float)int32_t((cmd[4] & 0xffff0000) | ((cmd[6] >> 16) & 0xffff)) / 65536.0f);
		snprintf(dsdx, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[5] >> 32) & 0xffff0000) | ((cmd[7] >> 48) & 0xffff)) / 65536.0f);
		snprintf(dtdx, s_element_buf_size, "%4.4f", (float)int32_t((((cmd[5] >> 32) & 0x0000ffff) << 16) | ((cmd[7] >> 32) & 0xffff)) / 65536.0f);
		snprintf(dwdx, s_element_buf_size, "%4.4f", (float)int32_t((cmd[5] & 0xffff0000) | ((cmd[7] >> 16) & 0xffff)) / 65536.0f);
		snprintf(dsde, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[8] >> 32) & 0xffff0000) | ((cmd[10] >> 48) & 0xffff)) / 65536.0f);
		snprintf(dtde, s_element_buf_size, "%4.4f", (float)int32_t((((cmd[8] >> 32) & 0x0000ffff) << 16) | ((cmd[10] >> 32) & 0xffff)) / 65536.0f);
		snprintf(dwde, s_element_buf_size, "%4.4f", (float)int32_t((cmd[8] & 0xffff0000) | ((cmd[10] >> 16) & 0xffff)) / 65536.0f);
		snprintf(dsdy, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[9] >> 32) & 0xffff0000) | ((cmd[11] >> 48) & 0xffff)) / 65536.0f);
		snprintf(dtdy, s_element_buf_size, "%4.4f", (float)int32_t((((cmd[9] >> 32) & 0x0000ffff) << 16) | ((cmd[11] >> 32) & 0xffff)) / 65536.0f);
		snprintf(dwdy, s_element_buf_size, "%4.4f", (float)int32_t((cmd[9] & 0xffff0000) | ((cmd[11] >> 16) & 0xffff)) / 65536.0f);
		
		snprintf(buffer, buf_size, "Tri_TexZ               %d, XL: %s, XM: %s, XH: %s, YL: %s, YM: %s, YH: %s\n"
			"                              "
			"                       S: %s, T: %s, W: %s\n"
			"                              "
			"                       DSDX: %s, DTDX: %s, DWDX: %s\n"
			"                              "
			"                       DSDE: %s, DTDE: %s, DWDE: %s\n"
			"                              "
			"                       DSDY: %s, DTDY: %s, DWDY: %s\n",
			lft, xl, xm, xh, yl, ym, yh,
			s, t, w,
			dsdx, dtdx, dwdx,
			dsde, dtde, dwde,
			dsdy, dtdy, dwdy);
		break;
	}
	case 0x0c:      // Tri_Shade
	{
		const int32_t lft = (command >> 23) & 0x1;

		if (length != s_rdp_command_length[command]) {
			snprintf(buffer, buf_size, "ERROR: Tri_Shade length = %d\n", length);
			return;
		}

		for (int32_t i = 1; i < 12; i++) {
			cmd[i] = m_cmd_data[i];
		}

		snprintf(yl, s_element_buf_size, "%4.4f", (float)((cmd[0] >> 32) & 0x1fff) / 4.0f);
		snprintf(ym, s_element_buf_size, "%4.4f", (float)((cmd[0] >> 16) & 0x1fff) / 4.0f);
		snprintf(yh, s_element_buf_size, "%4.4f", (float)((cmd[0] >> 0) & 0x1fff) / 4.0f);
		snprintf(xl, s_element_buf_size, "%4.4f", (float)int32_t(cmd[1] >> 32) / 65536.0f);
		snprintf(dxldy, s_element_buf_size, "%4.4f", (float)int32_t(cmd[1]) / 65536.0f);
		snprintf(xh, s_element_buf_size, "%4.4f", (float)int32_t(cmd[2] >> 32) / 65536.0f);
		snprintf(dxhdy, s_element_buf_size, "%4.4f", (float)int32_t(cmd[2]) / 65536.0f);
		snprintf(xm, s_element_buf_size, "%4.4f", (float)int32_t(cmd[3] >> 32) / 65536.0f);
		snprintf(dxmdy, s_element_buf_size, "%4.4f", (float)int32_t(cmd[3]) / 65536.0f);

		snprintf(rt, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[4] >> 32) & 0xffff0000) | ((cmd[6] >> 48) & 0xffff)) / 65536.0f);
		snprintf(gt, s_element_buf_size, "%4.4f", (float)int32_t((((cmd[4] >> 32) & 0x0000ffff) << 16) | ((cmd[6] >> 32) & 0xffff)) / 65536.0f);
		snprintf(bt, s_element_buf_size, "%4.4f", (float)int32_t((cmd[4] & 0xffff0000) | ((cmd[6] >> 16) & 0xffff)) / 65536.0f);
		snprintf(at, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[4] & 0x0000ffff) << 16) | (cmd[6] & 0xffff)) / 65536.0f);
		snprintf(drdx, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[5] >> 32) & 0xffff0000) | ((cmd[7] >> 48) & 0xffff)) / 65536.0f);
		snprintf(dgdx, s_element_buf_size, "%4.4f", (float)int32_t((((cmd[5] >> 32) & 0x0000ffff) << 16) | ((cmd[7] >> 32) & 0xffff)) / 65536.0f);
		snprintf(dbdx, s_element_buf_size, "%4.4f", (float)int32_t((cmd[5] & 0xffff0000) | ((cmd[7] >> 16) & 0xffff)) / 65536.0f);
		snprintf(dadx, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[5] & 0x0000ffff) << 16) | (cmd[7] & 0xffff)) / 65536.0f);
		snprintf(drde, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[8] >> 32) & 0xffff0000) | ((cmd[10] >> 48) & 0xffff)) / 65536.0f);
		snprintf(dgde, s_element_buf_size, "%4.4f", (float)int32_t((((cmd[8] >> 32) & 0x0000ffff) << 16) | ((cmd[10] >> 32) & 0xffff)) / 65536.0f);
		snprintf(dbde, s_element_buf_size, "%4.4f", (float)int32_t((cmd[8] & 0xffff0000) | ((cmd[10] >> 16) & 0xffff)) / 65536.0f);
		snprintf(dade, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[8] & 0x0000ffff) << 16) | (cmd[10] & 0xffff)) / 65536.0f);
		snprintf(drdy, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[9] >> 32) & 0xffff0000) | ((cmd[11] >> 48) & 0xffff)) / 65536.0f);
		snprintf(dgdy, s_element_buf_size, "%4.4f", (float)int32_t((((cmd[9] >> 32) & 0x0000ffff) << 16) | ((cmd[11] >> 32) & 0xffff)) / 65536.0f);
		snprintf(dbdy, s_element_buf_size, "%4.4f", (float)int32_t((cmd[9] & 0xffff0000) | ((cmd[11] >> 16) & 0xffff)) / 65536.0f);
		snprintf(dady, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[9] & 0x0000ffff) << 16) | (cmd[11] & 0xffff)) / 65536.0f);

		snprintf(buffer, buf_size, "Tri_Shade              %d, XL: %s, XM: %s, XH: %s, YL: %s, YM: %s, YH: %s\n"
			"                              "
			"                       R: %s, G: %s, B: %s, A: %s\n"
			"                              "
			"                       DRDX: %s, DGDX: %s, DBDX: %s, DADX: %s\n"
			"                              "
			"                       DRDE: %s, DGDE: %s, DBDE: %s, DADE: %s\n"
			"                              "
			"                       DRDY: %s, DGDY: %s, DBDY: %s, DADY: %s\n",
			lft, xl, xm, xh, yl, ym, yh,
			rt, gt, bt, at,
			drdx, dgdx, dbdx, dadx,
			drde, dgde, dbde, dade,
			drdy, dgdy, dbdy, dady);
		break;
	}
	case 0x0d:      // Tri_ShadeZ
	{
		const int32_t lft = (command >> 23) & 0x1;

		if (length != s_rdp_command_length[command]) {
			snprintf(buffer, buf_size, "ERROR: Tri_ShadeZ length = %d\n", length);
			return;
		}

		for (int32_t i = 1; i < 12; i++) {
			cmd[i] = m_cmd_data[i];
		}

		snprintf(yl, s_element_buf_size, "%4.4f", (float)((cmd[0] >> 32) & 0x1fff) / 4.0f);
		snprintf(ym, s_element_buf_size, "%4.4f", (float)((cmd[0] >> 16) & 0x1fff) / 4.0f);
		snprintf(yh, s_element_buf_size, "%4.4f", (float)((cmd[0] >> 0) & 0x1fff) / 4.0f);
		snprintf(xl, s_element_buf_size, "%4.4f", (float)int32_t(cmd[1] >> 32) / 65536.0f);
		snprintf(dxldy, s_element_buf_size, "%4.4f", (float)int32_t(cmd[1]) / 65536.0f);
		snprintf(xh, s_element_buf_size, "%4.4f", (float)int32_t(cmd[2] >> 32) / 65536.0f);
		snprintf(dxhdy, s_element_buf_size, "%4.4f", (float)int32_t(cmd[2]) / 65536.0f);
		snprintf(xm, s_element_buf_size, "%4.4f", (float)int32_t(cmd[3] >> 32) / 65536.0f);
		snprintf(dxmdy, s_element_buf_size, "%4.4f", (float)int32_t(cmd[3]) / 65536.0f);

		snprintf(rt, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[4] >> 32) & 0xffff0000) | ((cmd[6] >> 48) & 0xffff)) / 65536.0f);
		snprintf(gt, s_element_buf_size, "%4.4f", (float)int32_t((((cmd[4] >> 32) & 0x0000ffff) << 16) | ((cmd[6] >> 32) & 0xffff)) / 65536.0f);
		snprintf(bt, s_element_buf_size, "%4.4f", (float)int32_t((cmd[4] & 0xffff0000) | ((cmd[6] >> 16) & 0xffff)) / 65536.0f);
		snprintf(at, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[4] & 0x0000ffff) << 16) | (cmd[6] & 0xffff)) / 65536.0f);
		snprintf(drdx, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[5] >> 32) & 0xffff0000) | ((cmd[7] >> 48) & 0xffff)) / 65536.0f);
		snprintf(dgdx, s_element_buf_size, "%4.4f", (float)int32_t((((cmd[5] >> 32) & 0x0000ffff) << 16) | ((cmd[7] >> 32) & 0xffff)) / 65536.0f);
		snprintf(dbdx, s_element_buf_size, "%4.4f", (float)int32_t((cmd[5] & 0xffff0000) | ((cmd[7] >> 16) & 0xffff)) / 65536.0f);
		snprintf(dadx, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[5] & 0x0000ffff) << 16) | (cmd[7] & 0xffff)) / 65536.0f);
		snprintf(drde, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[8] >> 32) & 0xffff0000) | ((cmd[10] >> 48) & 0xffff)) / 65536.0f);
		snprintf(dgde, s_element_buf_size, "%4.4f", (float)int32_t((((cmd[8] >> 32) & 0x0000ffff) << 16) | ((cmd[10] >> 32) & 0xffff)) / 65536.0f);
		snprintf(dbde, s_element_buf_size, "%4.4f", (float)int32_t((cmd[8] & 0xffff0000) | ((cmd[10] >> 16) & 0xffff)) / 65536.0f);
		snprintf(dade, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[8] & 0x0000ffff) << 16) | (cmd[10] & 0xffff)) / 65536.0f);
		snprintf(drdy, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[9] >> 32) & 0xffff0000) | ((cmd[11] >> 48) & 0xffff)) / 65536.0f);
		snprintf(dgdy, s_element_buf_size, "%4.4f", (float)int32_t((((cmd[9] >> 32) & 0x0000ffff) << 16) | ((cmd[11] >> 32) & 0xffff)) / 65536.0f);
		snprintf(dbdy, s_element_buf_size, "%4.4f", (float)int32_t((cmd[9] & 0xffff0000) | ((cmd[11] >> 16) & 0xffff)) / 65536.0f);
		snprintf(dady, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[9] & 0x0000ffff) << 16) | (cmd[11] & 0xffff)) / 65536.0f);

		snprintf(buffer, buf_size, "Tri_ShadeZ             %d, XL: %s, XM: %s, XH: %s, YL: %s, YM: %s, YH: %s\n"
			"                              "
			"                       R: %s, G: %s, B: %s, A: %s\n"
			"                              "
			"                       DRDX: %s, DGDX: %s, DBDX: %s, DADX: %s\n"
			"                              "
			"                       DRDE: %s, DGDE: %s, DBDE: %s, DADE: %s\n"
			"                              "
			"                       DRDY: %s, DGDY: %s, DBDY: %s, DADY: %s\n",
			lft, xl, xm, xh, yl, ym, yh,
			rt, gt, bt, at,
			drdx, dgdx, dbdx, dadx,
			drde, dgde, dbde, dade,
			drdy, dgdy, dbdy, dady);
		break;
	}
	case 0x0e:      // Tri_TexShade
	{
		const int32_t lft = (command >> 23) & 0x1;

		if (length < s_rdp_command_length[command]) {
			snprintf(buffer, buf_size, "ERROR: Tri_TexShade length = %d\n", length);
			return;
		}

		for (int32_t i = 1; i < 20; i++) {
			cmd[i] = m_cmd_data[m_cmd_cur + i];
		}

		snprintf(yl, s_element_buf_size, "%4.4f", (float)((cmd[0] >> 32) & 0x1fff) / 4.0f);
		snprintf(ym, s_element_buf_size, "%4.4f", (float)((cmd[0] >> 16) & 0x1fff) / 4.0f);
		snprintf(yh, s_element_buf_size, "%4.4f", (float)((cmd[0] >> 0) & 0x1fff) / 4.0f);
		snprintf(xl, s_element_buf_size, "%4.4f", (float)int32_t(cmd[1] >> 32) / 65536.0f);
		snprintf(dxldy, s_element_buf_size, "%4.4f", (float)int32_t(cmd[1]) / 65536.0f);
		snprintf(xh, s_element_buf_size, "%4.4f", (float)int32_t(cmd[2] >> 32) / 65536.0f);
		snprintf(dxhdy, s_element_buf_size, "%4.4f", (float)int32_t(cmd[2]) / 65536.0f);
		snprintf(xm, s_element_buf_size, "%4.4f", (float)int32_t(cmd[3] >> 32) / 65536.0f);
		snprintf(dxmdy, s_element_buf_size, "%4.4f", (float)int32_t(cmd[3]) / 65536.0f);

		snprintf(rt, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[4] >> 32) & 0xffff0000) | ((cmd[6] >> 48) & 0xffff)) / 65536.0f);
		snprintf(gt, s_element_buf_size, "%4.4f", (float)int32_t((((cmd[4] >> 32) & 0x0000ffff) << 16) | ((cmd[6] >> 32) & 0xffff)) / 65536.0f);
		snprintf(bt, s_element_buf_size, "%4.4f", (float)int32_t((cmd[4] & 0xffff0000) | ((cmd[6] >> 16) & 0xffff)) / 65536.0f);
		snprintf(at, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[4] & 0x0000ffff) << 16) | (cmd[6] & 0xffff)) / 65536.0f);
		snprintf(drdx, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[5] >> 32) & 0xffff0000) | ((cmd[7] >> 48) & 0xffff)) / 65536.0f);
		snprintf(dgdx, s_element_buf_size, "%4.4f", (float)int32_t((((cmd[5] >> 32) & 0x0000ffff) << 16) | ((cmd[7] >> 32) & 0xffff)) / 65536.0f);
		snprintf(dbdx, s_element_buf_size, "%4.4f", (float)int32_t((cmd[5] & 0xffff0000) | ((cmd[7] >> 16) & 0xffff)) / 65536.0f);
		snprintf(dadx, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[5] & 0x0000ffff) << 16) | (cmd[7] & 0xffff)) / 65536.0f);
		snprintf(drde, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[8] >> 32) & 0xffff0000) | ((cmd[10] >> 48) & 0xffff)) / 65536.0f);
		snprintf(dgde, s_element_buf_size, "%4.4f", (float)int32_t((((cmd[8] >> 32) & 0x0000ffff) << 16) | ((cmd[10] >> 32) & 0xffff)) / 65536.0f);
		snprintf(dbde, s_element_buf_size, "%4.4f", (float)int32_t((cmd[8] & 0xffff0000) | ((cmd[10] >> 16) & 0xffff)) / 65536.0f);
		snprintf(dade, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[8] & 0x0000ffff) << 16) | (cmd[10] & 0xffff)) / 65536.0f);
		snprintf(drdy, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[9] >> 32) & 0xffff0000) | ((cmd[11] >> 48) & 0xffff)) / 65536.0f);
		snprintf(dgdy, s_element_buf_size, "%4.4f", (float)int32_t((((cmd[9] >> 32) & 0x0000ffff) << 16) | ((cmd[11] >> 32) & 0xffff)) / 65536.0f);
		snprintf(dbdy, s_element_buf_size, "%4.4f", (float)int32_t((cmd[9] & 0xffff0000) | ((cmd[11] >> 16) & 0xffff)) / 65536.0f);
		snprintf(dady, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[9] & 0x0000ffff) << 16) | (cmd[11] & 0xffff)) / 65536.0f);

		snprintf(s, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[4] >> 32) & 0xffff0000) | ((cmd[6] >> 48) & 0xffff)) / 65536.0f);
		snprintf(t, s_element_buf_size, "%4.4f", (float)int32_t((((cmd[4] >> 32) & 0x0000ffff) << 16) | ((cmd[6] >> 32) & 0xffff)) / 65536.0f);
		snprintf(w, s_element_buf_size, "%4.4f", (float)int32_t((cmd[4] & 0xffff0000) | ((cmd[6] >> 16) & 0xffff)) / 65536.0f);
		snprintf(dsdx, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[5] >> 32) & 0xffff0000) | ((cmd[7] >> 48) & 0xffff)) / 65536.0f);
		snprintf(dtdx, s_element_buf_size, "%4.4f", (float)int32_t((((cmd[5] >> 32) & 0x0000ffff) << 16) | ((cmd[7] >> 32) & 0xffff)) / 65536.0f);
		snprintf(dwdx, s_element_buf_size, "%4.4f", (float)int32_t((cmd[5] & 0xffff0000) | ((cmd[7] >> 16) & 0xffff)) / 65536.0f);
		snprintf(dsde, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[8] >> 32) & 0xffff0000) | ((cmd[10] >> 48) & 0xffff)) / 65536.0f);
		snprintf(dtde, s_element_buf_size, "%4.4f", (float)int32_t((((cmd[8] >> 32) & 0x0000ffff) << 16) | ((cmd[10] >> 32) & 0xffff)) / 65536.0f);
		snprintf(dwde, s_element_buf_size, "%4.4f", (float)int32_t((cmd[8] & 0xffff0000) | ((cmd[10] >> 16) & 0xffff)) / 65536.0f);
		snprintf(dsdy, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[9] >> 32) & 0xffff0000) | ((cmd[11] >> 48) & 0xffff)) / 65536.0f);
		snprintf(dtdy, s_element_buf_size, "%4.4f", (float)int32_t((((cmd[9] >> 32) & 0x0000ffff) << 16) | ((cmd[11] >> 32) & 0xffff)) / 65536.0f);
		snprintf(dwdy, s_element_buf_size, "%4.4f", (float)int32_t((cmd[9] & 0xffff0000) | ((cmd[11] >> 16) & 0xffff)) / 65536.0f);

		snprintf(buffer, buf_size, "Tri_TexShade           %d, XL: %s, XM: %s, XH: %s, YL: %s, YM: %s, YH: %s\n"
			"                              "
			"                       R: %s, G: %s, B: %s, A: %s\n"
			"                              "
			"                       DRDX: %s, DGDX: %s, DBDX: %s, DADX: %s\n"
			"                              "
			"                       DRDE: %s, DGDE: %s, DBDE: %s, DADE: %s\n"
			"                              "
			"                       DRDY: %s, DGDY: %s, DBDY: %s, DADY: %s\n"
			"                              "
			"                       S: %s, T: %s, W: %s\n"
			"                              "
			"                       DSDX: %s, DTDX: %s, DWDX: %s\n"
			"                              "
			"                       DSDE: %s, DTDE: %s, DWDE: %s\n"
			"                              "
			"                       DSDY: %s, DTDY: %s, DWDY: %s\n",
			lft, xl, xm, xh, yl, ym, yh,
			rt, gt, bt, at,
			drdx, dgdx, dbdx, dadx,
			drde, dgde, dbde, dade,
			drdy, dgdy, dbdy, dady,
			s, t, w,
			dsdx, dtdx, dwdx,
			dsde, dtde, dwde,
			dsdy, dtdy, dwdy);
		break;
	}
	case 0x0f:      // Tri_TexShadeZ
	{
		const int32_t lft = (command >> 23) & 0x1;

		if (length < s_rdp_command_length[command]) {
			snprintf(buffer, buf_size, "ERROR: Tri_TexShadeZ length = %d\n", length);
			return;
		}

		for (int32_t i = 1; i < 20; i++) {
			cmd[i] = m_cmd_data[m_cmd_cur + i];
		}

		snprintf(yl, s_element_buf_size, "%4.4f", (float)((cmd[0] >> 32) & 0x1fff) / 4.0f);
		snprintf(ym, s_element_buf_size, "%4.4f", (float)((cmd[0] >> 16) & 0x1fff) / 4.0f);
		snprintf(yh, s_element_buf_size, "%4.4f", (float)((cmd[0] >> 0) & 0x1fff) / 4.0f);
		snprintf(xl, s_element_buf_size, "%4.4f", (float)int32_t(cmd[1] >> 32) / 65536.0f);
		snprintf(dxldy, s_element_buf_size, "%4.4f", (float)int32_t(cmd[1]) / 65536.0f);
		snprintf(xh, s_element_buf_size, "%4.4f", (float)int32_t(cmd[2] >> 32) / 65536.0f);
		snprintf(dxhdy, s_element_buf_size, "%4.4f", (float)int32_t(cmd[2]) / 65536.0f);
		snprintf(xm, s_element_buf_size, "%4.4f", (float)int32_t(cmd[3] >> 32) / 65536.0f);
		snprintf(dxmdy, s_element_buf_size, "%4.4f", (float)int32_t(cmd[3]) / 65536.0f);

		snprintf(rt, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[4] >> 32) & 0xffff0000) | ((cmd[6] >> 48) & 0xffff)) / 65536.0f);
		snprintf(gt, s_element_buf_size, "%4.4f", (float)int32_t((((cmd[4] >> 32) & 0x0000ffff) << 16) | ((cmd[6] >> 32) & 0xffff)) / 65536.0f);
		snprintf(bt, s_element_buf_size, "%4.4f", (float)int32_t((cmd[4] & 0xffff0000) | ((cmd[6] >> 16) & 0xffff)) / 65536.0f);
		snprintf(at, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[4] & 0x0000ffff) << 16) | (cmd[6] & 0xffff)) / 65536.0f);
		snprintf(drdx, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[5] >> 32) & 0xffff0000) | ((cmd[7] >> 48) & 0xffff)) / 65536.0f);
		snprintf(dgdx, s_element_buf_size, "%4.4f", (float)int32_t((((cmd[5] >> 32) & 0x0000ffff) << 16) | ((cmd[7] >> 32) & 0xffff)) / 65536.0f);
		snprintf(dbdx, s_element_buf_size, "%4.4f", (float)int32_t((cmd[5] & 0xffff0000) | ((cmd[7] >> 16) & 0xffff)) / 65536.0f);
		snprintf(dadx, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[5] & 0x0000ffff) << 16) | (cmd[7] & 0xffff)) / 65536.0f);
		snprintf(drde, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[8] >> 32) & 0xffff0000) | ((cmd[10] >> 48) & 0xffff)) / 65536.0f);
		snprintf(dgde, s_element_buf_size, "%4.4f", (float)int32_t((((cmd[8] >> 32) & 0x0000ffff) << 16) | ((cmd[10] >> 32) & 0xffff)) / 65536.0f);
		snprintf(dbde, s_element_buf_size, "%4.4f", (float)int32_t((cmd[8] & 0xffff0000) | ((cmd[10] >> 16) & 0xffff)) / 65536.0f);
		snprintf(dade, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[8] & 0x0000ffff) << 16) | (cmd[10] & 0xffff)) / 65536.0f);
		snprintf(drdy, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[9] >> 32) & 0xffff0000) | ((cmd[11] >> 48) & 0xffff)) / 65536.0f);
		snprintf(dgdy, s_element_buf_size, "%4.4f", (float)int32_t((((cmd[9] >> 32) & 0x0000ffff) << 16) | ((cmd[11] >> 32) & 0xffff)) / 65536.0f);
		snprintf(dbdy, s_element_buf_size, "%4.4f", (float)int32_t((cmd[9] & 0xffff0000) | ((cmd[11] >> 16) & 0xffff)) / 65536.0f);
		snprintf(dady, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[9] & 0x0000ffff) << 16) | (cmd[11] & 0xffff)) / 65536.0f);

		snprintf(s, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[4] >> 32) & 0xffff0000) | ((cmd[6] >> 48) & 0xffff)) / 65536.0f);
		snprintf(t, s_element_buf_size, "%4.4f", (float)int32_t((((cmd[4] >> 32) & 0x0000ffff) << 16) | ((cmd[6] >> 32) & 0xffff)) / 65536.0f);
		snprintf(w, s_element_buf_size, "%4.4f", (float)int32_t((cmd[4] & 0xffff0000) | ((cmd[6] >> 16) & 0xffff)) / 65536.0f);
		snprintf(dsdx, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[5] >> 32) & 0xffff0000) | ((cmd[7] >> 48) & 0xffff)) / 65536.0f);
		snprintf(dtdx, s_element_buf_size, "%4.4f", (float)int32_t((((cmd[5] >> 32) & 0x0000ffff) << 16) | ((cmd[7] >> 32) & 0xffff)) / 65536.0f);
		snprintf(dwdx, s_element_buf_size, "%4.4f", (float)int32_t((cmd[5] & 0xffff0000) | ((cmd[7] >> 16) & 0xffff)) / 65536.0f);
		snprintf(dsde, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[8] >> 32) & 0xffff0000) | ((cmd[10] >> 48) & 0xffff)) / 65536.0f);
		snprintf(dtde, s_element_buf_size, "%4.4f", (float)int32_t((((cmd[8] >> 32) & 0x0000ffff) << 16) | ((cmd[10] >> 32) & 0xffff)) / 65536.0f);
		snprintf(dwde, s_element_buf_size, "%4.4f", (float)int32_t((cmd[8] & 0xffff0000) | ((cmd[10] >> 16) & 0xffff)) / 65536.0f);
		snprintf(dsdy, s_element_buf_size, "%4.4f", (float)int32_t(((cmd[9] >> 32) & 0xffff0000) | ((cmd[11] >> 48) & 0xffff)) / 65536.0f);
		snprintf(dtdy, s_element_buf_size, "%4.4f", (float)int32_t((((cmd[9] >> 32) & 0x0000ffff) << 16) | ((cmd[11] >> 32) & 0xffff)) / 65536.0f);
		snprintf(dwdy, s_element_buf_size, "%4.4f", (float)int32_t((cmd[9] & 0xffff0000) | ((cmd[11] >> 16) & 0xffff)) / 65536.0f);

		snprintf(buffer, buf_size, "Tri_TexShadeZ           %d, XL: %s, XM: %s, XH: %s, YL: %s, YM: %s, YH: %s\n"
			"                              "
			"                       R: %s, G: %s, B: %s, A: %s\n"
			"                              "
			"                       DRDX: %s, DGDX: %s, DBDX: %s, DADX: %s\n"
			"                              "
			"                       DRDE: %s, DGDE: %s, DBDE: %s, DADE: %s\n"
			"                              "
			"                       DRDY: %s, DGDY: %s, DBDY: %s, DADY: %s\n"
			"                              "
			"                       S: %s, T: %s, W: %s\n"
			"                              "
			"                       DSDX: %s, DTDX: %s, DWDX: %s\n"
			"                              "
			"                       DSDE: %s, DTDE: %s, DWDE: %s\n"
			"                              "
			"                       DSDY: %s, DTDY: %s, DWDY: %s\n",
			lft, xl, xm, xh, yl, ym, yh,
			rt, gt, bt, at,
			drdx, dgdx, dbdx, dadx,
			drde, dgde, dbde, dade,
			drdy, dgdy, dbdy, dady,
			s, t, w,
			dsdx, dtdx, dwdx,
			dsde, dtde, dwde,
			dsdy, dtdy, dwdy);
		break;
	}
	case 0x24:
	case 0x25:
	{
		if (length < 16) {
			sprintf(buffer, "ERROR: Texture_Rectangle length = %d\n", length);
			return;
		}

		cmd[1] = m_cmd_data[m_cmd_cur + 1];
		snprintf(s, s_element_buf_size, "%4.4f", (float)int16_t((cmd[1] >> 48) & 0xffff) / 32.0f);
		snprintf(t, s_element_buf_size, "%4.4f", (float)int16_t((cmd[1] >> 32) & 0xffff) / 32.0f);
		snprintf(dsdx, s_element_buf_size, "%4.4f", (float)int16_t((cmd[1] >> 16) & 0xffff) / 1024.0f);
		snprintf(dtdy, s_element_buf_size, "%4.4f", (float)int16_t((cmd[1] >> 0) & 0xffff) / 1024.0f);

		if (command == 0x24)
			snprintf(buffer, buf_size, "Texture_Rectangle      %d, %s, %s, %s, %s,  %s, %s, %s, %s", tile, sh, th, sl, tl, s, t, dsdx, dtdy);
		else
			snprintf(buffer, buf_size, "Texture_Rectangle_Flip %d, %s, %s, %s, %s,  %s, %s, %s, %s", tile, sh, th, sl, tl, s, t, dsdx, dtdy);

		break;
	}
	case 0x26:  snprintf(buffer, buf_size, "Sync_Load"); break;
	case 0x27:  snprintf(buffer, buf_size, "Sync_Pipe"); break;
	case 0x28:  snprintf(buffer, buf_size, "Sync_Tile"); break;
	case 0x29:  snprintf(buffer, buf_size, "Sync_Full"); break;
	case 0x2d:  snprintf(buffer, buf_size, "Set_Scissor            %s, %s, %s, %s", sl, tl, sh, th); break;
	case 0x2e:  snprintf(buffer, buf_size, "Set_Prim_Depth         %04X, %04X", uint32_t(cmd[0] >> 16) & 0xffff, (uint32_t)cmd[0] & 0xffff); break;
	case 0x2f:  snprintf(buffer, buf_size, "Set_Other_Modes        %08X %08X", uint32_t(cmd[0] >> 32), (uint32_t)cmd[0]); break;
	case 0x30:  snprintf(buffer, buf_size, "Load_TLUT              %d, %s, %s, %s, %s", tile, sl, tl, sh, th); break;
	case 0x32:  snprintf(buffer, buf_size, "Set_Tile_Size          %d, %s, %s, %s, %s", tile, sl, tl, sh, th); break;
	case 0x33:  snprintf(buffer, buf_size, "Load_Block             %d, %03X, %03X, %03X, %03X", tile, uint32_t(cmd[0] >> 44) & 0xfff, uint32_t(cmd[0] >> 32) & 0xfff, uint32_t(cmd[0] >> 12) & 0xfff, uint32_t(cmd[1]) & 0xfff); break;
	case 0x34:  snprintf(buffer, buf_size, "Load_Tile              %d, %s, %s, %s, %s", tile, sl, tl, sh, th); break;
	case 0x35:  snprintf(buffer, buf_size, "Set_Tile               %d, %s, %s, %d, %04X", tile, format, size, (uint32_t(cmd[0] >> 41) & 0x1ff) * 8, (uint32_t(cmd[0] >> 32) & 0x1ff) * 8); break;
	case 0x36:  snprintf(buffer, buf_size, "Fill_Rectangle         %s, %s, %s, %s", sh, th, sl, tl); break;
	case 0x37:  snprintf(buffer, buf_size, "Set_Fill_Color         R: %d, G: %d, B: %d, A: %d", r, g, b, a); break;
	case 0x38:  snprintf(buffer, buf_size, "Set_Fog_Color          R: %d, G: %d, B: %d, A: %d", r, g, b, a); break;
	case 0x39:  snprintf(buffer, buf_size, "Set_Blend_Color        R: %d, G: %d, B: %d, A: %d", r, g, b, a); break;
	case 0x3a:  snprintf(buffer, buf_size, "Set_Prim_Color         %d, %d, R: %d, G: %d, B: %d, A: %d", uint32_t(cmd[0] >> 40) & 0x1f, uint32_t(cmd[0] >> 32) & 0xff, r, g, b, a); break;
	case 0x3b:  snprintf(buffer, buf_size, "Set_Env_Color          R: %d, G: %d, B: %d, A: %d", r, g, b, a); break;
	case 0x3c:  snprintf(buffer, buf_size, "Set_Combine            %08X %08X", uint32_t(cmd[0] >> 32), (uint32_t)cmd[0]); break;
	case 0x3d:  snprintf(buffer, buf_size, "Set_Texture_Image      %s, %s, %d, %08X", format, size, (uint32_t(cmd[0] >> 32) & 0x1ff) + 1, (uint32_t)cmd[0]); break;
	case 0x3e:  snprintf(buffer, buf_size, "Set_Mask_Image         %08X", (uint32_t)cmd[0]); break;
	case 0x3f:  snprintf(buffer, buf_size, "Set_Color_Image        %s, %s, %d, %08X", format, size, (uint32_t(cmd[0] >> 32) & 0x1ff) + 1, (uint32_t)cmd[0]); break;
	default:    snprintf(buffer, buf_size, "Unknown (%08X %08X)", uint32_t(cmd[0] >> 32), (uint32_t)cmd[0]); break;
	}
}

/*****************************************************************************/

static uint32_t rightcvghex(uint32_t x, uint32_t fmask) {
	uint32_t stickybit = ((x >> 1) & 0x1fff) > 0;
	uint32_t covered = ((x >> 14) & 3) + stickybit;
	covered = (0xf0 >> covered) & 0xf;
	return (covered & fmask);
}

static uint32_t leftcvghex(uint32_t x, uint32_t fmask) {
	uint32_t stickybit = ((x >> 1) & 0x1fff) > 0;
	uint32_t covered = ((x >> 14) & 3) + stickybit;
	covered = 0xf >> covered;
	return (covered & fmask);
}

static int32_t CLIP(int32_t value, int32_t min, int32_t max) {
	if (value < min) {
		return min;
	} else if (value > max) {
		return max;
	} else {
		return value;
	}
}

void n64_rdp::compute_cvg_noflip(int32_t* majorx, int32_t* minorx, int32_t* majorxint, int32_t* minorxint, int32_t scanline, int32_t yh, int32_t yl, int32_t base) {
	int32_t purgestart = 0xfff;
	int32_t purgeend = 0;
	const bool writablescanline = !(scanline & ~0x3ff);
	const int32_t scanlinespx = scanline << 2;

	if (!writablescanline) return;

	for (int32_t i = 0; i < 4; i++) {
		if (minorxint[i] < purgestart) {
			purgestart = minorxint[i];
		}
		if (majorxint[i] > purgeend) {
			purgeend = majorxint[i];
		}
	}

	purgestart = CLIP(purgestart, 0, 1023);
	purgeend = CLIP(purgeend, 0, 1023);
	int32_t length = purgeend - purgestart;

	if (length < 0) return;

	memset(&m_cvg[purgestart], 0, (length + 1) << 1);

	for (int32_t i = 0; i < 4; i++) {
		int32_t minorcur = minorx[i];
		int32_t majorcur = majorx[i];
		int32_t minorcurint = minorxint[i];
		int32_t majorcurint = majorxint[i];
		length = majorcurint - minorcurint;

		int32_t fmask = (i & 1) ? 5 : 0xa;
		int32_t maskshift = (i ^ 3) << 2;
		int32_t fmaskshifted = fmask << maskshift;
		int32_t fleft = CLIP(minorcurint + 1, 0, 647);
		int32_t fright = CLIP(majorcurint - 1, 0, 647);
		bool valid_y = ((scanlinespx + i) >= yh && (scanlinespx + i) < yl);
		if (valid_y && length >= 0) {
			if (minorcurint != majorcurint) {
				if (!(minorcurint & ~0x3ff)) {
					m_cvg[minorcurint] |= (leftcvghex(minorcur, fmask) << maskshift);
				}
				if (!(majorcurint & ~0x3ff)) {
					m_cvg[majorcurint] |= (rightcvghex(majorcur, fmask) << maskshift);
				}
			} else {
				if (!(majorcurint & ~0x3ff)) {
					int32_t samecvg = leftcvghex(minorcur, fmask) & rightcvghex(majorcur, fmask);
					m_cvg[majorcurint] |= (samecvg << maskshift);
				}
			}
			for (; fleft <= fright; fleft++) {
				m_cvg[fleft] |= fmaskshifted;
			}
		}
	}
}

void n64_rdp::compute_cvg_flip(int32_t* majorx, int32_t* minorx, int32_t* majorxint, int32_t* minorxint, int32_t scanline, int32_t yh, int32_t yl, int32_t base) {
	int32_t purgestart = 0xfff;
	int32_t purgeend = 0;
	const bool writablescanline = !(scanline & ~0x3ff);
	const int32_t scanlinespx = scanline << 2;

	if (!writablescanline) return;

	for (int32_t i = 0; i < 4; i++) {
		if (majorxint[i] < purgestart) {
			purgestart = majorxint[i];
		}
		if (minorxint[i] > purgeend) {
			purgeend = minorxint[i];
		}
	}

	purgestart = CLIP(purgestart, 0, 1023);
	purgeend = CLIP(purgeend, 0, 1023);

	int32_t length = purgeend - purgestart;

	if (length < 0) return;

	memset(&m_cvg[purgestart], 0, (length + 1) << 1);

	for (int32_t i = 0; i < 4; i++) {
		int32_t minorcur = minorx[i];
		int32_t majorcur = majorx[i];
		int32_t minorcurint = minorxint[i];
		int32_t majorcurint = majorxint[i];
		length = minorcurint - majorcurint;

		int32_t fmask = (i & 1) ? 5 : 0xa;
		int32_t maskshift = (i ^ 3) << 2;
		int32_t fmaskshifted = fmask << maskshift;
		int32_t fleft = CLIP(majorcurint + 1, 0, 647);
		int32_t fright = CLIP(minorcurint - 1, 0, 647);
		bool valid_y = ((scanlinespx + i) >= yh && (scanlinespx + i) < yl);
		if (valid_y && length >= 0) {
			if (minorcurint != majorcurint) {
				if (!(minorcurint & ~0x3ff)) {
					m_cvg[minorcurint] |= (rightcvghex(minorcur, fmask) << maskshift);
				}
				if (!(majorcurint & ~0x3ff)) {
					m_cvg[majorcurint] |= (leftcvghex(majorcur, fmask) << maskshift);
				}
			} else {
				if (!(majorcurint & ~0x3ff)) {
					int32_t samecvg = rightcvghex(minorcur, fmask) & leftcvghex(majorcur, fmask);
					m_cvg[majorcurint] |= (samecvg << maskshift);
				}
			}
			for (; fleft <= fright; fleft++) {
				m_cvg[fleft] |= fmaskshifted;
			}
		}
	}
}

#define SIGN(x, numb)	(((x) & ((1 << numb) - 1)) | -((x) & (1 << (numb - 1))))

void n64_rdp::draw_triangle(bool shade, bool texture, bool zbuffer, bool rect) {
	const uint64_t* cmd_data = rect ? m_temp_rect_data : m_cmd_data;
	const uint32_t fifo_index = rect ? 0 : m_cmd_cur;
	const uint64_t w1 = cmd_data[fifo_index + 0];

	bool flip = (int32_t(w1 >> 55) & 1) != 0;
	m_misc_state.m_max_level = int32_t(w1 >> 51) & 7;
	int32_t tilenum = int32_t(w1 >> 48) & 0x7;

	int32_t dsdiff = 0, dtdiff = 0, dwdiff = 0, drdiff = 0, dgdiff = 0, dbdiff = 0, dadiff = 0, dzdiff = 0;
	int32_t dsdeh = 0, dtdeh = 0, dwdeh = 0, drdeh = 0, dgdeh = 0, dbdeh = 0, dadeh = 0, dzdeh = 0;
	int32_t dsdxh = 0, dtdxh = 0, dwdxh = 0, drdxh = 0, dgdxh = 0, dbdxh = 0, dadxh = 0, dzdxh = 0;
	int32_t dsdyh = 0, dtdyh = 0, dwdyh = 0, drdyh = 0, dgdyh = 0, dbdyh = 0, dadyh = 0, dzdyh = 0;

	int32_t maxxmx = 0; // maxxmx / minxhx very opaque names, consider re-naming
	int32_t minxmx = 0;
	int32_t maxxhx = 0;
	int32_t minxhx = 0;

	int32_t shade_base = fifo_index + 4;
	int32_t texture_base = fifo_index + 4;
	int32_t zbuffer_base = fifo_index + 4;
	if (shade) {
		texture_base += 8;
		zbuffer_base += 8;
	}
	if (texture) {
		zbuffer_base += 8;
	}

	uint64_t w2 = cmd_data[fifo_index + 1];
	uint64_t w3 = cmd_data[fifo_index + 2];
	uint64_t w4 = cmd_data[fifo_index + 3];

	int32_t yl = int32_t(w1 >> 32) & 0x3fff;
	int32_t ym = int32_t(w1 >> 16) & 0x3fff;
	int32_t yh = int32_t(w1 >> 0) & 0x3fff;
	int32_t xl = (int32_t)(w2 >> 32) & 0x3fffffff;
	int32_t xh = (int32_t)(w3 >> 32) & 0x3fffffff;
	int32_t xm = (int32_t)(w4 >> 32) & 0x3fffffff;
	// Inverse slopes in 16.16 format
	int32_t dxldy = (int32_t)w2;
	int32_t dxhdy = (int32_t)w3;
	int32_t dxmdy = (int32_t)w4;

	if (yl & 0x2000)  yl |= 0xffffc000;
	if (ym & 0x2000)  ym |= 0xffffc000;
	if (yh & 0x2000)  yh |= 0xffffc000;

	if (xl & 0x20000000)  xl |= 0xc0000000;
	if (xm & 0x20000000)  xm |= 0xc0000000;
	if (xh & 0x20000000)  xh |= 0xc0000000;

	int32_t r = int32_t(((cmd_data[shade_base] >> 32) & 0xffff0000) | ((cmd_data[shade_base + 2] >> 48) & 0x0000ffff));
	int32_t g = int32_t(((cmd_data[shade_base] >> 16) & 0xffff0000) | ((cmd_data[shade_base + 2] >> 32) & 0x0000ffff));
	int32_t b = int32_t((cmd_data[shade_base] & 0xffff0000) | ((cmd_data[shade_base + 2] >> 16) & 0x0000ffff));
	int32_t a = int32_t(((cmd_data[shade_base] << 16) & 0xffff0000) | (cmd_data[shade_base + 2] & 0x0000ffff));
	const int32_t drdx = int32_t(((cmd_data[shade_base + 1] >> 32) & 0xffff0000) | ((cmd_data[shade_base + 3] >> 48) & 0x0000ffff));
	const int32_t dgdx = int32_t(((cmd_data[shade_base + 1] >> 16) & 0xffff0000) | ((cmd_data[shade_base + 3] >> 32) & 0x0000ffff));
	const int32_t dbdx = int32_t((cmd_data[shade_base + 1] & 0xffff0000) | ((cmd_data[shade_base + 3] >> 16) & 0x0000ffff));
	const int32_t dadx = int32_t(((cmd_data[shade_base + 1] << 16) & 0xffff0000) | (cmd_data[shade_base + 3] & 0x0000ffff));
	const int32_t drde = int32_t(((cmd_data[shade_base + 4] >> 32) & 0xffff0000) | ((cmd_data[shade_base + 6] >> 48) & 0x0000ffff));
	const int32_t dgde = int32_t(((cmd_data[shade_base + 4] >> 16) & 0xffff0000) | ((cmd_data[shade_base + 6] >> 32) & 0x0000ffff));
	const int32_t dbde = int32_t((cmd_data[shade_base + 4] & 0xffff0000) | ((cmd_data[shade_base + 6] >> 16) & 0x0000ffff));
	const int32_t dade = int32_t(((cmd_data[shade_base + 4] << 16) & 0xffff0000) | (cmd_data[shade_base + 6] & 0x0000ffff));
	const int32_t drdy = int32_t(((cmd_data[shade_base + 5] >> 32) & 0xffff0000) | ((cmd_data[shade_base + 7] >> 48) & 0x0000ffff));
	const int32_t dgdy = int32_t(((cmd_data[shade_base + 5] >> 16) & 0xffff0000) | ((cmd_data[shade_base + 7] >> 32) & 0x0000ffff));
	const int32_t dbdy = int32_t((cmd_data[shade_base + 5] & 0xffff0000) | ((cmd_data[shade_base + 7] >> 16) & 0x0000ffff));
	const int32_t dady = int32_t(((cmd_data[shade_base + 5] << 16) & 0xffff0000) | (cmd_data[shade_base + 7] & 0x0000ffff));

	int32_t s = int32_t(((cmd_data[texture_base] >> 32) & 0xffff0000) | ((cmd_data[texture_base + 2] >> 48) & 0x0000ffff));
	int32_t t = int32_t(((cmd_data[texture_base] >> 16) & 0xffff0000) | ((cmd_data[texture_base + 2] >> 32) & 0x0000ffff));
	int32_t w = int32_t((cmd_data[texture_base] & 0xffff0000) | ((cmd_data[texture_base + 2] >> 16) & 0x0000ffff));
	const int32_t dsdx = int32_t(((cmd_data[texture_base + 1] >> 32) & 0xffff0000) | ((cmd_data[texture_base + 3] >> 48) & 0x0000ffff));
	const int32_t dtdx = int32_t(((cmd_data[texture_base + 1] >> 16) & 0xffff0000) | ((cmd_data[texture_base + 3] >> 32) & 0x0000ffff));
	const int32_t dwdx = int32_t((cmd_data[texture_base + 1] & 0xffff0000) | ((cmd_data[texture_base + 3] >> 16) & 0x0000ffff));
	const int32_t dsde = int32_t(((cmd_data[texture_base + 4] >> 32) & 0xffff0000) | ((cmd_data[texture_base + 6] >> 48) & 0x0000ffff));
	const int32_t dtde = int32_t(((cmd_data[texture_base + 4] >> 16) & 0xffff0000) | ((cmd_data[texture_base + 6] >> 32) & 0x0000ffff));
	const int32_t dwde = int32_t((cmd_data[texture_base + 4] & 0xffff0000) | ((cmd_data[texture_base + 6] >> 16) & 0x0000ffff));
	const int32_t dsdy = int32_t(((cmd_data[texture_base + 5] >> 32) & 0xffff0000) | ((cmd_data[texture_base + 7] >> 48) & 0x0000ffff));
	const int32_t dtdy = int32_t(((cmd_data[texture_base + 5] >> 16) & 0xffff0000) | ((cmd_data[texture_base + 7] >> 32) & 0x0000ffff));
	const int32_t dwdy = int32_t((cmd_data[texture_base + 5] & 0xffff0000) | ((cmd_data[texture_base + 7] >> 16) & 0x0000ffff));

	int32_t z = int32_t(cmd_data[zbuffer_base] >> 32);
	const int32_t dzdx = int32_t(cmd_data[zbuffer_base]);
	const int32_t dzde = int32_t(cmd_data[zbuffer_base + 1] >> 32);
	const int32_t dzdy = int32_t(cmd_data[zbuffer_base + 1]);

	const int32_t dzdy_dz = (dzdy >> 16) & 0xffff;
	const int32_t dzdx_dz = (dzdx >> 16) & 0xffff;

	m_span_base.m_span_drdy = drdy;
	m_span_base.m_span_dgdy = dgdy;
	m_span_base.m_span_dbdy = dbdy;
	m_span_base.m_span_dady = dady;
	m_span_base.m_span_dzdy = m_other_modes.z_source_sel ? 0 : dzdy;

	uint32_t temp_dzpix = ((dzdy_dz & 0x8000) ? ((~dzdy_dz) & 0x7fff) : dzdy_dz) + ((dzdx_dz & 0x8000) ? ((~dzdx_dz) & 0x7fff) : dzdx_dz);
	m_span_base.m_span_dr = drdx & ~0x1f;
	m_span_base.m_span_dg = dgdx & ~0x1f;
	m_span_base.m_span_db = dbdx & ~0x1f;
	m_span_base.m_span_da = dadx & ~0x1f;
	m_span_base.m_span_ds = dsdx;
	m_span_base.m_span_dt = dtdx;
	m_span_base.m_span_dw = dwdx;
	m_span_base.m_span_dz = m_other_modes.z_source_sel ? 0 : dzdx;
	m_span_base.m_span_dymax = 0;
	m_span_base.m_span_dzpix = m_dzpix_normalize[temp_dzpix & 0xffff];

	int32_t xleft_inc = (dxmdy >> 2) & ~1;
	int32_t xright_inc = (dxhdy >> 2) & ~1;

	int32_t xright = xh & ~1;
	int32_t xleft = xm & ~1;

	const bool sign_dxhdy = (dxhdy & 0x80000000) != 0;
	const bool do_offset = !(sign_dxhdy ^ (flip));

	if (do_offset) {
		dsdeh = dsde >> 9;  dsdyh = dsdy >> 9;
		dtdeh = dtde >> 9;  dtdyh = dtdy >> 9;
		dwdeh = dwde >> 9;  dwdyh = dwdy >> 9;
		drdeh = drde >> 9;  drdyh = drdy >> 9;
		dgdeh = dgde >> 9;  dgdyh = dgdy >> 9;
		dbdeh = dbde >> 9;  dbdyh = dbdy >> 9;
		dadeh = dade >> 9;  dadyh = dady >> 9;
		dzdeh = dzde >> 9;  dzdyh = dzdy >> 9;

		dsdiff = (dsdeh << 8) + (dsdeh << 7) - (dsdyh << 8) - (dsdyh << 7);
		dtdiff = (dtdeh << 8) + (dtdeh << 7) - (dtdyh << 8) - (dtdyh << 7);
		dwdiff = (dwdeh << 8) + (dwdeh << 7) - (dwdyh << 8) - (dwdyh << 7);
		drdiff = (drdeh << 8) + (drdeh << 7) - (drdyh << 8) - (drdyh << 7);
		dgdiff = (dgdeh << 8) + (dgdeh << 7) - (dgdyh << 8) - (dgdyh << 7);
		dbdiff = (dbdeh << 8) + (dbdeh << 7) - (dbdyh << 8) - (dbdyh << 7);
		dadiff = (dadeh << 8) + (dadeh << 7) - (dadyh << 8) - (dadyh << 7);
		dzdiff = (dzdeh << 8) + (dzdeh << 7) - (dzdyh << 8) - (dzdyh << 7);
	} else {
		dsdiff = dtdiff = dwdiff = drdiff = dgdiff = dbdiff = dadiff = dzdiff = 0;
	}

	dsdxh = dsdx >> 8;
	dtdxh = dtdx >> 8;
	dwdxh = dwdx >> 8;
	drdxh = drdx >> 8;
	dgdxh = dgdx >> 8;
	dbdxh = dbdx >> 8;
	dadxh = dadx >> 8;
	dzdxh = dzdx >> 8;

	const int32_t ycur = yh & ~3;
	const int32_t ylfar = yl | 3;
	const int32_t ldflag = (sign_dxhdy ^ flip) ? 0 : 3;
	int32_t majorx[4];
	int32_t minorx[4];
	int32_t majorxint[4];
	int32_t minorxint[4];

	int32_t xfrac = ((xright >> 8) & 0xff);

	const int32_t clipy1 = m_scissor.m_yh;
	const int32_t clipy2 = m_scissor.m_yl;

	// Trivial reject
	if ((ycur >> 2) >= clipy2 && (ylfar >> 2) >= clipy2) {
		return;
	}
	if ((ycur >> 2) < clipy1 && (ylfar >> 2) < clipy1) {
		return;
	}

	int32_t* minx = flip ? &minxhx : &minxmx;
	int32_t* maxx = flip ? &maxxmx : &maxxhx;
	int32_t* startx = flip ? maxx : minx;
	int32_t* endx = flip ? minx : maxx;

	for (int32_t k = ycur; k <= ylfar; k++) {
		if (k == ym) {
			xleft = xl & ~1;
			xleft_inc = (dxldy >> 2) & ~1;
		}

		const int32_t xstart = xleft >> 16;
		const int32_t xend = xright >> 16;
		const int32_t j = k >> 2;
		const int32_t spanidx = k >> 2;
		const int32_t  spix = k & 3;
		bool valid_y = !(k < yh || k >= yl);

		majorxint[spix] = xend;
		minorxint[spix] = xstart;
		majorx[spix] = xright;
		minorx[spix] = xleft;

		if (spix == 0) {
			*maxx = 0;
			*minx = 0xfff;
		}

		if (valid_y) {
			if (flip) {
				*maxx = std::max(xstart, *maxx);
				*minx = std::min(xend, *minx);
			} else {
				*minx = std::min(xstart, *minx);
				*maxx = std::max(xend, *maxx);
			}
		}

		if (spix == 0) {
			// Setup blender data for this scanline
			set_blender_input(0, 0, &m_color_inputs.blender1a_rgb[0], &m_color_inputs.blender1b_a[0], m_other_modes.blend_m1a_0, m_other_modes.blend_m1b_0);
			set_blender_input(0, 1, &m_color_inputs.blender2a_rgb[0], &m_color_inputs.blender2b_a[0], m_other_modes.blend_m2a_0, m_other_modes.blend_m2b_0);
			set_blender_input(1, 0, &m_color_inputs.blender1a_rgb[1], &m_color_inputs.blender1b_a[1], m_other_modes.blend_m1a_1, m_other_modes.blend_m1b_1);
			set_blender_input(1, 1, &m_color_inputs.blender2a_rgb[1], &m_color_inputs.blender2b_a[1], m_other_modes.blend_m2a_1, m_other_modes.blend_m2b_1);

			// Setup color combiner data for this scanline
			set_suba_input_rgb(&m_color_inputs.combiner_rgbsub_a[0], m_combine.sub_a_rgb0);
			set_subb_input_rgb(&m_color_inputs.combiner_rgbsub_b[0], m_combine.sub_b_rgb0);
			set_mul_input_rgb(&m_color_inputs.combiner_rgbmul[0], m_combine.mul_rgb0);
			set_add_input_rgb(&m_color_inputs.combiner_rgbadd[0], m_combine.add_rgb0);
			set_sub_input_alpha(&m_color_inputs.combiner_alphasub_a[0], m_combine.sub_a_a0);
			set_sub_input_alpha(&m_color_inputs.combiner_alphasub_b[0], m_combine.sub_b_a0);
			set_mul_input_alpha(&m_color_inputs.combiner_alphamul[0], m_combine.mul_a0);
			set_sub_input_alpha(&m_color_inputs.combiner_alphaadd[0], m_combine.add_a0);

			set_suba_input_rgb(&m_color_inputs.combiner_rgbsub_a[1], m_combine.sub_a_rgb1);
			set_subb_input_rgb(&m_color_inputs.combiner_rgbsub_b[1], m_combine.sub_b_rgb1);
			set_mul_input_rgb(&m_color_inputs.combiner_rgbmul[1], m_combine.mul_rgb1);
			set_add_input_rgb(&m_color_inputs.combiner_rgbadd[1], m_combine.add_rgb1);
			set_sub_input_alpha(&m_color_inputs.combiner_alphasub_a[1], m_combine.sub_a_a1);
			set_sub_input_alpha(&m_color_inputs.combiner_alphasub_b[1], m_combine.sub_b_a1);
			set_mul_input_alpha(&m_color_inputs.combiner_alphamul[1], m_combine.mul_a1);
			set_sub_input_alpha(&m_color_inputs.combiner_alphaadd[1], m_combine.add_a1);
		}

		if (spix == 3) {
			m_xstart = *startx;
			m_xstop = *endx;
			((this)->*(m_compute_cvg[flip]))(majorx, minorx, majorxint, minorxint, j, yh, yl, ycur >> 2);
		}

		if (spix == ldflag) {
			m_unscissored_rx = xend;
			xfrac = ((xright >> 8) & 0xff);
			m_rstart = ((r >> 9) << 9) + drdiff - (xfrac * drdxh);
			m_gstart = ((g >> 9) << 9) + dgdiff - (xfrac * dgdxh);
			m_bstart = ((b >> 9) << 9) + dbdiff - (xfrac * dbdxh);
			m_astart = ((a >> 9) << 9) + dadiff - (xfrac * dadxh);
			m_sstart = (((s >> 9) << 9) + dsdiff - (xfrac * dsdxh)) & ~0x1f;
			m_tstart = (((t >> 9) << 9) + dtdiff - (xfrac * dtdxh)) & ~0x1f;
			m_wstart = (((w >> 9) << 9) + dwdiff - (xfrac * dwdxh)) & ~0x1f;
			m_zstart = ((z >> 9) << 9) + dzdiff - (xfrac * dzdxh);
		}

		if (spix == 3) {
			if (spanidx >= m_scissor.m_yh && spanidx < m_scissor.m_yl) {
				switch (m_other_modes.cycle_type) {
				case CYCLE_TYPE_1:
					span_draw_1cycle(spanidx, flip, tilenum);
					break;

				case CYCLE_TYPE_2:
					span_draw_2cycle(spanidx, flip, tilenum);
					break;

				case CYCLE_TYPE_COPY:
					span_draw_copy(spanidx, flip, tilenum);
					break;

				case CYCLE_TYPE_FILL:
					span_draw_fill(spanidx, flip, tilenum);
					break;
				}
			}
			r += drde;
			g += dgde;
			b += dbde;
			a += dade;
			s += dsde;
			t += dtde;
			w += dwde;
			z += dzde;
		}
		xleft += xleft_inc;
		xright += xright_inc;
	}
}

/*****************************************************************************/

////////////////////////
// RDP COMMANDS
////////////////////////

void n64_rdp::cmd_triangle(uint64_t w1) {
	draw_triangle(false, false, false, false);
}

void n64_rdp::cmd_triangle_z(uint64_t w1) {
	draw_triangle(false, false, true, false);
}

void n64_rdp::cmd_triangle_t(uint64_t w1) {
	draw_triangle(false, true, false, false);
}

void n64_rdp::cmd_triangle_tz(uint64_t w1) {
	draw_triangle(false, true, true, false);
}

void n64_rdp::cmd_triangle_s(uint64_t w1) {
	draw_triangle(true, false, false, false);
}

void n64_rdp::cmd_triangle_sz(uint64_t w1) {
	draw_triangle(true, false, true, false);
}

void n64_rdp::cmd_triangle_st(uint64_t w1) {
	draw_triangle(true, true, false, false);
}

void n64_rdp::cmd_triangle_stz(uint64_t w1) {
	draw_triangle(true, true, true, false);
}

void n64_rdp::cmd_tex_rect(uint64_t w1) {
	const uint64_t* data = m_cmd_data + m_cmd_cur;

	const uint64_t w2 = data[1];

	const uint64_t tilenum = (w1 >> 24) & 0x7;
	const uint64_t xh = (w1 >> 12) & 0xfff;
	const uint64_t xl = (w1 >> 44) & 0xfff;
	const uint64_t yh = (w1 >> 0) & 0xfff;
	uint64_t yl = (w1 >> 32) & 0xfff;

	const uint64_t s = (w2 >> 48) & 0xffff;
	const uint64_t t = (w2 >> 32) & 0xffff;
	const uint64_t dsdx = SIGN16((w2 >> 16) & 0xffff);
	const uint64_t dtdy = SIGN16((w2 >> 0) & 0xffff);

	if (m_other_modes.cycle_type == CYCLE_TYPE_FILL || m_other_modes.cycle_type == CYCLE_TYPE_COPY) {
		yl |= 3;
	}

	const uint64_t xlint = (xl >> 2) & 0x3ff;
	const uint64_t xhint = (xh >> 2) & 0x3ff;

	uint64_t* ewdata = m_temp_rect_data;
	ewdata[0] = ((uint64_t)0x24 << 56) | ((0x80L | tilenum) << 48) | (yl << 32) | (yl << 16) | yh;   // command, flipped, tile, yl
	ewdata[1] = (xlint << 48) | ((xl & 3) << 46);               // xl, xl frac, dxldy (0), dxldy frac (0)
	ewdata[2] = (xhint << 48) | ((xh & 3) << 46);               // xh, xh frac, dxhdy (0), dxhdy frac (0)
	ewdata[3] = (xlint << 48) | ((xl & 3) << 46);               // xm, xm frac, dxmdy (0), dxmdy frac (0)
	memset(&ewdata[4], 0, 8 * sizeof(uint64_t));                // shade
	ewdata[12] = (s << 48) | (t << 32);                         // s, t, w (0)
	ewdata[13] = (dsdx >> 5) << 48;                             // dsdx, dtdx, dwdx (0)
	ewdata[14] = 0;                                             // s frac (0), t frac (0), w frac (0)
	ewdata[15] = (dsdx & 0x1f) << 59;                           // dsdx frac, dtdx frac, dwdx frac (0)
	ewdata[16] = ((dtdy >> 5) & 0xffff) << 32;                  // dsde, dtde, dwde (0)
	ewdata[17] = ((dtdy >> 5) & 0xffff) << 32;                  // dsdy, dtdy, dwdy (0)
	ewdata[18] = ((dtdy & 0x1f) << 11) << 32;                   // dsde frac, dtde frac, dwde frac (0)
	ewdata[38] = ((dtdy & 0x1f) << 11) << 32;                   // dsdy frac, dtdy frac, dwdy frac (0)
																// ewdata[40-43] = 0;                                       // depth

	draw_triangle(true, true, false, true);
}

void n64_rdp::cmd_tex_rect_flip(uint64_t w1) {
	const uint64_t* data = m_cmd_data + m_cmd_cur;

	const uint64_t w2 = data[1];

	const uint64_t tilenum = (w1 >> 56) & 0x7;
	const uint64_t xh = (w1 >> 12) & 0xfff;
	const uint64_t xl = (w1 >> 44) & 0xfff;
	const uint64_t yh = (w1 >> 0) & 0xfff;
	uint64_t yl = (w1 >> 32) & 0xfff;

	const uint64_t s = (w2 >> 48) & 0xffff;
	const uint64_t t = (w2 >> 32) & 0xffff;
	const uint64_t dsdx = SIGN16((w2 >> 16) & 0xffff);
	const uint64_t dtdy = SIGN16((w2 >> 0) & 0xffff);

	if (m_other_modes.cycle_type == CYCLE_TYPE_FILL || m_other_modes.cycle_type == CYCLE_TYPE_COPY) {
		yl |= 3;
	}

	const uint64_t xlint = (xl >> 2) & 0x3ff;
	const uint64_t xhint = (xh >> 2) & 0x3ff;

	uint64_t* ewdata = m_temp_rect_data;
	ewdata[0] = ((uint64_t)0x25 << 56) | ((0x80L | tilenum) << 48) | (yl << 32) | (yl << 16) | yh;   // command, flipped, tile, yl
	ewdata[1] = (xlint << 48) | ((xl & 3) << 46);               // xl, xl frac, dxldy (0), dxldy frac (0)
	ewdata[2] = (xhint << 48) | ((xh & 3) << 46);               // xh, xh frac, dxhdy (0), dxhdy frac (0)
	ewdata[3] = (xlint << 48) | ((xl & 3) << 46);               // xm, xm frac, dxmdy (0), dxmdy frac (0)
	memset(&ewdata[4], 0, 8 * sizeof(uint64_t));                // shade
	ewdata[12] = (s << 48) | (t << 32);                         // s, t, w (0)
	ewdata[13] = ((dtdy >> 5) & 0xffff) << 32;                  // dsdx, dtdx, dwdx (0)
	ewdata[14] = 0;                                             // s frac (0), t frac (0), w frac (0)
	ewdata[15] = ((dtdy & 0x1f) << 43);                         // dsdx frac, dtdx frac, dwdx frac (0)
	ewdata[16] = (dsdx >> 5) << 48;                             // dsde, dtde, dwde (0)
	ewdata[17] = (dsdx >> 5) << 48;                             // dsdy, dtdy, dwdy (0)
	ewdata[18] = (dsdx & 0x1f) << 59;                           // dsde frac, dtde frac, dwde frac (0)
	ewdata[19] = (dsdx & 0x1f) << 59;                           // dsdy frac, dtdy frac, dwdy frac (0)

	draw_triangle(true, true, false, true);
}

void n64_rdp::cmd_sync_load(uint64_t w1) {
	//wait("SyncLoad");
}

void n64_rdp::cmd_sync_pipe(uint64_t w1) {
	//wait("SyncPipe");
}

void n64_rdp::cmd_sync_tile(uint64_t w1) {
	//wait("SyncTile");
}

void n64_rdp::cmd_sync_full(uint64_t w1) {
	//wait("SyncFull");
}

void n64_rdp::cmd_set_key_gb(uint64_t w1) {
	m_key_scale.set_b(uint32_t(w1 >> 0) & 0xff);
	m_key_scale.set_g(uint32_t(w1 >> 16) & 0xff);
}

void n64_rdp::cmd_set_key_r(uint64_t w1) {
	m_key_scale.set_r(uint32_t(w1 & 0xff));
}

void n64_rdp::cmd_set_fill_color32(uint64_t w1) {
	//wait("SetFillColor");
	m_fill_color = (uint32_t)w1;
}

void n64_rdp::cmd_set_convert(uint64_t w1) {
	int32_t k0 = int32_t(w1 >> 45) & 0x1ff;
	int32_t k1 = int32_t(w1 >> 36) & 0x1ff;
	int32_t k2 = int32_t(w1 >> 27) & 0x1ff;
	int32_t k3 = int32_t(w1 >> 18) & 0x1ff;
	int32_t k4 = int32_t(w1 >> 9) & 0x1ff;
	int32_t k5 = int32_t(w1 >> 0) & 0x1ff;

	k0 = (SIGN9(k0) << 1) + 1;
	k1 = (SIGN9(k1) << 1) + 1;
	k2 = (SIGN9(k2) << 1) + 1;
	k3 = (SIGN9(k3) << 1) + 1;

	set_yuv_factors(rgbaint_t(0, k0, k2, k3), rgbaint_t(0, 0, k1, 0), rgbaint_t(k4, k4, k4, k4), rgbaint_t(k5, k5, k5, k5));
}

void n64_rdp::cmd_set_scissor(uint64_t w1) {
	m_scissor.m_xh = ((w1 >> 44) & 0xfff) >> 2;
	m_scissor.m_yh = ((w1 >> 32) & 0xfff) >> 2;
	m_scissor.m_xl = ((w1 >> 12) & 0xfff) >> 2;
	m_scissor.m_yl = ((w1 >> 0) & 0xfff) >> 2;

	// TODO: handle f & o?
}

void n64_rdp::cmd_set_prim_depth(uint64_t w1) {
	m_misc_state.m_primitive_z = (uint32_t)(w1 & 0x7fff0000);
	m_misc_state.m_primitive_dz = (uint16_t)(w1 >> 32);
}

void n64_rdp::cmd_set_other_modes(uint64_t w1) {
	//wait("SetOtherModes");
	m_other_modes.cycle_type = (w1 >> 52) & 0x3; // 01
	m_other_modes.persp_tex_en = (w1 >> 51) & 1; // 1
	m_other_modes.detail_tex_en = (w1 >> 50) & 1; // 0
	m_other_modes.sharpen_tex_en = (w1 >> 49) & 1; // 0
	m_other_modes.tex_lod_en = (w1 >> 48) & 1; // 0
	m_other_modes.en_tlut = (w1 >> 47) & 1; // 0
	m_other_modes.tlut_type = (w1 >> 46) & 1; // 0
	m_other_modes.sample_type = (w1 >> 45) & 1; // 1
	m_other_modes.mid_texel = (w1 >> 44) & 1; // 0
	m_other_modes.bi_lerp0 = (w1 >> 43) & 1; // 1
	m_other_modes.bi_lerp1 = (w1 >> 42) & 1; // 1
	m_other_modes.convert_one = (w1 >> 41) & 1; // 0
	m_other_modes.key_en = (w1 >> 40) & 1; // 0
	m_other_modes.rgb_dither_sel = (w1 >> 38) & 0x3; // 00
	m_other_modes.alpha_dither_sel = (w1 >> 36) & 0x3; // 01
	m_other_modes.blend_m1a_0 = (w1 >> 30) & 0x3; // 11
	m_other_modes.blend_m1a_1 = (w1 >> 28) & 0x3; // 00
	m_other_modes.blend_m1b_0 = (w1 >> 26) & 0x3; // 10
	m_other_modes.blend_m1b_1 = (w1 >> 24) & 0x3; // 00
	m_other_modes.blend_m2a_0 = (w1 >> 22) & 0x3; // 00
	m_other_modes.blend_m2a_1 = (w1 >> 20) & 0x3; // 01
	m_other_modes.blend_m2b_0 = (w1 >> 18) & 0x3; // 00
	m_other_modes.blend_m2b_1 = (w1 >> 16) & 0x3; // 01
	m_other_modes.force_blend = (w1 >> 14) & 1; // 0
	m_other_modes.blend_shift = m_other_modes.force_blend ? 5 : 2;
	m_other_modes.alpha_cvg_select = (w1 >> 13) & 1; // 1
	m_other_modes.cvg_times_alpha = (w1 >> 12) & 1; // 0
	m_other_modes.z_mode = (w1 >> 10) & 0x3; // 00
	m_other_modes.cvg_dest = (w1 >> 8) & 0x3; // 00
	m_other_modes.color_on_cvg = (w1 >> 7) & 1; // 0
	m_other_modes.image_read_en = (w1 >> 6) & 1; // 1
	m_other_modes.z_update_en = (w1 >> 5) & 1; // 1
	m_other_modes.z_compare_en = (w1 >> 4) & 1; // 1
	m_other_modes.antialias_en = (w1 >> 3) & 1; // 1
	m_other_modes.z_source_sel = (w1 >> 2) & 1; // 0
	m_other_modes.dither_alpha_en = (w1 >> 1) & 1; // 0
	m_other_modes.alpha_compare_en = (w1 >> 0) & 1; // 0
	m_other_modes.alpha_dither_mode = (m_other_modes.alpha_compare_en << 1) | m_other_modes.dither_alpha_en;
}

void n64_rdp::cmd_load_tlut(uint64_t w1) {
	//wait("LoadTLUT");
	n64_tile_t* tile = m_tiles;

	const int32_t tilenum = (w1 >> 24) & 0x7;
	const int32_t sl = tile[tilenum].sl = int32_t(w1 >> 44) & 0xfff;
	const int32_t tl = tile[tilenum].tl = int32_t(w1 >> 32) & 0xfff;
	const int32_t sh = tile[tilenum].sh = int32_t(w1 >> 12) & 0xfff;
	const int32_t th = tile[tilenum].th = int32_t(w1 >> 0) & 0xfff;

	if (tl != th) {
		printf("Load tlut: tl=%d, th=%d\n", tl, th);
	}

	m_capture->data_begin();

	const int32_t count = ((sh >> 2) - (sl >> 2) + 1) << 2;

	switch (m_misc_state.m_ti_size) {
	case PIXEL_SIZE_16BIT:
	{
		if (tile[tilenum].tmem < 256) {
			printf("rdp_load_tlut: loading tlut into low half at %d qwords\n", tile[tilenum].tmem);
		}
		int32_t srcstart = (m_misc_state.m_ti_address + (tl >> 2) * (m_misc_state.m_ti_width << 1) + (sl >> 1)) >> 1;
		int32_t dststart = tile[tilenum].tmem << 2;
		uint16_t* dst = get_tmem16();

		for (int32_t i = 0; i < count; i += 4) {
			if (dststart < 2048) {
				dst[dststart] = m_capture->data_block()->get16();
				dst[dststart + 1] = dst[dststart];
				dst[dststart + 2] = dst[dststart];
				dst[dststart + 3] = dst[dststart];
				dststart += 4;
				srcstart += 1;
			}
		}
		break;
	}
	default:
		printf("RDP: load_tlut: size = %d\n", m_misc_state.m_ti_size);
		break;
	}

	m_capture->data_end();
	m_capture->data_block()->reset();

	m_tiles[tilenum].sth = rgbaint_t(m_tiles[tilenum].sh, m_tiles[tilenum].sh, m_tiles[tilenum].th, m_tiles[tilenum].th);
	m_tiles[tilenum].stl = rgbaint_t(m_tiles[tilenum].sl, m_tiles[tilenum].sl, m_tiles[tilenum].tl, m_tiles[tilenum].tl);
}

void n64_rdp::cmd_set_tile_size(uint64_t w1) {
	//wait("SetTileSize");

	const int32_t tilenum = int32_t(w1 >> 24) & 0x7;

	m_tiles[tilenum].sl = int32_t(w1 >> 44) & 0xfff;
	m_tiles[tilenum].tl = int32_t(w1 >> 32) & 0xfff;
	m_tiles[tilenum].sh = int32_t(w1 >> 12) & 0xfff;
	m_tiles[tilenum].th = int32_t(w1 >> 0) & 0xfff;

	m_tiles[tilenum].sth = rgbaint_t(m_tiles[tilenum].sh, m_tiles[tilenum].sh, m_tiles[tilenum].th, m_tiles[tilenum].th);
	m_tiles[tilenum].stl = rgbaint_t(m_tiles[tilenum].sl, m_tiles[tilenum].sl, m_tiles[tilenum].tl, m_tiles[tilenum].tl);
}

void n64_rdp::cmd_load_block(uint64_t w1) {
	//wait("LoadBlock");
	n64_tile_t* tile = m_tiles;

	const int32_t tilenum = int32_t(w1 >> 24) & 0x7;
	uint16_t* tc = get_tmem16();

	int32_t sl = tile[tilenum].sl = int32_t(w1 >> 44) & 0xfff;
	int32_t tl = tile[tilenum].tl = int32_t(w1 >> 32) & 0xfff;
	int32_t sh = tile[tilenum].sh = int32_t(w1 >> 12) & 0xfff;
	const int32_t dxt = int32_t(w1 >> 0) & 0xfff;

	if (sh < sl) {
		printf("load_block: sh < sl\n");
	}

	int32_t width = (sh - sl) + 1;

	width = (width << m_misc_state.m_ti_size) >> 1;
	if (width & 7) {
		width = (width & ~7) + 8;
	}
	width >>= 3;

	const int32_t tb = tile[tilenum].tmem << 2;

	const int32_t tiwinwords = (m_misc_state.m_ti_width << m_misc_state.m_ti_size) >> 2;
	const int32_t slinwords = (sl << m_misc_state.m_ti_size) >> 2;

	const uint32_t src = (m_misc_state.m_ti_address >> 1) + (tl * tiwinwords) + slinwords;

	m_capture->data_begin();

	if (dxt != 0) {
		int32_t j = 0;
		int32_t t = 0;
		int32_t oldt = 0;

		if (tile[tilenum].size != PIXEL_SIZE_32BIT && tile[tilenum].format != FORMAT_YUV) {
			for (int32_t i = 0; i < width; i++) {
				oldt = t;
				t = ((j >> 11) & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
				if (t != oldt) {
					i += tile[tilenum].line;
				}

				int32_t ptr = tb + (i << 2);
				int32_t srcptr = src + (i << 2);

				tc[(ptr ^ t) & 0x7ff] = m_capture->data_block()->get16();
				tc[((ptr + 1) ^ t) & 0x7ff] = m_capture->data_block()->get16();
				tc[((ptr + 2) ^ t) & 0x7ff] = m_capture->data_block()->get16();
				tc[((ptr + 3) ^ t) & 0x7ff] = m_capture->data_block()->get16();

				j += dxt;
			}
		} else if (tile[tilenum].format == FORMAT_YUV) {
			for (int32_t i = 0; i < width; i++) {
				oldt = t;
				t = ((j >> 11) & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
				if (t != oldt) {
					i += tile[tilenum].line;
				}

				int32_t ptr = ((tb + (i << 1)) ^ t) & 0x3ff;
				int32_t srcptr = src + (i << 2);

				int32_t first = m_capture->data_block()->get16();
				int32_t sec = m_capture->data_block()->get16();
				tc[ptr] = ((first >> 8) << 8) | (sec >> 8);
				tc[ptr | 0x400] = ((first & 0xff) << 8) | (sec & 0xff);

				ptr = ((tb + (i << 1) + 1) ^ t) & 0x3ff;
				first = m_capture->data_block()->get16();
				sec = m_capture->data_block()->get16();
				tc[ptr] = ((first >> 8) << 8) | (sec >> 8);
				tc[ptr | 0x400] = ((first & 0xff) << 8) | (sec & 0xff);

				j += dxt;
			}
		} else {
			for (int32_t i = 0; i < width; i++) {
				oldt = t;
				t = ((j >> 11) & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
				if (t != oldt)
					i += tile[tilenum].line;

				int32_t ptr = ((tb + (i << 1)) ^ t) & 0x3ff;
				int32_t srcptr = src + (i << 2);
				tc[ptr] = m_capture->data_block()->get16();
				tc[ptr | 0x400] = m_capture->data_block()->get16();

				ptr = ((tb + (i << 1) + 1) ^ t) & 0x3ff;
				tc[ptr] = m_capture->data_block()->get16();
				tc[ptr | 0x400] = m_capture->data_block()->get16();

				j += dxt;
			}
		}
		tile[tilenum].th = tl + (j >> 11);
	} else {
		if (tile[tilenum].size != PIXEL_SIZE_32BIT && tile[tilenum].format != FORMAT_YUV) {
			for (int32_t i = 0; i < width; i++) {
				int32_t ptr = tb + (i << 2);
				int32_t srcptr = src + (i << 2);
				tc[(ptr ^ WORD_ADDR_XOR) & 0x7ff] = m_capture->data_block()->get16();
				tc[((ptr + 1) ^ WORD_ADDR_XOR) & 0x7ff] = m_capture->data_block()->get16();
				tc[((ptr + 2) ^ WORD_ADDR_XOR) & 0x7ff] = m_capture->data_block()->get16();
				tc[((ptr + 3) ^ WORD_ADDR_XOR) & 0x7ff] = m_capture->data_block()->get16();
			}
		} else if (tile[tilenum].format == FORMAT_YUV) {
			for (int32_t i = 0; i < width; i++) {
				int32_t ptr = ((tb + (i << 1)) ^ WORD_ADDR_XOR) & 0x3ff;
				int32_t srcptr = src + (i << 2);
				int32_t first = m_capture->data_block()->get16();
				int32_t sec = m_capture->data_block()->get16();
				tc[ptr] = ((first >> 8) << 8) | (sec >> 8);//UV pair
				tc[ptr | 0x400] = ((first & 0xff) << 8) | (sec & 0xff);

				ptr = ((tb + (i << 1) + 1) ^ WORD_ADDR_XOR) & 0x3ff;
				first = m_capture->data_block()->get16();
				sec = m_capture->data_block()->get16();
				tc[ptr] = ((first >> 8) << 8) | (sec >> 8);
				tc[ptr | 0x400] = ((first & 0xff) << 8) | (sec & 0xff);
			}
		} else {
			for (int32_t i = 0; i < width; i++) {
				int32_t ptr = ((tb + (i << 1)) ^ WORD_ADDR_XOR) & 0x3ff;
				int32_t srcptr = src + (i << 2);
				tc[ptr] = m_capture->data_block()->get16();
				tc[ptr | 0x400] = m_capture->data_block()->get16();

				ptr = ((tb + (i << 1) + 1) ^ WORD_ADDR_XOR) & 0x3ff;
				tc[ptr] = m_capture->data_block()->get16();
				tc[ptr | 0x400] = m_capture->data_block()->get16();
			}
		}
		tile[tilenum].th = tl;
	}

	m_capture->data_end();
	m_capture->data_block()->reset();

	m_tiles[tilenum].sth = rgbaint_t(m_tiles[tilenum].sh, m_tiles[tilenum].sh, m_tiles[tilenum].th, m_tiles[tilenum].th);
	m_tiles[tilenum].stl = rgbaint_t(m_tiles[tilenum].sl, m_tiles[tilenum].sl, m_tiles[tilenum].tl, m_tiles[tilenum].tl);
}

void n64_rdp::cmd_load_tile(uint64_t w1) {
	//wait("LoadTile");
	n64_tile_t* tile = m_tiles;
	const int32_t tilenum = int32_t(w1 >> 24) & 0x7;

	tile[tilenum].sl = int32_t(w1 >> 44) & 0xfff;
	tile[tilenum].tl = int32_t(w1 >> 32) & 0xfff;
	tile[tilenum].sh = int32_t(w1 >> 12) & 0xfff;
	tile[tilenum].th = int32_t(w1 >> 0) & 0xfff;

	const int32_t sl = tile[tilenum].sl >> 2;
	const int32_t tl = tile[tilenum].tl >> 2;
	const int32_t sh = tile[tilenum].sh >> 2;
	const int32_t th = tile[tilenum].th >> 2;

	const int32_t width = (sh - sl) + 1;
	const int32_t height = (th - tl) + 1;
	/*
	int32_t topad;
	if (m_misc_state.m_ti_size < 3)
	{
	topad = (width * m_misc_state.m_ti_size) & 0x7;
	}
	else
	{
	topad = (width << 2) & 0x7;
	}
	topad = 0; // ????
	*/

	m_capture->data_begin();

	switch (m_misc_state.m_ti_size) {
	case PIXEL_SIZE_8BIT:
	{
		const uint32_t src = m_misc_state.m_ti_address;
		const int32_t tb = tile[tilenum].tmem << 3;
		uint8_t* tc = get_tmem8();

		for (int32_t j = 0; j < height; j++) {
			const int32_t tline = tb + ((tile[tilenum].line << 3) * j);
			const int32_t s = ((j + tl) * m_misc_state.m_ti_width) + sl;
			const int32_t xorval8 = ((j & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR);

			for (int32_t i = 0; i < width; i++) {
				const uint8_t data = m_capture->data_block()->get8();
				tc[((tline + i) ^ xorval8) & 0xfff] = data;
			}
		}
		break;
	}
	case PIXEL_SIZE_16BIT:
	{
		const uint32_t src = m_misc_state.m_ti_address >> 1;
		uint16_t* tc = get_tmem16();

		if (tile[tilenum].format != FORMAT_YUV) {
			for (int32_t j = 0; j < height; j++) {
				const int32_t tb = tile[tilenum].tmem << 2;
				const int32_t tline = tb + ((tile[tilenum].line << 2) * j);
				const int32_t s = ((j + tl) * m_misc_state.m_ti_width) + sl;
				const int32_t xorval16 = (j & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;

				for (int32_t i = 0; i < width; i++) {
					const uint32_t taddr = (tline + i) ^ xorval16;
					const uint16_t data = m_capture->data_block()->get16();
					tc[taddr & 0x7ff] = data;
				}
			}
		} else {
			for (int32_t j = 0; j < height; j++) {
				const int32_t tb = tile[tilenum].tmem << 3;
				const int32_t tline = tb + ((tile[tilenum].line << 3) * j);
				const int32_t s = ((j + tl) * m_misc_state.m_ti_width) + sl;
				const int32_t xorval8 = (j & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;

				for (int32_t i = 0; i < width; i++) {
					uint32_t taddr = ((tline + i) ^ xorval8) & 0x7ff;
					uint16_t yuvword = m_capture->data_block()->get16();
					get_tmem8()[taddr] = yuvword >> 8;
					get_tmem8()[taddr | 0x800] = yuvword & 0xff;
				}
			}
		}
		break;
	}
	case PIXEL_SIZE_32BIT:
	{
		const uint32_t src = m_misc_state.m_ti_address >> 2;
		const int32_t tb = (tile[tilenum].tmem << 2);
		uint16_t* tc16 = get_tmem16();

		for (int32_t j = 0; j < height; j++) {
			const int32_t tline = tb + ((tile[tilenum].line << 2) * j);

			const int32_t s = ((j + tl) * m_misc_state.m_ti_width) + sl;
			const int32_t xorval32cur = (j & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
			for (int32_t i = 0; i < width; i++) {
				uint32_t c = m_capture->data_block()->get32();
				uint32_t ptr = ((tline + i) ^ xorval32cur) & 0x3ff;
				tc16[ptr] = c >> 16;
				tc16[ptr | 0x400] = c & 0xffff;
			}
		}
		break;
	}

	default:
		printf("RDP: load_tile: size = %d\n", m_misc_state.m_ti_size);
	}

	m_capture->data_end();
	m_capture->data_block()->reset();

	m_tiles[tilenum].sth = rgbaint_t(m_tiles[tilenum].sh, m_tiles[tilenum].sh, m_tiles[tilenum].th, m_tiles[tilenum].th);
	m_tiles[tilenum].stl = rgbaint_t(m_tiles[tilenum].sl, m_tiles[tilenum].sl, m_tiles[tilenum].tl, m_tiles[tilenum].tl);
}

void n64_rdp::cmd_set_tile(uint64_t w1) {
	//wait("SetTile");
	const int32_t tilenum = int32_t(w1 >> 24) & 0x7;
	n64_tile_t* tex_tile = &m_tiles[tilenum];

	tex_tile->format = int32_t(w1 >> 53) & 0x7;
	tex_tile->size = int32_t(w1 >> 51) & 0x3;
	tex_tile->line = int32_t(w1 >> 41) & 0x1ff;
	tex_tile->tmem = int32_t(w1 >> 32) & 0x1ff;
	tex_tile->palette = int32_t(w1 >> 20) & 0xf;
	tex_tile->ct = int32_t(w1 >> 19) & 0x1;
	tex_tile->mt = int32_t(w1 >> 18) & 0x1;
	tex_tile->mask_t = int32_t(w1 >> 14) & 0xf;
	tex_tile->shift_t = int32_t(w1 >> 10) & 0xf;
	tex_tile->cs = int32_t(w1 >> 9) & 0x1;
	tex_tile->ms = int32_t(w1 >> 8) & 0x1;
	tex_tile->mask_s = int32_t(w1 >> 4) & 0xf;
	tex_tile->shift_s = int32_t(w1 >> 0) & 0xf;

	tex_tile->lshift_s = (tex_tile->shift_s >= 11) ? (16 - tex_tile->shift_s) : 0;
	tex_tile->rshift_s = (tex_tile->shift_s < 11) ? tex_tile->shift_s : 0;
	tex_tile->lshift_t = (tex_tile->shift_t >= 11) ? (16 - tex_tile->shift_t) : 0;
	tex_tile->rshift_t = (tex_tile->shift_t < 11) ? tex_tile->shift_t : 0;
	tex_tile->wrapped_mask_s = (tex_tile->mask_s > 10 ? 10 : tex_tile->mask_s);
	tex_tile->wrapped_mask_t = (tex_tile->mask_t > 10 ? 10 : tex_tile->mask_t);
	tex_tile->wrapped_mask = rgbaint_t(tex_tile->wrapped_mask_s, tex_tile->wrapped_mask_s, tex_tile->wrapped_mask_t, tex_tile->wrapped_mask_t);
	tex_tile->clamp_s = tex_tile->cs || !tex_tile->mask_s;
	tex_tile->clamp_t = tex_tile->ct || !tex_tile->mask_t;
	tex_tile->mm = rgbaint_t(tex_tile->ms ? ~0 : 0, tex_tile->ms ? ~0 : 0, tex_tile->mt ? ~0 : 0, tex_tile->mt ? ~0 : 0);
	tex_tile->invmm = rgbaint_t(tex_tile->ms ? 0 : ~0, tex_tile->ms ? 0 : ~0, tex_tile->mt ? 0 : ~0, tex_tile->mt ? 0 : ~0);
	tex_tile->mask = rgbaint_t(tex_tile->mask_s ? ~0 : 0, tex_tile->mask_s ? ~0 : 0, tex_tile->mask_t ? ~0 : 0, tex_tile->mask_t ? ~0 : 0);
	tex_tile->invmask = rgbaint_t(tex_tile->mask_s ? 0 : ~0, tex_tile->mask_s ? 0 : ~0, tex_tile->mask_t ? 0 : ~0, tex_tile->mask_t ? 0 : ~0);
	tex_tile->lshift = rgbaint_t(tex_tile->lshift_s, tex_tile->lshift_s, tex_tile->lshift_t, tex_tile->lshift_t);
	tex_tile->rshift = rgbaint_t(tex_tile->rshift_s, tex_tile->rshift_s, tex_tile->rshift_t, tex_tile->rshift_t);
	tex_tile->clamp_st = rgbaint_t(tex_tile->clamp_s ? ~0 : 0, tex_tile->clamp_s ? ~0 : 0, tex_tile->clamp_t ? ~0 : 0, tex_tile->clamp_t ? ~0 : 0);

	if (tex_tile->format == FORMAT_I && tex_tile->size > PIXEL_SIZE_8BIT) {
		tex_tile->format = FORMAT_RGBA; // Used by Supercross 2000 (in-game)
	}
	if (tex_tile->format == FORMAT_CI && tex_tile->size > PIXEL_SIZE_8BIT) {
		tex_tile->format = FORMAT_RGBA; // Used by Clay Fighter - Sculptor's Cut
	}

	if (tex_tile->format == FORMAT_RGBA && tex_tile->size < PIXEL_SIZE_16BIT) {
		tex_tile->format = FORMAT_CI; // Used by Exterem-G2, Madden Football 64, and Rat Attack
	}

	//m_pending_mode_block = true;
}

void n64_rdp::cmd_fill_rect(uint64_t w1) {
	//if(m_pending_mode_block) { wait("Block on pending mode-change"); m_pending_mode_block = false; }
	const uint64_t xh = (w1 >> 12) & 0xfff;
	const uint64_t xl = (w1 >> 44) & 0xfff;
	const uint64_t yh = (w1 >> 0) & 0xfff;
	uint64_t yl = (w1 >> 32) & 0xfff;

	if (m_other_modes.cycle_type == CYCLE_TYPE_FILL || m_other_modes.cycle_type == CYCLE_TYPE_COPY) {
		yl |= 3;
	}

	const uint64_t xlint = (xl >> 2) & 0x3ff;
	const uint64_t xhint = (xh >> 2) & 0x3ff;

	uint64_t* ewdata = m_temp_rect_data;
	ewdata[0] = ((uint64_t)0x3680 << 48) | (yl << 32) | (yl << 16) | yh; // command, flipped, tile, yl, ym, yh
	ewdata[1] = (xlint << 48) | ((xl & 3) << 46); // xl, xl frac, dxldy (0), dxldy frac (0)
	ewdata[2] = (xhint << 48) | ((xh & 3) << 46); // xh, xh frac, dxhdy (0), dxhdy frac (0)
	ewdata[3] = (xlint << 48) | ((xl & 3) << 46); // xm, xm frac, dxmdy (0), dxmdy frac (0)
	memset(&ewdata[4], 0, 18 * sizeof(uint64_t));//shade, texture, depth

	draw_triangle(false, false, false, true);
}

void n64_rdp::cmd_set_fog_color(uint64_t w1) {
	m_fog_color.set(uint8_t(w1), uint8_t(w1 >> 24), uint8_t(w1 >> 16), uint8_t(w1 >> 8));
}

void n64_rdp::cmd_set_blend_color(uint64_t w1) {
	m_blend_color.set(uint8_t(w1), uint8_t(w1 >> 24), uint8_t(w1 >> 16), uint8_t(w1 >> 8));
}

void n64_rdp::cmd_set_prim_color(uint64_t w1) {
	m_misc_state.m_min_level = int32_t(w1 >> 40) & 0x1f;
	const uint8_t prim_lod_fraction = uint8_t(w1 >> 32);
	m_prim_lod_fraction.set(prim_lod_fraction, prim_lod_fraction, prim_lod_fraction, prim_lod_fraction);

	const uint8_t alpha = uint8_t(w1);
	m_prim_color.set(alpha, uint8_t(w1 >> 24), uint8_t(w1 >> 16), uint8_t(w1 >> 8));
	m_prim_alpha.set(alpha, alpha, alpha, alpha);
}

void n64_rdp::cmd_set_env_color(uint64_t w1) {
	const uint8_t alpha = uint8_t(w1);
	m_env_color.set(alpha, uint8_t(w1 >> 24), uint8_t(w1 >> 16), uint8_t(w1 >> 8));
	m_env_alpha.set(alpha, alpha, alpha, alpha);
}

void n64_rdp::cmd_set_combine(uint64_t w1) {
	m_combine.sub_a_rgb0 = uint32_t(w1 >> 52) & 0xf;
	m_combine.mul_rgb0 = uint32_t(w1 >> 47) & 0x1f;
	m_combine.sub_a_a0 = uint32_t(w1 >> 44) & 0x7;
	m_combine.mul_a0 = uint32_t(w1 >> 41) & 0x7;
	m_combine.sub_a_rgb1 = uint32_t(w1 >> 37) & 0xf;
	m_combine.mul_rgb1 = uint32_t(w1 >> 32) & 0x1f;

	m_combine.sub_b_rgb0 = uint32_t(w1 >> 28) & 0xf;
	m_combine.sub_b_rgb1 = uint32_t(w1 >> 24) & 0xf;
	m_combine.sub_a_a1 = uint32_t(w1 >> 21) & 0x7;
	m_combine.mul_a1 = uint32_t(w1 >> 18) & 0x7;
	m_combine.add_rgb0 = uint32_t(w1 >> 15) & 0x7;
	m_combine.sub_b_a0 = uint32_t(w1 >> 12) & 0x7;
	m_combine.add_a0 = uint32_t(w1 >> 9) & 0x7;
	m_combine.add_rgb1 = uint32_t(w1 >> 6) & 0x7;
	m_combine.sub_b_a1 = uint32_t(w1 >> 3) & 0x7;
	m_combine.add_a1 = uint32_t(w1 >> 0) & 0x7;
}

void n64_rdp::cmd_set_texture_image(uint64_t w1) {
	m_misc_state.m_ti_format = uint32_t(w1 >> 53) & 0x7;
	m_misc_state.m_ti_size = uint32_t(w1 >> 51) & 0x3;
	m_misc_state.m_ti_width = (uint32_t(w1 >> 32) & 0x3ff) + 1;
	m_misc_state.m_ti_address = uint32_t(w1) & 0x01ffffff;
}

void n64_rdp::cmd_set_mask_image(uint64_t w1) {
	//wait("SetMaskImage");

	m_misc_state.m_zb_address = uint32_t(w1) & 0x01ffffff;
}

void n64_rdp::cmd_set_color_image(uint64_t w1) {
	//wait("SetColorImage");

	m_misc_state.m_fb_format = uint32_t(w1 >> 53) & 0x7;
	m_misc_state.m_fb_size = uint32_t(w1 >> 51) & 0x3;
	m_misc_state.m_fb_width = (uint32_t(w1 >> 32) & 0x3ff) + 1;
	m_misc_state.m_fb_address = uint32_t(w1) & 0x01ffffff;

	if (m_misc_state.m_fb_format < 2 || m_misc_state.m_fb_format > 32) // Jet Force Gemini sets the format to 4, Intensity.  Protection?
	{
		m_misc_state.m_fb_format = 2;
	}
}

/*****************************************************************************/

void n64_rdp::cmd_invalid(uint64_t w1) {
	printf("n64_rdp::Invalid: %d, %08x %08x\n", uint32_t(w1 >> 56) & 0x3f, uint32_t(w1 >> 32), (uint32_t)w1);
}

void n64_rdp::cmd_noop(uint64_t w1) {
	// Do nothing
}


void n64_rdp::process_command_list() {
	while (commands_available()) {
		process_command();
		m_capture->next_command();
	};
	reset();
}

void n64_rdp::reset() {
	m_cmd_ptr = 0;
	m_cmd_cur = 0;

	m_start = m_current = m_end;
}

void n64_rdp::process_command() {
	pin64_data_t* data = m_capture->command()->data();
	const uint32_t length = data->get32(0);

	m_cmd_ptr = 0;
	// load command data
	for (uint32_t i = 0; i < length; i++) {
		uint64_t val = data->get64(4 + i * 8);
		m_cmd_data[m_cmd_ptr++] = val;
	}

	uint32_t cmd = (m_cmd_data[0] >> 56) & 0x3f;
	uint32_t cmd_length = length * 8;

	// check if more data is needed
	if (cmd_length < s_rdp_command_length[cmd]) {
		return;
	}

	m_cmd_cur = 0;

	if (((m_cmd_ptr - m_cmd_cur) * 8) < s_rdp_command_length[cmd]) {
		return;
		//fatalerror("rdp_process_list: not enough rdp command data: cur = %d, ptr = %d, expected = %d\n", m_cmd_cur, m_cmd_ptr, s_rdp_command_length[cmd]);
	}

	m_capture->command(&m_cmd_data[m_cmd_cur], s_rdp_command_length[cmd] / 8);

	if (LOG_RDP_EXECUTION) {
		char string[4000];
		disassemble(string, 4000);

		fprintf(rdp_exec, "%08X: %08X%08X   %s\n", m_start + (m_cmd_cur * 8), uint32_t(m_cmd_data[m_cmd_cur] >> 32), (uint32_t)m_cmd_data[m_cmd_cur], string);
		fflush(rdp_exec);
	}

	// execute the command
	uint64_t w = m_cmd_data[m_cmd_cur];

	switch (cmd) {
	case 0x00:  cmd_noop(w);           break;

	case 0x08:  cmd_triangle(w);       break;
	case 0x09:  cmd_triangle_z(w);     break;
	case 0x0a:  cmd_triangle_t(w);     break;
	case 0x0b:  cmd_triangle_tz(w);    break;
	case 0x0c:  cmd_triangle_s(w);     break;
	case 0x0d:  cmd_triangle_sz(w);    break;
	case 0x0e:  cmd_triangle_st(w);    break;
	case 0x0f:  cmd_triangle_stz(w);   break;

	case 0x24:  cmd_tex_rect(w);       break;
	case 0x25:  cmd_tex_rect_flip(w);  break;

	case 0x26:  cmd_sync_load(w);      break;
	case 0x27:  cmd_sync_pipe(w);      break;
	case 0x28:  cmd_sync_tile(w);      break;
	case 0x29:  cmd_sync_full(w);      break;

	case 0x2a:  cmd_set_key_gb(w);     break;
	case 0x2b:  cmd_set_key_r(w);      break;

	case 0x2c:  cmd_set_convert(w);    break;
	case 0x3c:  cmd_set_combine(w);    break;
	case 0x2d:  cmd_set_scissor(w);    break;
	case 0x2e:  cmd_set_prim_depth(w); break;
	case 0x2f:  cmd_set_other_modes(w); break;

	case 0x30:  cmd_load_tlut(w);      break;
	case 0x33:  cmd_load_block(w);     break;
	case 0x34:  cmd_load_tile(w);      break;

	case 0x32:  cmd_set_tile_size(w);  break;
	case 0x35:  cmd_set_tile(w);       break;

	case 0x36:  cmd_fill_rect(w);      break;

	case 0x37:  cmd_set_fill_color32(w); break;
	case 0x38:  cmd_set_fog_color(w);  break;
	case 0x39:  cmd_set_blend_color(w); break;
	case 0x3a:  cmd_set_prim_color(w); break;
	case 0x3b:  cmd_set_env_color(w);  break;

	case 0x3d:  cmd_set_texture_image(w); break;
	case 0x3e:  cmd_set_mask_image(w);  break;
	case 0x3f:  cmd_set_color_image(w); break;
	}
}

/*****************************************************************************/

n64_rdp::n64_rdp(uint32_t* rdram) {
	ignore = false;
	dolog = false;

	m_vi_blank = false;
	m_rdram = rdram;

	m_aux_buf_ptr = 0;
	m_aux_buf = nullptr;

	m_pending_mode_block = false;

	m_cmd_ptr = 0;
	m_cmd_cur = 0;

	m_start = 0;
	m_end = 0;
	m_current = 0;
	m_status = 0x88;

	m_one.set(0xff, 0xff, 0xff, 0xff);
	m_zero.set(0, 0, 0, 0);

	m_tmem = nullptr;

	//memset(m_hidden_bits, 3, 8388608);

	m_prim_lod_fraction.set(0, 0, 0, 0);
	z_build_com_table();

	memset(m_temp_rect_data, 0, sizeof(uint32_t) * 0x1000);

	for (int32_t i = 0; i < 0x4000; i++) {
		uint32_t exponent = (i >> 11) & 7;
		uint32_t mantissa = i & 0x7ff;
		m_z_complete_dec_table[i] = ((mantissa << m_z_dec_table[exponent].shift) + m_z_dec_table[exponent].add) & 0x3fffff;
	}

	precalc_cvmask_derivatives();

	for (int32_t i = 0; i < 0x200; i++) {
		switch ((i >> 7) & 3) {
		case 0:
		case 1:
			s_special_9bit_clamptable[i] = i & 0xff;
			break;
		case 2:
			s_special_9bit_clamptable[i] = 0xff;
			break;
		case 3:
			s_special_9bit_clamptable[i] = 0;
			break;
		}
	}

	for (int32_t i = 0; i < 32; i++) {
		m_replicated_rgba[i] = (i << 3) | ((i >> 2) & 7);
	}

	for (int32_t i = 0; i < 0x10000; i++) {
		m_dzpix_normalize[i] = (uint16_t)normalize_dzpix(i & 0xffff);
	}

	m_compute_cvg[0] = &n64_rdp::compute_cvg_noflip;
	m_compute_cvg[1] = &n64_rdp::compute_cvg_flip;

	for (int32_t i = 0; i < 256; i++) {
		m_gamma_table[i] = (int32_t)sqrt((float)(i << 6));
		m_gamma_table[i] <<= 1;
	}

	for (int32_t i = 0; i < 0x4000; i++) {
		m_gamma_dither_table[i] = (int32_t)sqrt((float)i);
		m_gamma_dither_table[i] <<= 1;
	}

	m_blender.set_processor(this);
	m_tex_pipe.init(this);
}

void n64_rdp::rgbaz_clip(int32_t sr, int32_t sg, int32_t sb, int32_t sa, int32_t* sz) {
	m_shade_color.set(sa, sr, sg, sb);
	m_shade_color.clamp_and_clear(0xfffffe00);
	uint32_t a = m_shade_color.get_a();
	m_shade_alpha.set(a, a, a, a);

	int32_t zanded = (*sz) & 0x60000;

	zanded >>= 17;
	switch (zanded) {
	case 0: *sz &= 0x3ffff;                                         break;
	case 1: *sz &= 0x3ffff;                                         break;
	case 2: *sz = 0x3ffff;                                          break;
	case 3: *sz = 0x3ffff;                                          break;
	}
}

void n64_rdp::rgbaz_correct_triangle(int32_t offx, int32_t offy, int32_t* r, int32_t* g, int32_t* b, int32_t* a, int32_t* z) {
	if (m_current_pix_cvg == 8) {
		*r >>= 2;
		*g >>= 2;
		*b >>= 2;
		*a >>= 2;
		*z = (*z >> 3) & 0x7ffff;
	} else {
		int32_t summand_xr = offx * SIGN13(m_span_base.m_span_dr >> 14);
		int32_t summand_yr = offy * SIGN13(m_span_base.m_span_drdy >> 14);
		int32_t summand_xb = offx * SIGN13(m_span_base.m_span_db >> 14);
		int32_t summand_yb = offy * SIGN13(m_span_base.m_span_dbdy >> 14);
		int32_t summand_xg = offx * SIGN13(m_span_base.m_span_dg >> 14);
		int32_t summand_yg = offy * SIGN13(m_span_base.m_span_dgdy >> 14);
		int32_t summand_xa = offx * SIGN13(m_span_base.m_span_da >> 14);
		int32_t summand_ya = offy * SIGN13(m_span_base.m_span_dady >> 14);

		int32_t summand_xz = offx * SIGN22(m_span_base.m_span_dz >> 10);
		int32_t summand_yz = offy * SIGN22(m_span_base.m_span_dzdy >> 10);

		*r = ((*r << 2) + summand_xr + summand_yr) >> 4;
		*g = ((*g << 2) + summand_xg + summand_yg) >> 4;
		*b = ((*b << 2) + summand_xb + summand_yb) >> 4;
		*a = ((*a << 2) + summand_xa + summand_ya) >> 4;
		*z = (((*z << 2) + summand_xz + summand_yz) >> 5) & 0x7ffff;
	}
}

inline void n64_rdp::write_pixel(uint32_t curpixel, color_t& color) {
	if (m_misc_state.m_fb_size == 2) // 16-bit framebuffer
	{
		const uint32_t fb = (m_misc_state.m_fb_address >> 1) + curpixel;

		uint16_t finalcolor;
		if (m_other_modes.color_on_cvg && !m_pre_wrap) {
			finalcolor = RREADIDX16(fb) & 0xfffe;
		} else {
			color.shr_imm(3);
			finalcolor = (color.get_r() << 11) | (color.get_g() << 6) | (color.get_b() << 1);
		}

		switch (m_other_modes.cvg_dest) {
		case 0:
			if (m_blend_enable) {
				uint32_t finalcvg = m_current_pix_cvg + m_current_mem_cvg;
				if (finalcvg & 8) {
					finalcvg = 7;
				}
				RWRITEIDX16(fb, finalcolor | (finalcvg >> 2));
				HWRITEADDR8(fb, finalcvg & 3);
			} else {
				const uint32_t finalcvg = (m_current_pix_cvg - 1) & 7;
				RWRITEIDX16(fb, finalcolor | (finalcvg >> 2));
				HWRITEADDR8(fb, finalcvg & 3);
			}
			break;
		case 1:
		{
			const uint32_t finalcvg = (m_current_pix_cvg + m_current_mem_cvg) & 7;
			RWRITEIDX16(fb, finalcolor | (finalcvg >> 2));
			HWRITEADDR8(fb, finalcvg & 3);
			break;
		}
		case 2:
			RWRITEIDX16(fb, finalcolor | 1);
			HWRITEADDR8(fb, 3);
			break;
		case 3:
			RWRITEIDX16(fb, finalcolor | (m_current_mem_cvg >> 2));
			HWRITEADDR8(fb, m_current_mem_cvg & 3);
			break;
		}
	} else // 32-bit framebuffer
	{
		const uint32_t fb = (m_misc_state.m_fb_address >> 2) + curpixel;

		uint32_t finalcolor;
		if (m_other_modes.color_on_cvg && !m_pre_wrap) {
			finalcolor = RREADIDX32(fb) & 0xffffff00;
		} else {
			finalcolor = (color.get_r() << 24) | (color.get_g() << 16) | (color.get_b() << 8);
		}

		switch (m_other_modes.cvg_dest) {
		case 0:
			if (m_blend_enable) {
				uint32_t finalcvg = m_current_pix_cvg + m_current_mem_cvg;
				if (finalcvg & 8) {
					finalcvg = 7;
				}

				RWRITEIDX32(fb, finalcolor | (finalcvg << 5));
			} else {
				RWRITEIDX32(fb, finalcolor | (((m_current_pix_cvg - 1) & 7) << 5));
			}
			break;
		case 1:
			RWRITEIDX32(fb, finalcolor | (((m_current_pix_cvg + m_current_mem_cvg) & 7) << 5));
			break;
		case 2:
			RWRITEIDX32(fb, finalcolor | 0xE0);
			break;
		case 3:
			RWRITEIDX32(fb, finalcolor | (m_current_mem_cvg << 5));
			break;
		}
	}
}

inline void n64_rdp::read_pixel(uint32_t curpixel) {
	if (m_misc_state.m_fb_size == 2) // 16-bit framebuffer
	{
		const uint16_t fword = RREADIDX16((m_misc_state.m_fb_address >> 1) + curpixel);

		m_memory_color.set(0, GETHICOL(fword), GETMEDCOL(fword), GETLOWCOL(fword));
		if (m_other_modes.image_read_en) {
			uint8_t hbyte = HREADADDR8((m_misc_state.m_fb_address >> 1) + curpixel);
			m_memory_color.set_a(m_current_mem_cvg << 5);
			m_current_mem_cvg = ((fword & 1) << 2) | (hbyte & 3);
		} else {
			m_memory_color.set_a(0xff);
			m_current_mem_cvg = 7;
		}
	} else // 32-bit framebuffer
	{
		const uint32_t mem = RREADIDX32((m_misc_state.m_fb_address >> 2) + curpixel);
		m_memory_color.set(0, (mem >> 24) & 0xff, (mem >> 16) & 0xff, (mem >> 8) & 0xff);
		if (m_other_modes.image_read_en) {
			m_memory_color.set_a(mem & 0xff);
			m_current_mem_cvg = (mem >> 5) & 7;
		} else {
			m_memory_color.set_a(0xff);
			m_current_mem_cvg = 7;
		}
	}
}

inline void n64_rdp::copy_pixel(uint32_t curpixel, color_t& color) {
	const uint32_t current_pix_cvg = color.get_a() ? 7 : 0;
	const uint8_t r = color.get_r(); // Vectorize me
	const uint8_t g = color.get_g();
	const uint8_t b = color.get_b();
	if (m_misc_state.m_fb_size == 2) // 16-bit framebuffer
	{
		RWRITEIDX16((m_misc_state.m_fb_address >> 1) + curpixel, ((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | ((current_pix_cvg >> 2) & 1));
		HWRITEADDR8((m_misc_state.m_fb_address >> 1) + curpixel, current_pix_cvg & 3);
	} else // 32-bit framebuffer
	{
		RWRITEIDX32((m_misc_state.m_fb_address >> 2) + curpixel, (r << 24) | (g << 16) | (b << 8) | (current_pix_cvg << 5));
	}
}

inline void n64_rdp::fill_pixel(uint32_t curpixel) {
	if (m_misc_state.m_fb_size == 2) // 16-bit framebuffer
	{
		uint16_t val;
		if (curpixel & 1) {
			val = m_fill_color & 0xffff;
		} else {
			val = (m_fill_color >> 16) & 0xffff;
		}
		RWRITEIDX16((m_misc_state.m_fb_address >> 1) + curpixel, val);
		HWRITEADDR8((m_misc_state.m_fb_address >> 1) + curpixel, ((val & 1) << 1) | (val & 1));
	} else // 32-bit framebuffer
	{
		RWRITEIDX32((m_misc_state.m_fb_address >> 2) + curpixel, m_fill_color);
		HWRITEADDR8((m_misc_state.m_fb_address >> 1) + (curpixel << 1), (m_fill_color & 0x10000) ? 3 : 0);
		HWRITEADDR8((m_misc_state.m_fb_address >> 1) + (curpixel << 1) + 1, (m_fill_color & 0x1) ? 3 : 0);
	}
}

void n64_rdp::span_draw_1cycle(int32_t scanline, bool flip, int32_t tilenum) {
	const int32_t clipx1 = m_scissor.m_xh;
	const int32_t clipx2 = m_scissor.m_xl;

	span_param_t r; r.w = m_rstart;
	span_param_t g; g.w = m_gstart;
	span_param_t b; b.w = m_bstart;
	span_param_t a; a.w = m_astart;
	span_param_t s; s.w = m_sstart;
	span_param_t t; t.w = m_tstart;
	span_param_t w; w.w = m_wstart;
	span_param_t z; z.w = m_zstart;

	const uint32_t zb = m_misc_state.m_zb_address >> 1;
	const uint32_t zhb = m_misc_state.m_zb_address;

	m_tex_pipe.calculate_clamp_diffs(tilenum);

	const bool partialreject = (m_color_inputs.blender2b_a[0] == &m_inv_pixel_color && m_color_inputs.blender1b_a[0] == &m_pixel_color);
	const int32_t sel0 = (m_color_inputs.blender2b_a[0] == &m_memory_color) ? 1 : 0;

	int32_t drinc, dginc, dbinc, dainc;
	int32_t dzinc, dzpix;
	int32_t dsinc, dtinc, dwinc;
	int32_t xinc;

	if (!flip) {
		drinc = -m_span_base.m_span_dr;
		dginc = -m_span_base.m_span_dg;
		dbinc = -m_span_base.m_span_db;
		dainc = -m_span_base.m_span_da;
		dzinc = -m_span_base.m_span_dz;
		dsinc = -m_span_base.m_span_ds;
		dtinc = -m_span_base.m_span_dt;
		dwinc = -m_span_base.m_span_dw;
		xinc = -1;
	} else {
		drinc = m_span_base.m_span_dr;
		dginc = m_span_base.m_span_dg;
		dbinc = m_span_base.m_span_db;
		dainc = m_span_base.m_span_da;
		dzinc = m_span_base.m_span_dz;
		dsinc = m_span_base.m_span_ds;
		dtinc = m_span_base.m_span_dt;
		dwinc = m_span_base.m_span_dw;
		xinc = 1;
	}

	const int32_t fb_index = m_misc_state.m_fb_width * scanline;

	const int32_t xstart = m_xstart;
	const int32_t xend = m_unscissored_rx;
	const int32_t xend_scissored = m_xstop;

	int32_t x = xend;

	const int32_t length = flip ? (xstart - xend) : (xend - xstart);

	if (m_other_modes.z_source_sel) {
		z.w = m_misc_state.m_primitive_z;
		dzpix = m_misc_state.m_primitive_dz;
		dzinc = 0;
	} else {
		dzpix = m_span_base.m_span_dzpix;
	}

	const int32_t blend_index = (m_other_modes.alpha_cvg_select ? 2 : 0) | ((m_other_modes.rgb_dither_sel < 3) ? 1 : 0);
	const int32_t cycle0 = ((m_other_modes.sample_type & 1) << 1) | (m_other_modes.bi_lerp0 & 1);

	int32_t sss = 0;
	int32_t sst = 0;

	if (m_other_modes.persp_tex_en) {
		tc_div(s.w >> 16, t.w >> 16, w.w >> 16, &sss, &sst);
	} else {
		tc_div_no_perspective(s.w >> 16, t.w >> 16, w.w >> 16, &sss, &sst);
	}

	m_start_span = true;
	for (int32_t j = 0; j <= length; j++) {
		int32_t sr = r.w >> 14;
		int32_t sg = g.w >> 14;
		int32_t sb = b.w >> 14;
		int32_t sa = a.w >> 14;
		int32_t sz = (z.w >> 10) & 0x3fffff;
		const bool valid_x = (flip) ? (x >= xend_scissored) : (x <= xend_scissored);

		if (x >= clipx1 && x < clipx2 && valid_x) {
			uint8_t offx, offy;
			lookup_cvmask_derivatives(m_cvg[x], &offx, &offy);

			m_tex_pipe.lod_1cycle(&sss, &sst, s.w, t.w, w.w, dsinc, dtinc, dwinc);

			rgbaz_correct_triangle(offx, offy, &sr, &sg, &sb, &sa, &sz);
			rgbaz_clip(sr, sg, sb, sa, &sz);

			((m_tex_pipe).*(m_tex_pipe.m_cycle[cycle0]))(&m_texel0_color, &m_texel0_color, sss, sst, tilenum, 0);
			uint32_t t0a = m_texel0_color.get_a();
			m_texel0_alpha.set(t0a, t0a, t0a, t0a);

			const uint8_t noise = rand() << 3; // Not accurate
			m_noise_color.set(0, noise, noise, noise);

			rgbaint_t rgbsub_a(*m_color_inputs.combiner_rgbsub_a[1]);
			rgbaint_t rgbsub_b(*m_color_inputs.combiner_rgbsub_b[1]);
			rgbaint_t rgbmul(*m_color_inputs.combiner_rgbmul[1]);
			rgbaint_t rgbadd(*m_color_inputs.combiner_rgbadd[1]);

			rgbsub_a.merge_alpha(*m_color_inputs.combiner_alphasub_a[1]);
			rgbsub_b.merge_alpha(*m_color_inputs.combiner_alphasub_b[1]);
			rgbmul.merge_alpha(*m_color_inputs.combiner_alphamul[1]);
			rgbadd.merge_alpha(*m_color_inputs.combiner_alphaadd[1]);

			rgbsub_a.sign_extend(0x180, 0xfffffe00);
			rgbsub_b.sign_extend(0x180, 0xfffffe00);
			rgbadd.sign_extend(0x180, 0xfffffe00);

			rgbadd.shl_imm(8);
			rgbsub_a.sub(rgbsub_b);
			rgbsub_a.mul(rgbmul);
			rgbsub_a.add(rgbadd);
			rgbsub_a.add_imm(0x0080);
			rgbsub_a.sra_imm(8);
			rgbsub_a.clamp_and_clear(0xfffffe00);

			m_pixel_color = rgbsub_a;

			//Alpha coverage combiner
			m_pixel_color.set_a(get_alpha_cvg(m_pixel_color.get_a()));

			const uint32_t curpixel = fb_index + x;
			const uint32_t zbcur = zb + curpixel;
			const uint32_t zhbcur = zhb + curpixel;

			read_pixel(curpixel);

			if (z_compare(zbcur, zhbcur, sz, dzpix)) {
				int32_t cdith = 0;
				int32_t adith = 0;
				get_dither_values(scanline, j, &cdith, &adith);

				color_t blended_pixel;
				bool rendered = ((&m_blender)->*(m_blender.blend1[(m_blend_enable << 2) | blend_index]))(blended_pixel, cdith, adith, partialreject, sel0);

				if (rendered) {
					write_pixel(curpixel, blended_pixel);
					if (m_other_modes.z_update_en) {
						z_store(zbcur, zhbcur, sz, m_dzpix_enc);
					}
				}
			}

			sss = m_tex_pipe.precomp_s();
			sst = m_tex_pipe.precomp_t();
		}

		r.w += drinc;
		g.w += dginc;
		b.w += dbinc;
		a.w += dainc;
		s.w += dsinc;
		t.w += dtinc;
		w.w += dwinc;
		z.w += dzinc;

		x += xinc;
	}
}

void n64_rdp::span_draw_2cycle(int32_t scanline, bool flip, int32_t tilenum) {
	const int32_t clipx1 = m_scissor.m_xh;
	const int32_t clipx2 = m_scissor.m_xl;

	span_param_t r; r.w = m_rstart;
	span_param_t g; g.w = m_gstart;
	span_param_t b; b.w = m_bstart;
	span_param_t a; a.w = m_astart;
	span_param_t s; s.w = m_sstart;
	span_param_t t; t.w = m_tstart;
	span_param_t w; w.w = m_wstart;
	span_param_t z; z.w = m_zstart;

	const uint32_t zb = m_misc_state.m_zb_address >> 1;
	const uint32_t zhb = m_misc_state.m_zb_address;

	int32_t tile2 = (tilenum + 1) & 7;
	int32_t tile1 = tilenum;
	const uint32_t prim_tile = tilenum;

	int32_t newtile1 = tile1;
	int32_t news = 0;
	int32_t newt = 0;

	m_tex_pipe.calculate_clamp_diffs(tile1);

	bool partialreject = (m_color_inputs.blender2b_a[1] == &m_inv_pixel_color && m_color_inputs.blender1b_a[1] == &m_pixel_color);
	int32_t sel0 = (m_color_inputs.blender2b_a[0] == &m_memory_color) ? 1 : 0;
	int32_t sel1 = (m_color_inputs.blender2b_a[1] == &m_memory_color) ? 1 : 0;

	int32_t drinc, dginc, dbinc, dainc;
	int32_t dzinc, dzpix;
	int32_t dsinc, dtinc, dwinc;
	int32_t xinc;

	if (!flip) {
		drinc = -m_span_base.m_span_dr;
		dginc = -m_span_base.m_span_dg;
		dbinc = -m_span_base.m_span_db;
		dainc = -m_span_base.m_span_da;
		dzinc = -m_span_base.m_span_dz;
		dsinc = -m_span_base.m_span_ds;
		dtinc = -m_span_base.m_span_dt;
		dwinc = -m_span_base.m_span_dw;
		xinc = -1;
	} else {
		drinc = m_span_base.m_span_dr;
		dginc = m_span_base.m_span_dg;
		dbinc = m_span_base.m_span_db;
		dainc = m_span_base.m_span_da;
		dzinc = m_span_base.m_span_dz;
		dsinc = m_span_base.m_span_ds;
		dtinc = m_span_base.m_span_dt;
		dwinc = m_span_base.m_span_dw;
		xinc = 1;
	}

	const int32_t fb_index = m_misc_state.m_fb_width * scanline;

	int32_t cdith = 0;
	int32_t adith = 0;

	const int32_t xstart = m_xstart;
	const int32_t xend = m_unscissored_rx;
	const int32_t xend_scissored = m_xstop;

	int32_t x = xend;

	const int32_t length = flip ? (xstart - xend) : (xend - xstart);

	if (m_other_modes.z_source_sel) {
		z.w = m_misc_state.m_primitive_z;
		dzpix = m_misc_state.m_primitive_dz;
		dzinc = 0;
	} else {
		dzpix = m_span_base.m_span_dzpix;
	}

	const int32_t blend_index = (m_other_modes.alpha_cvg_select ? 2 : 0) | ((m_other_modes.rgb_dither_sel < 3) ? 1 : 0);
	const int32_t cycle0 = ((m_other_modes.sample_type & 1) << 1) | (m_other_modes.bi_lerp0 & 1);
	const int32_t cycle1 = ((m_other_modes.sample_type & 1) << 1) | (m_other_modes.bi_lerp1 & 1);

	int32_t sss = 0;
	int32_t sst = 0;

	if (m_other_modes.persp_tex_en) {
		tc_div(s.w >> 16, t.w >> 16, w.w >> 16, &sss, &sst);
	} else {
		tc_div_no_perspective(s.w >> 16, t.w >> 16, w.w >> 16, &sss, &sst);
	}

	m_start_span = true;
	for (int32_t j = 0; j <= length; j++) {
		int32_t sr = r.w >> 14;
		int32_t sg = g.w >> 14;
		int32_t sb = b.w >> 14;
		int32_t sa = a.w >> 14;
		int32_t sz = (z.w >> 10) & 0x3fffff;

		const bool valid_x = (flip) ? (x >= xend_scissored) : (x <= xend_scissored);

		if (x >= clipx1 && x < clipx2 && valid_x) {
			const uint32_t compidx = m_compressed_cvmasks[m_cvg[x]];
			m_current_pix_cvg = cvarray[compidx].cvg;
			m_current_cvg_bit = cvarray[compidx].cvbit;
			const uint8_t offx = cvarray[compidx].xoff;
			const uint8_t offy = cvarray[compidx].yoff;
			//lookup_cvmask_derivatives(m_cvg[x], &offx, &offy);

			m_tex_pipe.lod_2cycle(&sss, &sst, s.w, t.w, w.w, dsinc, dtinc, dwinc, prim_tile, &tile1, &tile2);

			news = m_tex_pipe.precomp_s();
			newt = m_tex_pipe.precomp_t();
			m_tex_pipe.lod_2cycle_limited(&news, &newt, s.w + dsinc, t.w + dtinc, w.w + dwinc, dsinc, dtinc, dwinc, prim_tile, &newtile1);

			rgbaz_correct_triangle(offx, offy, &sr, &sg, &sb, &sa, &sz);
			rgbaz_clip(sr, sg, sb, sa, &sz);

			((m_tex_pipe).*(m_tex_pipe.m_cycle[cycle0]))(&m_texel0_color, &m_texel0_color, sss, sst, tile1, 0);
			((m_tex_pipe).*(m_tex_pipe.m_cycle[cycle1]))(&m_texel1_color, &m_texel0_color, sss, sst, tile2, 1);
			((m_tex_pipe).*(m_tex_pipe.m_cycle[cycle1]))(&m_next_texel_color, &m_next_texel_color, sss, sst, tile2, 1);

			uint32_t t0a = m_texel0_color.get_a();
			uint32_t t1a = m_texel1_color.get_a();
			uint32_t tna = m_next_texel_color.get_a();
			m_texel0_alpha.set(t0a, t0a, t0a, t0a);
			m_texel1_alpha.set(t1a, t1a, t1a, t1a);
			m_next_texel_alpha.set(tna, tna, tna, tna);

			const uint8_t noise = rand() << 3; // Not accurate
			m_noise_color.set(0, noise, noise, noise);

			rgbaint_t rgbsub_a(*m_color_inputs.combiner_rgbsub_a[0]);
			rgbaint_t rgbsub_b(*m_color_inputs.combiner_rgbsub_b[0]);
			rgbaint_t rgbmul(*m_color_inputs.combiner_rgbmul[0]);
			rgbaint_t rgbadd(*m_color_inputs.combiner_rgbadd[0]);

			rgbsub_a.merge_alpha(*m_color_inputs.combiner_alphasub_a[0]);
			rgbsub_b.merge_alpha(*m_color_inputs.combiner_alphasub_b[0]);
			rgbmul.merge_alpha(*m_color_inputs.combiner_alphamul[0]);
			rgbadd.merge_alpha(*m_color_inputs.combiner_alphaadd[0]);

			rgbsub_a.sign_extend(0x180, 0xfffffe00);
			rgbsub_b.sign_extend(0x180, 0xfffffe00);
			rgbadd.sign_extend(0x180, 0xfffffe00);

			rgbadd.shl_imm(8);
			rgbsub_a.sub(rgbsub_b);
			rgbsub_a.mul(rgbmul);

			rgbsub_a.add(rgbadd);
			rgbsub_a.add_imm(0x0080);
			rgbsub_a.sra_imm(8);
			rgbsub_a.clamp_and_clear(0xfffffe00);

			m_combined_color.set(rgbsub_a);
			m_texel0_color.set(m_texel1_color);
			m_texel1_color.set(m_next_texel_color);

			uint32_t ca = m_combined_color.get_a();
			m_combined_alpha.set(ca, ca, ca, ca);
			m_texel0_alpha.set(m_texel1_alpha);
			m_texel1_alpha.set(m_next_texel_alpha);

			rgbsub_a.set(*m_color_inputs.combiner_rgbsub_a[1]);
			rgbsub_b.set(*m_color_inputs.combiner_rgbsub_b[1]);
			rgbmul.set(*m_color_inputs.combiner_rgbmul[1]);
			rgbadd.set(*m_color_inputs.combiner_rgbadd[1]);

			rgbsub_a.merge_alpha(*m_color_inputs.combiner_alphasub_a[1]);
			rgbsub_b.merge_alpha(*m_color_inputs.combiner_alphasub_b[1]);
			rgbmul.merge_alpha(*m_color_inputs.combiner_alphamul[1]);
			rgbadd.merge_alpha(*m_color_inputs.combiner_alphaadd[1]);

			rgbsub_a.sign_extend(0x180, 0xfffffe00);
			rgbsub_b.sign_extend(0x180, 0xfffffe00);
			rgbadd.sign_extend(0x180, 0xfffffe00);

			rgbadd.shl_imm(8);
			rgbsub_a.sub(rgbsub_b);
			rgbsub_a.mul(rgbmul);
			rgbsub_a.add(rgbadd);
			rgbsub_a.add_imm(0x0080);
			rgbsub_a.sra_imm(8);
			rgbsub_a.clamp_and_clear(0xfffffe00);

			m_pixel_color.set(rgbsub_a);

			//Alpha coverage combiner
			m_pixel_color.set_a(get_alpha_cvg(m_pixel_color.get_a()));

			const uint32_t curpixel = fb_index + x;
			const uint32_t zbcur = zb + curpixel;
			const uint32_t zhbcur = zhb + curpixel;

			read_pixel(curpixel);

			if (z_compare(zbcur, zhbcur, sz, dzpix)) {
				get_dither_values(scanline, j, &cdith, &adith);

				color_t blended_pixel;
				bool rendered = ((&m_blender)->*(m_blender.blend2[(m_blend_enable << 2) | blend_index]))(blended_pixel, cdith, adith, partialreject, sel0, sel1);

				if (rendered) {
					write_pixel(curpixel, blended_pixel);
					if (m_other_modes.z_update_en) {
						z_store(zbcur, zhbcur, sz, m_dzpix_enc);
					}
				}
			}
			sss = m_tex_pipe.precomp_s();
			sst = m_tex_pipe.precomp_t();
		}

		r.w += drinc;
		g.w += dginc;
		b.w += dbinc;
		a.w += dainc;
		s.w += dsinc;
		t.w += dtinc;
		w.w += dwinc;
		z.w += dzinc;

		x += xinc;
	}
}

void n64_rdp::span_draw_copy(int32_t scanline, bool flip, int32_t tilenum) {
	const int32_t clipx1 = m_scissor.m_xh;
	const int32_t clipx2 = m_scissor.m_xl;

	const int32_t xstart = m_xstart;
	const int32_t xend = m_unscissored_rx;
	const int32_t xend_scissored = m_xstop;
	const int32_t xinc = flip ? 1 : -1;
	const int32_t length = flip ? (xstart - xend) : (xend - xstart);

	span_param_t s; s.w = m_sstart;
	span_param_t t; t.w = m_tstart;

	const int32_t ds = m_span_base.m_span_ds / 4;
	const int32_t dt = m_span_base.m_span_dt / 4;
	const int32_t dsinc = flip ? (ds) : -ds;
	const int32_t dtinc = flip ? (dt) : -dt;

	const int32_t fb_index = m_misc_state.m_fb_width * scanline;

	int32_t x = xend;

	for (int32_t j = 0; j <= length; j++) {
		const bool valid_x = (flip) ? (x >= xend_scissored) : (x <= xend_scissored);

		if (x >= clipx1 && x < clipx2 && valid_x) {
			int32_t sss = s.h.h;
			int32_t sst = t.h.h;
			m_tex_pipe.copy(&m_texel0_color, sss, sst, tilenum);

			uint32_t curpixel = fb_index + x;
			if ((m_texel0_color.get_a() != 0) || (!m_other_modes.alpha_compare_en)) {
				copy_pixel(curpixel, m_texel0_color);
			}
		}

		s.w += dsinc;
		t.w += dtinc;
		x += xinc;
	}
}

void n64_rdp::span_draw_fill(int32_t scanline, bool flip, int32_t tilenum) {
	const int32_t clipx1 = m_scissor.m_xh;
	const int32_t clipx2 = m_scissor.m_xl;

	const int32_t xinc = flip ? 1 : -1;

	const int32_t fb_index = m_misc_state.m_fb_width * scanline;

	const int32_t xstart = m_xstart;
	const int32_t xend_scissored = m_xstop;

	int32_t x = xend_scissored;

	const int32_t length = flip ? (xstart - xend_scissored) : (xend_scissored - xstart);

	for (int32_t j = 0; j <= length; j++) {
		if (x >= clipx1 && x < clipx2) {
			fill_pixel(fb_index + x);
		}

		x += xinc;
	}
}
