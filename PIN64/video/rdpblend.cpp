// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
/******************************************************************************


SGI/Nintendo Reality Display Processor Blend Unit (BL)
-------------------

by Ryan Holtz
based on initial C code by Ville Linde
contains additional improvements from angrylion, Ziggy, Gonetz and Orkin


******************************************************************************/

#include "../emu.h"
#include "n64.h"
#include <cstdlib>

n64_blender_t::n64_blender_t() {
	blend1[0] = &n64_blender_t::cycle1_noblend_noacvg_nodither;
	blend1[1] = &n64_blender_t::cycle1_noblend_noacvg_dither;
	blend1[2] = &n64_blender_t::cycle1_noblend_acvg_nodither;
	blend1[3] = &n64_blender_t::cycle1_noblend_acvg_dither;
	blend1[4] = &n64_blender_t::cycle1_blend_noacvg_nodither;
	blend1[5] = &n64_blender_t::cycle1_blend_noacvg_dither;
	blend1[6] = &n64_blender_t::cycle1_blend_acvg_nodither;
	blend1[7] = &n64_blender_t::cycle1_blend_acvg_dither;

	blend2[0] = &n64_blender_t::cycle2_noblend_noacvg_nodither;
	blend2[1] = &n64_blender_t::cycle2_noblend_noacvg_dither;
	blend2[2] = &n64_blender_t::cycle2_noblend_acvg_nodither;
	blend2[3] = &n64_blender_t::cycle2_noblend_acvg_dither;
	blend2[4] = &n64_blender_t::cycle2_blend_noacvg_nodither;
	blend2[5] = &n64_blender_t::cycle2_blend_noacvg_dither;
	blend2[6] = &n64_blender_t::cycle2_blend_acvg_nodither;
	blend2[7] = &n64_blender_t::cycle2_blend_acvg_dither;

	for (int value = 0; value < 256; value++) {
		for (int dither = 0; dither < 8; dither++) {
			m_color_dither[(value << 3) | dither] = (uint8_t)dither_color(value, dither);
			m_alpha_dither[(value << 3) | dither] = (uint8_t)dither_alpha(value, dither);
		}
	}
}

int32_t n64_blender_t::dither_alpha(int32_t alpha, int32_t dither) {
	return min(alpha + dither, 0xff);
}

int32_t n64_blender_t::dither_color(int32_t color, int32_t dither) {
	if ((color & 7) > dither) {
		color = (color & 0xf8) + 8;
		if (color > 247) {
			color = 255;
		}
	}
	return color;
}

bool n64_blender_t::test_for_reject() {
	if (alpha_reject()) {
		return true;
	}
	if (m_rdp->m_other_modes.antialias_en ? !m_rdp->m_current_pix_cvg : !m_rdp->m_current_cvg_bit) {
		return true;
	}
	return false;
}

bool n64_blender_t::alpha_reject() {
	switch (m_rdp->m_other_modes.alpha_dither_mode) {
	case 0:
	case 1:
		return false;

	case 2:
		return m_rdp->m_pixel_color.get_a() < m_rdp->m_blend_color.get_a();

	case 3:
		return m_rdp->m_pixel_color.get_a() < (rand() & 0xff);

	default:
		return false;
	}
}

bool n64_blender_t::cycle1_noblend_noacvg_nodither(color_t& blended_pixel, int dith, int adseed, int partialreject, int sel0) {
	m_rdp->m_pixel_color.set_a(m_alpha_dither[((uint8_t)m_rdp->m_pixel_color.get_a() << 3) | adseed]);
	m_rdp->m_shade_color.set_a(m_alpha_dither[((uint8_t)m_rdp->m_shade_color.get_a() << 3) | adseed]);
	if (test_for_reject()) {
		return false;
	}
	blended_pixel.set(*m_rdp->m_color_inputs.blender1a_rgb[0]);

	return true;
}

bool n64_blender_t::cycle1_noblend_noacvg_dither(color_t& blended_pixel, int dith, int adseed, int partialreject, int sel0) {
	m_rdp->m_pixel_color.set_a(m_alpha_dither[((uint8_t)m_rdp->m_pixel_color.get_a() << 3) | adseed]);
	m_rdp->m_shade_color.set_a(m_alpha_dither[((uint8_t)m_rdp->m_shade_color.get_a() << 3) | adseed]);
	if (test_for_reject()) {
		return false;
	}

	rgbaint_t index(*m_rdp->m_color_inputs.blender1a_rgb[0]);
	index.shl_imm(3);
	index.or_imm(dith);
	index.and_imm(0x7ff);
	blended_pixel.set(0, m_color_dither[index.get_r32()], m_color_dither[index.get_g32()], m_color_dither[index.get_b32()]);

	return true;
}

bool n64_blender_t::cycle1_noblend_acvg_nodither(color_t& blended_pixel, int dith, int adseed, int partialreject, int sel0) {
	m_rdp->m_shade_color.set_a(m_alpha_dither[((uint8_t)m_rdp->m_shade_color.get_a() << 3) | adseed]);

	if (test_for_reject()) {
		return false;
	}
	blended_pixel.set(*m_rdp->m_color_inputs.blender1a_rgb[0]);

	return true;
}

bool n64_blender_t::cycle1_noblend_acvg_dither(color_t& blended_pixel, int dith, int adseed, int partialreject, int sel0) {
	m_rdp->m_shade_color.set_a(m_alpha_dither[((uint8_t)m_rdp->m_shade_color.get_a() << 3) | adseed]);

	if (test_for_reject()) {
		return false;
	}

	rgbaint_t index(*m_rdp->m_color_inputs.blender1a_rgb[0]);
	index.shl_imm(3);
	index.or_imm(dith);
	index.and_imm(0x7ff);
	blended_pixel.set(0, m_color_dither[index.get_r32()], m_color_dither[index.get_g32()], m_color_dither[index.get_b32()]);

	return true;
}

bool n64_blender_t::cycle1_blend_noacvg_nodither(color_t& blended_pixel, int dith, int adseed, int partialreject, int sel0) {
	m_rdp->m_pixel_color.set_a(m_alpha_dither[((uint8_t)m_rdp->m_pixel_color.get_a() << 3) | adseed]);
	m_rdp->m_shade_color.set_a(m_alpha_dither[((uint8_t)m_rdp->m_shade_color.get_a() << 3) | adseed]);

	if (test_for_reject()) {
		return false;
	}

	blend_with_partial_reject(blended_pixel, 0, partialreject, sel0);

	return true;
}

bool n64_blender_t::cycle1_blend_noacvg_dither(color_t& blended_pixel, int dith, int adseed, int partialreject, int sel0) {
	m_rdp->m_pixel_color.set_a(m_alpha_dither[((uint8_t)m_rdp->m_pixel_color.get_a() << 3) | adseed]);
	m_rdp->m_shade_color.set_a(m_alpha_dither[((uint8_t)m_rdp->m_shade_color.get_a() << 3) | adseed]);

	if (test_for_reject()) {
		return false;
	}

	color_t rgb;
	blend_with_partial_reject(rgb, 0, partialreject, sel0);

	rgb.shl_imm(3);
	rgb.or_imm(dith);
	rgb.and_imm(0x7ff);
	blended_pixel.set(0, m_color_dither[rgb.get_r32()], m_color_dither[rgb.get_g32()], m_color_dither[rgb.get_b32()]);

	return true;
}

bool n64_blender_t::cycle1_blend_acvg_nodither(color_t& blended_pixel, int dith, int adseed, int partialreject, int sel0) {
	m_rdp->m_shade_color.set_a(m_alpha_dither[((uint8_t)m_rdp->m_shade_color.get_a() << 3) | adseed]);

	if (test_for_reject()) {
		return false;
	}

	blend_with_partial_reject(blended_pixel, 0, partialreject, sel0);

	return true;
}

bool n64_blender_t::cycle1_blend_acvg_dither(color_t& blended_pixel, int dith, int adseed, int partialreject, int sel0) {
	m_rdp->m_shade_color.set_a(m_alpha_dither[((uint8_t)m_rdp->m_shade_color.get_a() << 3) | adseed]);

	if (test_for_reject()) {
		return false;
	}

	color_t rgb;
	blend_with_partial_reject(rgb, 0, partialreject, sel0);

	rgb.shl_imm(3);
	rgb.or_imm(dith);
	rgb.and_imm(0x7ff);
	blended_pixel.set(0, m_color_dither[rgb.get_r32()], m_color_dither[rgb.get_g32()], m_color_dither[rgb.get_b32()]);

	return true;
}

bool n64_blender_t::cycle2_noblend_noacvg_nodither(color_t& blended_pixel, int dith, int adseed, int partialreject, int sel0, int sel1) {
	m_rdp->m_pixel_color.set_a(m_alpha_dither[((uint8_t)m_rdp->m_pixel_color.get_a() << 3) | adseed]);
	m_rdp->m_shade_color.set_a(m_alpha_dither[((uint8_t)m_rdp->m_shade_color.get_a() << 3) | adseed]);

	if (test_for_reject()) {
		return false;
	}

	m_rdp->m_inv_pixel_color.set_a(0xff - m_rdp->m_color_inputs.blender1b_a[0]->get_a());
	blend_pipe(0, sel0, m_rdp->m_blended_pixel_color);
	m_rdp->m_blended_pixel_color.set_a(m_rdp->m_pixel_color.get_a());

	blended_pixel.set(*m_rdp->m_color_inputs.blender1a_rgb[1]);

	return true;
}

bool n64_blender_t::cycle2_noblend_noacvg_dither(color_t& blended_pixel, int dith, int adseed, int partialreject, int sel0, int sel1) {
	m_rdp->m_pixel_color.set_a(m_alpha_dither[((uint8_t)m_rdp->m_pixel_color.get_a() << 3) | adseed]);
	m_rdp->m_shade_color.set_a(m_alpha_dither[((uint8_t)m_rdp->m_shade_color.get_a() << 3) | adseed]);

	if (test_for_reject()) {
		return false;
	}

	m_rdp->m_inv_pixel_color.set_a(0xff - (uint8_t)m_rdp->m_color_inputs.blender1b_a[0]->get_a());
	blend_pipe(0, sel0, m_rdp->m_blended_pixel_color);
	m_rdp->m_blended_pixel_color.set_a(m_rdp->m_pixel_color.get_a());

	rgbaint_t index(*m_rdp->m_color_inputs.blender1a_rgb[1]);
	index.shl_imm(3);
	index.or_imm(dith);
	index.and_imm(0x7ff);
	blended_pixel.set(0, m_color_dither[index.get_r32()], m_color_dither[index.get_g32()], m_color_dither[index.get_b32()]);

	return true;
}

bool n64_blender_t::cycle2_noblend_acvg_nodither(color_t& blended_pixel, int dith, int adseed, int partialreject, int sel0, int sel1) {
	m_rdp->m_shade_color.set_a(m_alpha_dither[((uint8_t)m_rdp->m_shade_color.get_a() << 3) | adseed]);

	if (test_for_reject()) {
		return false;
	}

	m_rdp->m_inv_pixel_color.set_a(0xff - m_rdp->m_color_inputs.blender1b_a[0]->get_a());
	blend_pipe(0, sel0, m_rdp->m_blended_pixel_color);
	m_rdp->m_blended_pixel_color.set_a(m_rdp->m_pixel_color.get_a());

	blended_pixel.set(*m_rdp->m_color_inputs.blender1a_rgb[1]);

	return true;
}

bool n64_blender_t::cycle2_noblend_acvg_dither(color_t& blended_pixel, int dith, int adseed, int partialreject, int sel0, int sel1) {
	m_rdp->m_shade_color.set_a(m_alpha_dither[((uint8_t)m_rdp->m_shade_color.get_a() << 3) | adseed]);

	if (test_for_reject()) {
		return false;
	}

	m_rdp->m_inv_pixel_color.set_a(0xff - m_rdp->m_color_inputs.blender1b_a[0]->get_a());
	blend_pipe(0, sel0, m_rdp->m_blended_pixel_color);
	m_rdp->m_blended_pixel_color.set_a(m_rdp->m_pixel_color.get_a());

	rgbaint_t index(*m_rdp->m_color_inputs.blender1a_rgb[1]);
	index.shl_imm(3);
	index.or_imm(dith);
	index.and_imm(0x7ff);
	blended_pixel.set(0, m_color_dither[index.get_r32()], m_color_dither[index.get_g32()], m_color_dither[index.get_b32()]);

	return true;
}

bool n64_blender_t::cycle2_blend_noacvg_nodither(color_t& blended_pixel, int dith, int adseed, int partialreject, int sel0, int sel1) {
	m_rdp->m_pixel_color.set_a(m_alpha_dither[((uint8_t)m_rdp->m_pixel_color.get_a() << 3) | adseed]);
	m_rdp->m_shade_color.set_a(m_alpha_dither[((uint8_t)m_rdp->m_shade_color.get_a() << 3) | adseed]);

	if (test_for_reject()) {
		return false;
	}

	m_rdp->m_inv_pixel_color.set_a(0xff - m_rdp->m_color_inputs.blender1b_a[0]->get_a());
	blend_pipe(0, sel0, m_rdp->m_blended_pixel_color);
	m_rdp->m_blended_pixel_color.set_a(m_rdp->m_pixel_color.get_a());

	blend_with_partial_reject(blended_pixel, 1, partialreject, sel1);

	return true;
}

bool n64_blender_t::cycle2_blend_noacvg_dither(color_t& blended_pixel, int dith, int adseed, int partialreject, int sel0, int sel1) {
	m_rdp->m_pixel_color.set_a(m_alpha_dither[(m_rdp->m_pixel_color.get_a() << 3) | adseed]);
	m_rdp->m_shade_color.set_a(m_alpha_dither[(m_rdp->m_shade_color.get_a() << 3) | adseed]);

	if (test_for_reject()) {
		return false;
	}

	m_rdp->m_inv_pixel_color.set_a(0xff - m_rdp->m_color_inputs.blender1b_a[0]->get_a());
	blend_pipe(0, sel0, m_rdp->m_blended_pixel_color);
	m_rdp->m_blended_pixel_color.set_a(m_rdp->m_pixel_color.get_a());

	color_t rgb;
	blend_with_partial_reject(rgb, 1, partialreject, sel1);

	rgb.shl_imm(3);
	rgb.or_imm(dith);
	rgb.and_imm(0x7ff);
	blended_pixel.set(0, m_color_dither[rgb.get_r32()], m_color_dither[rgb.get_g32()], m_color_dither[rgb.get_b32()]);

	return true;
}

bool n64_blender_t::cycle2_blend_acvg_nodither(color_t& blended_pixel, int dith, int adseed, int partialreject, int sel0, int sel1) {
	m_rdp->m_shade_color.set_a(m_alpha_dither[(m_rdp->m_shade_color.get_a() << 3) | adseed]);

	if (test_for_reject()) {
		return false;
	}

	m_rdp->m_inv_pixel_color.set_a(0xff - m_rdp->m_color_inputs.blender1b_a[0]->get_a());
	blend_pipe(0, sel0, m_rdp->m_blended_pixel_color);
	m_rdp->m_blended_pixel_color.set_a(m_rdp->m_pixel_color.get_a());

	blend_with_partial_reject(blended_pixel, 1, partialreject, sel1);

	return true;
}

bool n64_blender_t::cycle2_blend_acvg_dither(color_t& blended_pixel, int dith, int adseed, int partialreject, int sel0, int sel1) {
	m_rdp->m_shade_color.set_a(m_alpha_dither[(m_rdp->m_shade_color.get_a() << 3) | adseed]);

	if (test_for_reject()) {
		return false;
	}

	m_rdp->m_inv_pixel_color.set_a(0xff - m_rdp->m_color_inputs.blender1b_a[0]->get_a());
	blend_pipe(0, sel0, m_rdp->m_blended_pixel_color);
	m_rdp->m_blended_pixel_color.set_a(m_rdp->m_pixel_color.get_a());

	color_t rgb;
	blend_with_partial_reject(rgb, 1, partialreject, sel1);

	rgb.shl_imm(3);
	rgb.or_imm(dith);
	rgb.and_imm(0x7ff);
	blended_pixel.set(0, m_color_dither[rgb.get_r32()], m_color_dither[rgb.get_g32()], m_color_dither[rgb.get_b32()]);

	return true;
}

void n64_blender_t::blend_with_partial_reject(color_t& out, int32_t cycle, int32_t partialreject, int32_t select) {
	if (partialreject && m_rdp->m_pixel_color.get_a() >= 0xff) {
		out.set(*m_rdp->m_color_inputs.blender1a_rgb[cycle]);
	} else {
		m_rdp->m_inv_pixel_color.set_a(0xff - m_rdp->m_color_inputs.blender1b_a[cycle]->get_a());
		blend_pipe(cycle, select, out);
	}
}

void n64_blender_t::blend_pipe(const int cycle, const int special, color_t& out) {
	const int32_t mask = 0xff & ~(0x73 * special);
	const int32_t shift_a = 3 + m_rdp->m_shift_a * special;
	const int32_t shift_b = 3 + m_rdp->m_shift_b * special;
	const int32_t blend1a = (m_rdp->m_color_inputs.blender1b_a[cycle]->get_a() >> shift_a) & mask;
	const int32_t blend2a = (m_rdp->m_color_inputs.blender2b_a[cycle]->get_a() >> shift_b) & mask;
	const int32_t special_shift = special << 1;

	rgbaint_t temp(*m_rdp->m_color_inputs.blender1a_rgb[cycle]);
	temp.mul_imm(blend1a);

	rgbaint_t secondary(*m_rdp->m_color_inputs.blender2a_rgb[cycle]);
	rgbaint_t other(*m_rdp->m_color_inputs.blender2a_rgb[cycle]);
	other.mul_imm(blend2a);

	temp.add(other);
	secondary.shl_imm(special_shift);
	temp.add(secondary);
	temp.shr_imm(m_rdp->m_other_modes.blend_shift);

	int32_t factor_sum = 0;
	if (!m_rdp->m_other_modes.force_blend) {
		factor_sum = ((blend1a >> 2) + (blend2a >> 2) + 1) & 0xf;
		if (factor_sum) {
			temp.set_r(temp.get_r32() / factor_sum);
			temp.set_g(temp.get_g32() / factor_sum);
			temp.set_b(temp.get_b32() / factor_sum);
		} else {
			temp.set(0, 0xff, 0xff, 0xff);
		}
	}

	temp.min(255);
	out.set(temp);
}

inline int32_t n64_blender_t::min(const int32_t x, const int32_t min) {
	if (x < min) {
		return x;
	}
	return min;
}
