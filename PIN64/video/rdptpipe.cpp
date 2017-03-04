// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
/******************************************************************************


SGI/Nintendo Reality Display Texture Fetch Unit (TF)
-------------------

by Ryan Holtz
based on initial C code by Ville Linde
contains additional improvements from angrylion, Ziggy, Gonetz and Orkin


******************************************************************************/

#include "../emu.h"
#include "n64.h"
#include "rdptpipe.h"
#include "rgbutil.h"

#define RELATIVE(x, y)  ((((x) >> 3) - (y)) << 3) | (x & 7);

void n64_texture_pipe_t::init(n64_rdp* rdp) {
	m_rdp = rdp;

	for (int32_t i = 0; i < 0x10000; i++) {
		m_expand_16to32_table[i] = color_t((i & 1) ? 0xff : 0x00, m_rdp->m_replicated_rgba[(i >> 11) & 0x1f], m_rdp->m_replicated_rgba[(i >> 6) & 0x1f], m_rdp->m_replicated_rgba[(i >> 1) & 0x1f]);
	}

	for (uint32_t i = 0; i < 0x80000; i++) {
		if (i & 0x40000) {
			m_lod_lookup[i] = 0x7fff;
		} else if (i & 0x20000) {
			m_lod_lookup[i] = 0x8000;
		} else {
			if ((i & 0x18000) == 0x8000) {
				m_lod_lookup[i] = 0x7fff;
			} else if ((i & 0x18000) == 0x10000) {
				m_lod_lookup[i] = 0x8000;
			} else {
				m_lod_lookup[i] = i & 0xffff;
			}
		}
	}

	m_st2_add.set(1, 0, 1, 0);
	m_v1.set(1, 1, 1, 1);
}

void n64_texture_pipe_t::mask(rgbaint_t& sstt, const n64_tile_t& tile) {
	uint32_t s_mask_bits = m_maskbits_table[tile.mask_s];
	uint32_t t_mask_bits = m_maskbits_table[tile.mask_t];
	rgbaint_t maskbits(s_mask_bits, s_mask_bits, t_mask_bits, t_mask_bits);

	rgbaint_t do_wrap(sstt);
	do_wrap.sra(tile.wrapped_mask);
	do_wrap.and_reg(m_v1);
	do_wrap.cmpeq(m_v1);
	do_wrap.and_reg(tile.mm);

	rgbaint_t wrapped(sstt);
	wrapped.xor_reg(do_wrap);
	wrapped.and_reg(maskbits);
	wrapped.and_reg(tile.mask);
	sstt.and_reg(tile.invmask);
	sstt.or_reg(wrapped);
}

rgbaint_t n64_texture_pipe_t::shift_cycle(rgbaint_t& st, const n64_tile_t& tile) {
	st.sign_extend(0x00008000, 0xffff8000);
	st.sra(tile.rshift);
	st.shl(tile.lshift);

	rgbaint_t maxst(st);
	maxst.sra_imm(3);
	rgbaint_t maxst_eq(maxst);
	maxst.cmpgt(tile.sth);
	maxst_eq.cmpeq(tile.sth);
	maxst.or_reg(maxst_eq);

	rgbaint_t stlsb(st);
	stlsb.and_imm(7);

	st.sra_imm(3);
	st.sub(tile.stl);
	st.shl_imm(3);
	st.or_reg(stlsb);

	return maxst;
}

inline void n64_texture_pipe_t::shift_copy(rgbaint_t& st, const n64_tile_t& tile) {
	st.shr(tile.rshift);
	st.shl(tile.lshift);
}

void n64_texture_pipe_t::clamp_cycle(rgbaint_t& st, rgbaint_t& stfrac, rgbaint_t& maxst, const int32_t tilenum, const n64_tile_t& tile) {
	rgbaint_t not_clamp(tile.clamp_st);
	not_clamp.xor_imm(0xffffffff);

	rgbaint_t highbit_mask(0x10000, 0x10000, 0x10000, 0x10000);
	rgbaint_t highbit(st);
	highbit.and_reg(highbit_mask);
	highbit.cmpeq(highbit_mask);

	rgbaint_t not_highbit(highbit);
	not_highbit.xor_imm(0xffffffff);

	rgbaint_t not_maxst(maxst);
	not_maxst.xor_imm(0xffffffff);
	not_maxst.and_reg(not_highbit);
	not_maxst.or_reg(not_clamp);

	rgbaint_t shifted_st(st);
	shifted_st.sign_extend(0x00010000, 0xffff0000);
	shifted_st.shr_imm(5);
	shifted_st.and_imm(0x1fff);
	shifted_st.and_reg(not_maxst);
	stfrac.and_reg(not_maxst);

	rgbaint_t clamp_diff(m_clamp_diff[tilenum]);
	clamp_diff.and_reg(tile.clamp_st);
	clamp_diff.and_reg(maxst);

	st.set(shifted_st);
	st.or_reg(clamp_diff);
}

void n64_texture_pipe_t::clamp_cycle_light(rgbaint_t& st, rgbaint_t& maxst, const int32_t tilenum, const n64_tile_t& tile) {
	rgbaint_t not_clamp(tile.clamp_st);
	not_clamp.xor_imm(0xffffffff);

	rgbaint_t highbit_mask(0x10000, 0x10000, 0x10000, 0x10000);
	rgbaint_t highbit(st);
	highbit.and_reg(highbit_mask);
	highbit.cmpeq(highbit_mask);

	rgbaint_t not_highbit(highbit);
	not_highbit.xor_imm(0xffffffff);

	rgbaint_t not_maxst(maxst);
	not_maxst.xor_imm(0xffffffff);
	not_maxst.and_reg(not_highbit);
	not_maxst.or_reg(not_clamp);

	rgbaint_t shifted_st(st);
	shifted_st.sign_extend(0x00010000, 0xffff0000);
	shifted_st.shr_imm(5);
	shifted_st.and_imm(0x1fff);
	shifted_st.and_reg(not_maxst);

	rgbaint_t clamp_diff(m_clamp_diff[tilenum]);
	clamp_diff.and_reg(tile.clamp_st);
	clamp_diff.and_reg(maxst);

	st.set(shifted_st);
	st.or_reg(clamp_diff);
}

void n64_texture_pipe_t::cycle_nearest(color_t* TEX, color_t* prev, int32_t SSS, int32_t SST, uint32_t tilenum, uint32_t cycle) {
	const n64_tile_t& tile = m_rdp->m_tiles[tilenum];
	const uint32_t index = (tile.format << 4) | (tile.size << 2) | ((uint32_t)m_rdp->m_other_modes.en_tlut << 1) | (uint32_t)m_rdp->m_other_modes.tlut_type;

	rgbaint_t st(0, SSS, 0, SST);
	rgbaint_t maxst = shift_cycle(st, tile);
	clamp_cycle_light(st, maxst, tilenum, tile);
	mask(st, tile);

	uint32_t tbase = tile.tmem + ((tile.line * st.get_b32()) & 0x1ff);

	rgbaint_t t0;
	((this)->*(m_texel_fetch[index]))(t0, st.get_r32(), st.get_b32(), tbase, tile.palette);
	if (m_rdp->m_other_modes.convert_one && cycle) {
		t0.set(*prev);
	}

	t0.sign_extend(0x00000100, 0xffffff00);

	rgbaint_t k1r(m_rdp->get_k1());
	k1r.mul_imm(t0.get_r32());

	TEX->set(m_rdp->get_k023());
	TEX->mul_imm(t0.get_g32());
	TEX->add(k1r);
	TEX->add_imm(0x80);
	TEX->shr_imm(8);
	TEX->add_imm(t0.get_b32());
	TEX->and_imm(0x1ff);
}

void n64_texture_pipe_t::cycle_nearest_lerp(color_t* TEX, color_t* prev, int32_t SSS, int32_t SST, uint32_t tilenum, uint32_t cycle) {
	const n64_tile_t& tile = m_rdp->m_tiles[tilenum];
	const uint32_t index = (tile.format << 4) | (tile.size << 2) | ((uint32_t)m_rdp->m_other_modes.en_tlut << 1) | (uint32_t)m_rdp->m_other_modes.tlut_type;

	rgbaint_t st(0, SSS, 0, SST);
	rgbaint_t maxst = shift_cycle(st, tile);
	clamp_cycle_light(st, maxst, tilenum, tile);
	mask(st, tile);

	uint32_t tbase = tile.tmem + ((tile.line * st.get_b32()) & 0x1ff);

	((this)->*(m_texel_fetch[index]))(*TEX, st.get_r32(), st.get_b32(), tbase, tile.palette);
}

void n64_texture_pipe_t::cycle_linear(color_t* TEX, color_t* prev, int32_t SSS, int32_t SST, uint32_t tilenum, uint32_t cycle) {
	const n64_tile_t& tile = m_rdp->m_tiles[tilenum];
	const uint32_t index = (tile.format << 4) | (tile.size << 2) | ((uint32_t)m_rdp->m_other_modes.en_tlut << 1) | (uint32_t)m_rdp->m_other_modes.tlut_type;

	rgbaint_t st(0, SSS, 0, SST);
	rgbaint_t maxst = shift_cycle(st, tile);

	clamp_cycle_light(st, maxst, tilenum, tile);

	mask(st, tile);

	const uint32_t tbase = tile.tmem + ((tile.line * st.get_b32()) & 0x1ff);

	rgbaint_t t0;
	((this)->*(m_texel_fetch[index]))(t0, st.get_r32(), st.get_b32(), tbase, tile.palette);
	if (m_rdp->m_other_modes.convert_one && cycle) {
		t0.set(*prev);
	}

	t0.sign_extend(0x00000100, 0xffffff00);

	rgbaint_t k1r(m_rdp->get_k1());
	k1r.mul_imm(t0.get_r32());

	TEX->set(m_rdp->get_k023());
	TEX->mul_imm(t0.get_g32());
	TEX->add(k1r);
	TEX->add_imm(0x80);
	TEX->shr_imm(8);
	TEX->add_imm(t0.get_b32());
	TEX->and_imm(0x1ff);
}

void n64_texture_pipe_t::cycle_linear_lerp(color_t* TEX, color_t* prev, int32_t SSS, int32_t SST, uint32_t tilenum, uint32_t cycle) {
	const n64_tile_t& tile = m_rdp->m_tiles[tilenum];

	uint32_t tpal = tile.palette;
	uint32_t index = (tile.format << 4) | (tile.size << 2) | ((uint32_t)m_rdp->m_other_modes.en_tlut << 1) | (uint32_t)m_rdp->m_other_modes.tlut_type;

	rgbaint_t sstt(SSS, SSS, SST, SST);
	rgbaint_t maxst = shift_cycle(sstt, tile);
	rgbaint_t stfrac = sstt;
	stfrac.and_imm(0x1f);

	clamp_cycle(sstt, stfrac, maxst, tilenum, tile);

	sstt.add(m_st2_add);

	mask(sstt, tile);

	const uint32_t tbase1 = tile.tmem + ((tile.line * sstt.get_b32()) & 0x1ff);
	const uint32_t tbase2 = tile.tmem + ((tile.line * sstt.get_g32()) & 0x1ff);

	bool upper = ((stfrac.get_r32() + stfrac.get_b32()) >= 0x20);

	rgbaint_t invstf(stfrac);
	if (upper) {
		invstf.subr_imm(0x20);
		invstf.shl_imm(3);
	}

	stfrac.shl_imm(3);

	bool center = (stfrac.get_r32() == 0x10) && (stfrac.get_b32() == 0x10) && m_rdp->m_other_modes.mid_texel;

	rgbaint_t t2;
	((this)->*(m_texel_fetch[index]))(*TEX, sstt.get_a32(), sstt.get_b32(), tbase1, tpal);
	((this)->*(m_texel_fetch[index]))(t2, sstt.get_r32(), sstt.get_g32(), tbase2, tpal);

	if (!center) {
		if (upper) {
			rgbaint_t t3;
			((this)->*(m_texel_fetch[index]))(t3, sstt.get_a32(), sstt.get_g32(), tbase2, tpal);

			TEX->sub(t3);
			t2.sub(t3);

			TEX->mul_imm(invstf.get_b32());
			t2.mul_imm(invstf.get_r32());

			TEX->add(t2);
			TEX->add_imm(0x0080);
			TEX->sra_imm(8);
			TEX->add(t3);
		} else {
			rgbaint_t t0;
			((this)->*(m_texel_fetch[index]))(t0, sstt.get_r32(), sstt.get_b32(), tbase1, tpal);

			TEX->sub(t0);
			t2.sub(t0);

			TEX->mul_imm(stfrac.get_r32());
			t2.mul_imm(stfrac.get_b32());

			TEX->add(t2);
			TEX->add_imm(0x80);
			TEX->sra_imm(8);
			TEX->add(t0);
		}
	} else {
		rgbaint_t t0, t3;
		((this)->*(m_texel_fetch[index]))(t0, sstt.get_r32(), sstt.get_b32(), tbase1, tpal);
		((this)->*(m_texel_fetch[index]))(t3, sstt.get_a32(), sstt.get_g32(), tbase2, tpal);
		TEX->add(t0);
		TEX->add(t2);
		TEX->add(t3);
		TEX->sra_imm(2);
	}
}

void n64_texture_pipe_t::copy(color_t* TEX, int32_t SSS, int32_t SST, uint32_t tilenum) {
	const n64_tile_t* tiles = m_rdp->m_tiles;
	const n64_tile_t& tile = tiles[tilenum];

	rgbaint_t st(0, SSS, 0, SST);
	shift_copy(st, tile);
	rgbaint_t stlsb(st);
	stlsb.and_imm(7);
	st.shr_imm(3);
	st.sub(rgbaint_t(0, tile.sl, 0, tile.tl));
	st.shl_imm(3);
	st.add(stlsb);
	st.sign_extend(0x00010000, 0xffff0000);
	st.shr_imm(5);
	st.and_imm(0x1fff);
	mask(st, tile);

	const uint32_t index = (tile.format << 4) | (tile.size << 2) | ((uint32_t)m_rdp->m_other_modes.en_tlut << 1) | (uint32_t)m_rdp->m_other_modes.tlut_type;
	const uint32_t tbase = tile.tmem + ((tile.line * st.get_b32()) & 0x1ff);
	((this)->*(m_texel_fetch[index]))(*TEX, st.get_r32(), st.get_b32(), tbase, tile.palette);
}

void n64_texture_pipe_t::lod_1cycle(int32_t* sss, int32_t* sst, const int32_t s, const int32_t t, const int32_t w, const int32_t dsinc, const int32_t dtinc, const int32_t dwinc) {
	const int32_t nextsw = (w + dwinc) >> 16;
	int32_t nexts = (s + dsinc) >> 16;
	int32_t nextt = (t + dtinc) >> 16;

	if (m_rdp->m_other_modes.persp_tex_en) {
		m_rdp->tc_div(nexts, nextt, nextsw, &nexts, &nextt);
	} else {
		m_rdp->tc_div_no_perspective(nexts, nextt, nextsw, &nexts, &nextt);
	}

	m_rdp->m_start_span = false;
	m_precomp_s = nexts;
	m_precomp_t = nextt;

	const int32_t lodclamp = (((*sst & 0x60000) > 0) | ((nextt & 0x60000) > 0)) || (((*sss & 0x60000) > 0) | ((nexts & 0x60000) > 0));

	int32_t horstep = SIGN17(nexts & 0x1ffff) - SIGN17(*sss & 0x1ffff);
	int32_t vertstep = SIGN17(nextt & 0x1ffff) - SIGN17(*sst & 0x1ffff);
	if (horstep & 0x20000) {
		horstep = ~horstep & 0x1ffff;
	}
	if (vertstep & 0x20000) {
		vertstep = ~vertstep & 0x1ffff;
	}

	int32_t lod = (horstep >= vertstep) ? horstep : vertstep;

	*sss = m_lod_lookup[*sss & 0x7ffff];
	*sst = m_lod_lookup[*sst & 0x7ffff];

	if ((lod & 0x4000) || lodclamp) {
		lod = 0x7fff;
	} else if (lod < m_rdp->m_misc_state.m_min_level) {
		lod = m_rdp->m_misc_state.m_min_level;
	}

	int32_t l_tile = m_rdp->get_log2((lod >> 5) & 0xff);
	const bool magnify = (lod < 32);
	const bool distant = ((lod & 0x6000) || (l_tile >= m_rdp->m_misc_state.m_max_level));

	uint8_t lod_fraction = ((lod << 3) >> l_tile) & 0xff;

	if (!m_rdp->m_other_modes.sharpen_tex_en && !m_rdp->m_other_modes.detail_tex_en) {
		if (distant) {
			lod_fraction = 0xff;
		} else if (magnify) {
			lod_fraction = 0;
		}
	}

	m_rdp->m_lod_fraction.set(lod_fraction, lod_fraction, lod_fraction, lod_fraction);
	/* FIXME: ???
	if(m_rdp->m_other_modes.sharpen_tex_en && magnify)
	{
	m_rdp->m_lod_fraction |= 0x100;
	}
	*/
}

void n64_texture_pipe_t::lod_2cycle(int32_t* sss, int32_t* sst, const int32_t s, const int32_t t, const int32_t w, const int32_t dsinc, const int32_t dtinc, const int32_t dwinc, const int32_t prim_tile, int32_t* t1, int32_t* t2) {
	const int32_t nextsw = (w + dwinc) >> 16;
	int32_t nexts = (s + dsinc) >> 16;
	int32_t nextt = (t + dtinc) >> 16;

	if (m_rdp->m_other_modes.persp_tex_en) {
		m_rdp->tc_div(nexts, nextt, nextsw, &nexts, &nextt);
	} else {
		m_rdp->tc_div_no_perspective(nexts, nextt, nextsw, &nexts, &nextt);
	}

	m_rdp->m_start_span = false;
	m_precomp_s = nexts;
	m_precomp_t = nextt;

	const int32_t lodclamp = (((*sst & 0x60000) > 0) | ((nextt & 0x60000) > 0)) || (((*sss & 0x60000) > 0) | ((nexts & 0x60000) > 0));

	int32_t horstep = SIGN17(nexts & 0x1ffff) - SIGN17(*sss & 0x1ffff);
	int32_t vertstep = SIGN17(nextt & 0x1ffff) - SIGN17(*sst & 0x1ffff);
	if (horstep & 0x20000) {
		horstep = ~horstep & 0x1ffff;
	}
	if (vertstep & 0x20000) {
		vertstep = ~vertstep & 0x1ffff;
	}

	int32_t lod = (horstep >= vertstep) ? horstep : vertstep;

	*sss = m_lod_lookup[*sss & 0x7ffff];
	*sst = m_lod_lookup[*sst & 0x7ffff];

	if ((lod & 0x4000) || lodclamp) {
		lod = 0x7fff;
	} else if (lod < m_rdp->m_misc_state.m_min_level) {
		lod = m_rdp->m_misc_state.m_min_level;
	}

	int32_t l_tile = m_rdp->get_log2((lod >> 5) & 0xff);
	const bool magnify = (lod < 32);
	const bool distant = ((lod & 0x6000) || (l_tile >= m_rdp->m_misc_state.m_max_level));

	uint8_t lod_fraction = ((lod << 3) >> l_tile) & 0xff;

	if (!m_rdp->m_other_modes.sharpen_tex_en && !m_rdp->m_other_modes.detail_tex_en) {
		if (distant) {
			lod_fraction = 0xff;
		} else if (magnify) {
			lod_fraction = 0;
		}
	}

	m_rdp->m_lod_fraction.set(lod_fraction, lod_fraction, lod_fraction, lod_fraction);

	/* FIXME: ???
	if(m_rdp->m_other_modes.sharpen_tex_en && magnify)
	{
	m_rdp->m_lod_fraction |= 0x100;
	}*/

	if (m_rdp->m_other_modes.tex_lod_en) {
		if (distant) {
			l_tile = m_rdp->m_misc_state.m_max_level;
		}
		if (!m_rdp->m_other_modes.detail_tex_en) {
			*t1 = (prim_tile + l_tile) & 7;
			if (!(distant || (!m_rdp->m_other_modes.sharpen_tex_en && magnify))) {
				*t2 = (*t1 + 1) & 7;
			} else {
				*t2 = *t1; // World Driver Championship, Stunt Race 64, Beetle Adventure Racing
			}
		} else // Beetle Adventure Racing, World Driver Championship (ingame_, NFL Blitz 2001, Pilotwings
		{
			if (!magnify) {
				*t1 = (prim_tile + l_tile + 1);
			} else {
				*t1 = (prim_tile + l_tile);
			}
			*t1 &= 7;
			if (!distant && !magnify) {
				*t2 = (prim_tile + l_tile + 2) & 7;
			} else {
				*t2 = (prim_tile + l_tile + 1) & 7;
			}
		}
	}
}

void n64_texture_pipe_t::lod_2cycle_limited(int32_t* sss, int32_t* sst, const int32_t s, const int32_t t, const int32_t w, const int32_t dsinc, const int32_t dtinc, const int32_t dwinc, const int32_t prim_tile, int32_t* t1) {
	const int32_t nextsw = (w + dwinc) >> 16;
	int32_t nexts = (s + dsinc) >> 16;
	int32_t nextt = (t + dtinc) >> 16;

	if (m_rdp->m_other_modes.persp_tex_en) {
		m_rdp->tc_div(nexts, nextt, nextsw, &nexts, &nextt);
	} else {
		m_rdp->tc_div_no_perspective(nexts, nextt, nextsw, &nexts, &nextt);
	}

	const int32_t lodclamp = (((*sst & 0x60000) > 0) | ((nextt & 0x60000) > 0)) || (((*sss & 0x60000) > 0) | ((nexts & 0x60000) > 0));

	int32_t horstep = SIGN17(nexts & 0x1ffff) - SIGN17(*sss & 0x1ffff);
	int32_t vertstep = SIGN17(nextt & 0x1ffff) - SIGN17(*sst & 0x1ffff);
	if (horstep & 0x20000) {
		horstep = ~horstep & 0x1ffff;
	}
	if (vertstep & 0x20000) {
		vertstep = ~vertstep & 0x1ffff;
	}

	int32_t lod = (horstep >= vertstep) ? horstep : vertstep;

	*sss = m_lod_lookup[*sss & 0x7ffff];
	*sst = m_lod_lookup[*sst & 0x7ffff];

	if ((lod & 0x4000) || lodclamp) {
		lod = 0x7fff;
	} else if (lod < m_rdp->m_misc_state.m_min_level) {
		lod = m_rdp->m_misc_state.m_min_level;
	}

	int32_t l_tile = m_rdp->get_log2((lod >> 5) & 0xff);
	const bool magnify = (lod < 32);
	const bool distant = (lod & 0x6000) || (l_tile >= m_rdp->m_misc_state.m_max_level);

	if (m_rdp->m_other_modes.tex_lod_en) {
		if (distant) {
			l_tile = m_rdp->m_misc_state.m_max_level;
		}
		if (!m_rdp->m_other_modes.detail_tex_en) {
			*t1 = (prim_tile + l_tile) & 7;
		} else {
			if (!magnify) {
				*t1 = (prim_tile + l_tile + 1);
			} else {
				*t1 = (prim_tile + l_tile);
			}
			*t1 &= 7;
		}
	}
}

void n64_texture_pipe_t::calculate_clamp_diffs(uint32_t prim_tile) {
	const n64_tile_t* tiles = m_rdp->m_tiles;
	if (m_rdp->m_other_modes.cycle_type == CYCLE_TYPE_2) {
		if (m_rdp->m_other_modes.tex_lod_en) {
			for (int32_t start = 0; start <= 7; start++) {
				m_clamp_diff[start].set((tiles[start].sh >> 2) - (tiles[start].sl >> 2), (tiles[start].sh >> 2) - (tiles[start].sl >> 2), (tiles[start].th >> 2) - (tiles[start].tl >> 2), (tiles[start].th >> 2) - (tiles[start].tl >> 2));
			}
		} else {
			const int32_t start = prim_tile;
			const int32_t end = (prim_tile + 1) & 7;
			m_clamp_diff[start].set((tiles[start].sh >> 2) - (tiles[start].sl >> 2), (tiles[start].sh >> 2) - (tiles[start].sl >> 2), (tiles[start].th >> 2) - (tiles[start].tl >> 2), (tiles[start].th >> 2) - (tiles[start].tl >> 2));
			m_clamp_diff[end].set((tiles[end].sh >> 2) - (tiles[end].sl >> 2), (tiles[end].sh >> 2) - (tiles[end].sl >> 2), (tiles[end].th >> 2) - (tiles[end].tl >> 2), (tiles[end].th >> 2) - (tiles[end].tl >> 2));
		}
	} else//1-cycle or copy
	{
		m_clamp_diff[prim_tile].set((tiles[prim_tile].sh >> 2) - (tiles[prim_tile].sl >> 2), (tiles[prim_tile].sh >> 2) - (tiles[prim_tile].sl >> 2), (tiles[prim_tile].th >> 2) - (tiles[prim_tile].tl >> 2), (tiles[prim_tile].th >> 2) - (tiles[prim_tile].tl >> 2));
	}
}

#define USE_64K_LUT (1)

static int32_t sTexAddrSwap16[2] = { WORD_ADDR_XOR, WORD_XOR_DWORD_SWAP };
static int32_t sTexAddrSwap8[2] = { BYTE_ADDR_XOR, BYTE_XOR_DWORD_SWAP };

void n64_texture_pipe_t::fetch_rgba16_tlut0(rgbaint_t& out, int32_t s, int32_t t, int32_t tbase, int32_t tpal) {
	int32_t taddr = (((tbase << 2) + s) ^ sTexAddrSwap16[t & 1]) & 0x7ff;

	uint16_t c = m_rdp->get_tmem16()[taddr];
	c = (m_rdp->get_tmem16() + 0x400)[(c >> 8) << 2];

#if USE_64K_LUT
	out.set(m_expand_16to32_table[c]);
#else
	out.set((c & 1) * 0xff, GET_HI_RGBA16_TMEM(c), GET_MED_RGBA16_TMEM(c), GET_LOW_RGBA16_TMEM(c));
#endif
}

void n64_texture_pipe_t::fetch_rgba16_tlut1(rgbaint_t& out, int32_t s, int32_t t, int32_t tbase, int32_t tpal) {
	const int32_t taddr = (((tbase << 2) + s) ^ sTexAddrSwap16[t & 1]) & 0x7ff;

	uint16_t c = m_rdp->get_tmem16()[taddr];
	c = (m_rdp->get_tmem16() + 0x400)[(c >> 8) << 2];

	const uint8_t k = (c >> 8) & 0xff;
	out.set(c & 0xff, k, k, k);
}

void n64_texture_pipe_t::fetch_rgba16_raw(rgbaint_t& out, int32_t s, int32_t t, int32_t tbase, int32_t tpal) {
	const int32_t taddr = (((tbase << 2) + s) ^ sTexAddrSwap16[t & 1]) & 0x7ff;

	const uint16_t c = m_rdp->get_tmem16()[taddr];

#if USE_64K_LUT
	out.set(m_expand_16to32_table[c]);
#else
	out.set((c & 1) * 0xff, GET_HI_RGBA16_TMEM(c), GET_MED_RGBA16_TMEM(c), GET_LOW_RGBA16_TMEM(c));
#endif
}

void n64_texture_pipe_t::fetch_rgba32_tlut0(rgbaint_t& out, int32_t s, int32_t t, int32_t tbase, int32_t tpal) {
	const int32_t taddr = (((tbase << 2) + s) ^ sTexAddrSwap16[t & 1]) & 0x3ff;

	uint32_t c = m_rdp->get_tmem32()[taddr];
	c = (m_rdp->get_tmem16() + 0x400)[(c >> 24) << 2];

#if USE_64K_LUT
	out.set(m_expand_16to32_table[c]);
#else
	out.set((c & 1) * 0xff, GET_HI_RGBA16_TMEM(c), GET_MED_RGBA16_TMEM(c), GET_LOW_RGBA16_TMEM(c));
#endif
}

void n64_texture_pipe_t::fetch_rgba32_tlut1(rgbaint_t& out, int32_t s, int32_t t, int32_t tbase, int32_t tpal) {
	const int32_t taddr = (((tbase << 2) + s) ^ sTexAddrSwap16[t & 1]) & 0x3ff;

	uint32_t c = m_rdp->get_tmem32()[taddr];
	c = (m_rdp->get_tmem16() + 0x400)[(c >> 24) << 2];

	const uint8_t k = (c >> 8) & 0xff;
	out.set(c & 0xff, k, k, k);
}

void n64_texture_pipe_t::fetch_rgba32_raw(rgbaint_t& out, int32_t s, int32_t t, int32_t tbase, int32_t tpal) {
	const int32_t taddr = (((tbase << 2) + s) ^ sTexAddrSwap16[t & 1]) & 0x3ff;

	const uint16_t cl = m_rdp->get_tmem16()[taddr];
	const uint16_t ch = m_rdp->get_tmem16()[taddr | 0x400];

	out.set(ch & 0xff, cl >> 8, cl & 0xff, ch >> 8);
}

void n64_texture_pipe_t::fetch_nop(rgbaint_t& out, int32_t s, int32_t t, int32_t tbase, int32_t tpal) {}

void n64_texture_pipe_t::fetch_yuv(rgbaint_t& out, int32_t s, int32_t t, int32_t tbase, int32_t tpal) {
	const int32_t taddr = (((tbase << 3) + s) ^ sTexAddrSwap8[t & 1]) & 0x7ff;
	const int32_t taddrlow = ((taddr >> 1) ^ sTexAddrSwap16[t & 1]) & 0x3ff;

	const uint16_t c = m_rdp->get_tmem16()[taddrlow];

	int32_t y = m_rdp->m_tmem.get()[taddr | 0x800];
	int32_t u = c >> 8;
	int32_t v = c & 0xff;

	v ^= 0x80; u ^= 0x80;
	u |= ((u & 0x80) << 1);
	v |= ((v & 0x80) << 1);

	out.set(y & 0xff, y & 0xff, u & 0xff, v & 0xff);
}

void n64_texture_pipe_t::fetch_ci4_tlut0(rgbaint_t& out, int32_t s, int32_t t, int32_t tbase, int32_t tpal) {
	const int32_t taddr = ((((tbase << 4) + s) >> 1) ^ sTexAddrSwap8[t & 1]) & 0x7ff;

	const uint8_t tex = m_rdp->get_tmem8()[taddr];
	const uint8_t p = (s & 1) ? (tex & 0xf) : (tex >> 4);
	const uint16_t c = (m_rdp->get_tmem16() + 0x400)[((tpal << 4) | p) << 2];

#if USE_64K_LUT
	out.set(m_expand_16to32_table[c]);
#else
	out.set((c & 1) * 0xff, GET_HI_RGBA16_TMEM(c), GET_MED_RGBA16_TMEM(c), GET_LOW_RGBA16_TMEM(c));
#endif
}

void n64_texture_pipe_t::fetch_ci4_tlut1(rgbaint_t& out, int32_t s, int32_t t, int32_t tbase, int32_t tpal) {
	int32_t taddr = ((((tbase << 4) + s) >> 1) ^ sTexAddrSwap8[t & 1]) & 0x7ff;

	const uint8_t tex = m_rdp->get_tmem8()[taddr];
	const uint8_t p = (s & 1) ? (tex & 0xf) : (tex >> 4);
	const uint16_t c = (m_rdp->get_tmem16() + 0x400)[((tpal << 4) | p) << 2];

	const uint8_t k = (c >> 8) & 0xff;
	out.set(c & 0xff, k, k, k);
}

void n64_texture_pipe_t::fetch_ci4_raw(rgbaint_t& out, int32_t s, int32_t t, int32_t tbase, int32_t tpal) {
	const int32_t taddr = ((((tbase << 4) + s) >> 1) ^ sTexAddrSwap8[t & 1]) & 0xfff;

	const uint8_t tex = m_rdp->get_tmem8()[taddr];
	uint8_t p = (s & 1) ? (tex & 0xf) : (tex >> 4);
	p = (tpal << 4) | p;

	out.set(p, p, p, p);
}

void n64_texture_pipe_t::fetch_ci8_tlut0(rgbaint_t& out, int32_t s, int32_t t, int32_t tbase, int32_t tpal) {
	const int32_t taddr = (((tbase << 3) + s) ^ sTexAddrSwap8[t & 1]) & 0x7ff;

	const uint8_t p = m_rdp->get_tmem8()[taddr];
	const uint16_t c = (m_rdp->get_tmem16() + 0x400)[p << 2];

#if USE_64K_LUT
	out.set(m_expand_16to32_table[c]);
#else
	out.set((c & 1) * 0xff, GET_HI_RGBA16_TMEM(c), GET_MED_RGBA16_TMEM(c), GET_LOW_RGBA16_TMEM(c));
#endif
}

void n64_texture_pipe_t::fetch_ci8_tlut1(rgbaint_t& out, int32_t s, int32_t t, int32_t tbase, int32_t tpal) {
	const int32_t taddr = (((tbase << 3) + s) ^ sTexAddrSwap8[t & 1]) & 0x7ff;

	const uint8_t p = m_rdp->get_tmem8()[taddr];
	const uint16_t c = (m_rdp->get_tmem16() + 0x400)[p << 2];

	const uint8_t k = (c >> 8) & 0xff;
	out.set(c & 0xff, k, k, k);
}

void n64_texture_pipe_t::fetch_ci8_raw(rgbaint_t& out, int32_t s, int32_t t, int32_t tbase, int32_t tpal) {
	const int32_t taddr = (((tbase << 3) + s) ^ sTexAddrSwap8[t & 1]) & 0xfff;

	const uint8_t p = m_rdp->get_tmem8()[taddr];
	out.set(p, p, p, p);
}

void n64_texture_pipe_t::fetch_ia4_tlut0(rgbaint_t& out, int32_t s, int32_t t, int32_t tbase, int32_t tpal) {
	const int32_t taddr = ((((tbase << 4) + s) >> 1) ^ sTexAddrSwap8[t & 1]) & 0x7ff;

	const uint8_t tex = m_rdp->get_tmem8()[taddr];
	const uint8_t p = (s & 1) ? (tex & 0xf) : (tex >> 4);
	const uint16_t c = (m_rdp->get_tmem16() + 0x400)[((tpal << 4) | p) << 2];

#if USE_64K_LUT
	out.set(m_expand_16to32_table[c]);
#else
	out.set((c & 1) * 0xff, GET_HI_RGBA16_TMEM(c), GET_MED_RGBA16_TMEM(c), GET_LOW_RGBA16_TMEM(c));
#endif
}

void n64_texture_pipe_t::fetch_ia4_tlut1(rgbaint_t& out, int32_t s, int32_t t, int32_t tbase, int32_t tpal) {
	const int32_t taddr = ((((tbase << 4) + s) >> 1) ^ sTexAddrSwap8[t & 1]) & 0x7ff;

	const uint8_t tex = m_rdp->get_tmem8()[taddr];
	const uint8_t p = (s & 1) ? (tex & 0xf) : (tex >> 4);
	const uint16_t c = (m_rdp->get_tmem16() + 0x400)[((tpal << 4) | p) << 2];

	const uint8_t k = (c >> 8) & 0xff;
	out.set(c & 0xff, k, k, k);
}

void n64_texture_pipe_t::fetch_ia4_raw(rgbaint_t& out, int32_t s, int32_t t, int32_t tbase, int32_t tpal) {
	const int32_t taddr = ((((tbase << 4) + s) >> 1) ^ sTexAddrSwap8[t & 1]) & 0xfff;

	const uint8_t tex = m_rdp->get_tmem8()[taddr];
	const uint8_t p = (s & 1) ? (tex & 0xf) : (tex >> 4);
	uint8_t i = p & 0xe;
	i = (i << 4) | (i << 1) | (i >> 2);

	out.set((p & 1) * 0xff, i, i, i);
}

void n64_texture_pipe_t::fetch_ia8_tlut0(rgbaint_t& out, int32_t s, int32_t t, int32_t tbase, int32_t tpal) {
	const int32_t taddr = (((tbase << 3) + s) ^ sTexAddrSwap8[t & 1]) & 0x7ff;

	const uint8_t p = m_rdp->get_tmem8()[taddr];
	const uint16_t c = (m_rdp->get_tmem16() + 0x400)[p << 2];

#if USE_64K_LUT
	out.set(m_expand_16to32_table[c]);
#else
	out.set((c & 1) * 0xff, GET_HI_RGBA16_TMEM(c), GET_MED_RGBA16_TMEM(c), GET_LOW_RGBA16_TMEM(c));
#endif
}

void n64_texture_pipe_t::fetch_ia8_tlut1(rgbaint_t& out, int32_t s, int32_t t, int32_t tbase, int32_t tpal) {
	const int32_t taddr = (((tbase << 3) + s) ^ sTexAddrSwap8[t & 1]) & 0x7ff;

	const uint8_t p = m_rdp->get_tmem8()[taddr];
	const uint16_t c = (m_rdp->get_tmem16() + 0x400)[p << 2];

	const uint8_t k = (c >> 8) & 0xff;
	out.set(c & 0xff, k, k, k);
}

void n64_texture_pipe_t::fetch_ia8_raw(rgbaint_t& out, int32_t s, int32_t t, int32_t tbase, int32_t tpal) {
	const int32_t taddr = (((tbase << 3) + s) ^ sTexAddrSwap8[t & 1]) & 0xfff;

	const uint8_t p = m_rdp->get_tmem8()[taddr];
	uint8_t i = p & 0xf0;
	i |= (i >> 4);

	out.set(((p << 4) | (p & 0xf)) & 0xff, i, i, i);
}

void n64_texture_pipe_t::fetch_ia16_tlut0(rgbaint_t& out, int32_t s, int32_t t, int32_t tbase, int32_t tpal) {
	const int32_t taddr = (((tbase << 2) + s) ^ sTexAddrSwap16[t & 1]) & 0x3ff;

	uint16_t c = m_rdp->get_tmem16()[taddr];
	c = (m_rdp->get_tmem16() + 0x400)[(c >> 8) << 2];

#if USE_64K_LUT
	out.set(m_expand_16to32_table[c]);
#else
	out.set((c & 1) * 0xff, GET_HI_RGBA16_TMEM(c), GET_MED_RGBA16_TMEM(c), GET_LOW_RGBA16_TMEM(c));
#endif
}

void n64_texture_pipe_t::fetch_ia16_tlut1(rgbaint_t& out, int32_t s, int32_t t, int32_t tbase, int32_t tpal) {
	const int32_t taddr = (((tbase << 2) + s) ^ sTexAddrSwap16[t & 1]) & 0x3ff;

	uint16_t c = m_rdp->get_tmem16()[taddr];
	c = (m_rdp->get_tmem16() + 0x400)[(c >> 8) << 2];

	const uint8_t k = (c >> 8) & 0xff;
	out.set(c & 0xff, k, k, k);
}

void n64_texture_pipe_t::fetch_ia16_raw(rgbaint_t& out, int32_t s, int32_t t, int32_t tbase, int32_t tpal) {
	const int32_t taddr = (((tbase << 2) + s) ^ sTexAddrSwap16[t & 1]) & 0x7ff;

	const uint16_t c = m_rdp->get_tmem16()[taddr];
	const uint8_t i = (c >> 8);
	out.set(c & 0xff, i, i, i);
}

void n64_texture_pipe_t::fetch_i4_tlut0(rgbaint_t& out, int32_t s, int32_t t, int32_t tbase, int32_t tpal) {
	const int32_t taddr = ((((tbase << 4) + s) >> 1) ^ sTexAddrSwap8[t & 1]) & 0x7ff;

	const uint8_t tex = m_rdp->get_tmem8()[taddr];
	const uint8_t c = ((s & 1)) ? (tex & 0xf) : (tex >> 4);
	const uint16_t k = (m_rdp->get_tmem16() + 0x400)[((tpal << 4) | c) << 2];

#if USE_64K_LUT
	out.set(m_expand_16to32_table[k]);
#else
	out.set((c & 1) * 0xff, GET_HI_RGBA16_TMEM(c), GET_MED_RGBA16_TMEM(c), GET_LOW_RGBA16_TMEM(c));
#endif
}

void n64_texture_pipe_t::fetch_i4_tlut1(rgbaint_t& out, int32_t s, int32_t t, int32_t tbase, int32_t tpal) {
	const int32_t taddr = ((((tbase << 4) + s) >> 1) ^ sTexAddrSwap8[t & 1]) & 0x7ff;

	const uint8_t tex = m_rdp->get_tmem8()[taddr];
	const uint8_t c = (s & 1) ? (tex & 0xf) : (tex >> 4);
	const uint16_t k = (m_rdp->get_tmem16() + 0x400)[((tpal << 4) | c) << 2];

	const uint8_t i = (k >> 8) & 0xff;
	out.set(k & 0xff, i, i, i);
}

void n64_texture_pipe_t::fetch_i4_raw(rgbaint_t& out, int32_t s, int32_t t, int32_t tbase, int32_t tpal) {
	const int32_t taddr = ((((tbase << 4) + s) >> 1) ^ sTexAddrSwap8[t & 1]) & 0xfff;

	const uint8_t tex = m_rdp->get_tmem8()[taddr];
	uint8_t c = (s & 1) ? (tex & 0xf) : (tex >> 4);
	c |= (c << 4);

	out.set(c, c, c, c);
}

void n64_texture_pipe_t::fetch_i8_tlut0(rgbaint_t& out, int32_t s, int32_t t, int32_t tbase, int32_t tpal) {
	const int32_t taddr = (((tbase << 3) + s) ^ sTexAddrSwap8[t & 1]) & 0x7ff;

	const uint8_t c = m_rdp->get_tmem8()[taddr];
	const uint16_t k = (m_rdp->get_tmem16() + 0x400)[c << 2];

#if USE_64K_LUT
	out.set(m_expand_16to32_table[k]);
#else
	out.set((c & 1) * 0xff, GET_HI_RGBA16_TMEM(c), GET_MED_RGBA16_TMEM(c), GET_LOW_RGBA16_TMEM(c));
#endif
}

void n64_texture_pipe_t::fetch_i8_tlut1(rgbaint_t& out, int32_t s, int32_t t, int32_t tbase, int32_t tpal) {
	const int32_t taddr = (((tbase << 3) + s) ^ sTexAddrSwap8[t & 1]) & 0x7ff;

	const uint8_t c = m_rdp->get_tmem8()[taddr];
	const uint16_t k = (m_rdp->get_tmem16() + 0x400)[c << 2];

	const uint8_t i = (k >> 8) & 0xff;
	out.set(k & 0xff, i, i, i);
}

void n64_texture_pipe_t::fetch_i8_raw(rgbaint_t& out, int32_t s, int32_t t, int32_t tbase, int32_t tpal) {
	const int32_t taddr = (((tbase << 3) + s) ^ sTexAddrSwap8[t & 1]) & 0xfff;

	const uint8_t c = m_rdp->get_tmem8()[taddr];

	out.set(c, c, c, c);
}