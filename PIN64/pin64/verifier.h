// license:BSD-3-Clause
// copyright-holders:Ryan Holtz
#pragma once

#ifndef PIN64_VERIFIER_H
#define PIN64_VERIFIER_H

#include <cstdint>
#include <map>

class pin64_t;
class pin64_block_t;
class pin64_data_t;

class pin64_verifier_t {
public:
	static bool verify(pin64_data_t* data);

private:
	static bool verify_headers(pin64_data_t* data);
	static bool verify_preamble(pin64_data_t* data, const uint32_t offset, const uint8_t* compare, const size_t size);
	static bool verify_data_directory(pin64_data_t* data);
	static bool verify_blocks(std::map<uint32_t, pin64_block_t*>& blocks, pin64_data_t* data);
	static bool verify_cmdlist_directory(std::map<uint32_t, pin64_block_t*>& blocks, pin64_data_t* data);
	static bool verify_cmdlist(std::map<uint32_t, pin64_block_t*>& blocks, pin64_data_t* data);
	static bool verify_metas(std::map<uint32_t, pin64_block_t*>& blocks, pin64_data_t* data);
};

#endif // PIN64_VERIFIER_H