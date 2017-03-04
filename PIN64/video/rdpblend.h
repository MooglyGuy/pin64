// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
/******************************************************************************


SGI/Nintendo Reality Display Processor Blend Unit (BL)
-------------------

by Ryan Holtz
based on initial C code by Ville Linde
contains additional improvements from angrylion, Ziggy, Gonetz and Orkin


******************************************************************************/

#ifndef _VIDEO_RDPBLEND_H_
#define _VIDEO_RDPBLEND_H_

#include "../emu.h"
#include "n64types.h"

class n64_rdp;

class n64_blender_t {
public:
	typedef bool (n64_blender_t::*blender1)(color_t& blended_pixel, int dith, int adseed, int partialreject, int sel0);
	typedef bool (n64_blender_t::*blender2)(color_t& blended_pixel, int dith, int adseed, int partialreject, int sel0, int sel1);

	n64_blender_t();

	blender1            blend1[8];
	blender2            blend2[8];

	void                set_processor(n64_rdp* rdp) { m_rdp = rdp; }

private:
	n64_rdp*            m_rdp;

	int32_t min(const int32_t x, const int32_t min);
	bool alpha_reject();
	bool test_for_reject();
	void blend_pipe(const int cycle, const int special, color_t& out);
	void blend_with_partial_reject(color_t& out, int32_t cycle, int32_t partialreject, int32_t select);

	bool cycle1_noblend_noacvg_nodither(color_t& blended_pixel, int dith, int adseed, int partialreject, int sel0);
	bool cycle1_noblend_noacvg_dither(color_t& blended_pixel, int dith, int adseed, int partialreject, int sel0);
	bool cycle1_noblend_acvg_nodither(color_t& blended_pixel, int dith, int adseed, int partialreject, int sel0);
	bool cycle1_noblend_acvg_dither(color_t& blended_pixel, int dith, int adseed, int partialreject, int sel0);
	bool cycle1_blend_noacvg_nodither(color_t& blended_pixel, int dith, int adseed, int partialreject, int sel0);
	bool cycle1_blend_noacvg_dither(color_t& blended_pixel, int dith, int adseed, int partialreject, int sel0);
	bool cycle1_blend_acvg_nodither(color_t& blended_pixel, int dith, int adseed, int partialreject, int sel0);
	bool cycle1_blend_acvg_dither(color_t& blended_pixel, int dith, int adseed, int partialreject, int sel0);

	bool cycle2_noblend_noacvg_nodither(color_t& blended_pixel, int dith, int adseed, int partialreject, int sel0, int sel1);
	bool cycle2_noblend_noacvg_dither(color_t& blended_pixel, int dith, int adseed, int partialreject, int sel0, int sel1);
	bool cycle2_noblend_acvg_nodither(color_t& blended_pixel, int dith, int adseed, int partialreject, int sel0, int sel1);
	bool cycle2_noblend_acvg_dither(color_t& blended_pixel, int dith, int adseed, int partialreject, int sel0, int sel1);
	bool cycle2_blend_noacvg_nodither(color_t& blended_pixel, int dith, int adseed, int partialreject, int sel0, int sel1);
	bool cycle2_blend_noacvg_dither(color_t& blended_pixel, int dith, int adseed, int partialreject, int sel0, int sel1);
	bool cycle2_blend_acvg_nodither(color_t& blended_pixel, int dith, int adseed, int partialreject, int sel0, int sel1);
	bool cycle2_blend_acvg_dither(color_t& blended_pixel, int dith, int adseed, int partialreject, int sel0, int sel1);

	int32_t dither_alpha(int32_t alpha, int32_t dither);
	int32_t dither_color(int32_t color, int32_t dither);

	uint8_t               m_color_dither[256 * 8];
	uint8_t               m_alpha_dither[256 * 8];
};

#endif // _VIDEO_RDPBLEND_H_
