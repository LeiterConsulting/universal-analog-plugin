#include <soup/CpproProtocol.hpp>

#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>

using namespace soup;

namespace
{
constexpr std::array<Key, 69> EXPECTED_LAYOUT = {
	KEY_NONE,
	KEY_2,             KEY_E,             KEY_R,             KEY_ENTER,
	KEY_I,             KEY_U,             KEY_Y,             KEY_T,
	KEY_RSHIFT,        KEY_H,             KEY_J,             KEY_K,
	KEY_L,             KEY_W,             KEY_Q,             KEY_TAB,
	KEY_O,             KEY_P,             KEY_BRACKET_LEFT,  KEY_BRACKET_RIGHT,
	KEY_BACKSLASH,     KEY_5,             KEY_4,             KEY_3,
	KEY_N,             KEY_B,             KEY_CAPS_LOCK,     KEY_A,
	KEY_G,             KEY_F,             KEY_D,             KEY_S,
	KEY_DEL,           KEY_INSERT,        KEY_PAGE_UP,       KEY_PAGE_DOWN,
	KEY_ARROW_UP,      KEY_ARROW_RIGHT,   KEY_ARROW_DOWN,    KEY_ARROW_LEFT,
	KEY_Z,             KEY_X,             KEY_C,             KEY_V,
	KEY_SLASH,         KEY_PERIOD,        KEY_COMMA,         KEY_M,
	KEY_6,             KEY_7,             KEY_8,             KEY_9,
	KEY_BACKSPACE,     KEY_EQUALS,        KEY_MINUS,         KEY_0,
	KEY_SPACE,         KEY_RALT,          KEY_FN,            KEY_RCTRL,
	KEY_LSHIFT,        KEY_LCTRL,         KEY_LMETA,         KEY_LALT,
	KEY_SEMICOLON,     KEY_QUOTE,         KEY_1,             KEY_ESCAPE,
};

void test_layout()
{
	static_assert(EXPECTED_LAYOUT.size() == sizeof(cp_pro::KEY_LAYOUT) / sizeof(cp_pro::KEY_LAYOUT[0]));
	std::array<bool, NUM_KEYS> seen{};
	for (size_t i = 1; i != EXPECTED_LAYOUT.size(); ++i)
	{
		assert(cp_pro::KEY_LAYOUT[i] == EXPECTED_LAYOUT[i]);
		assert(cp_pro::KEY_LAYOUT[i] != KEY_NONE);
		assert(!seen[cp_pro::KEY_LAYOUT[i]]);
		seen[cp_pro::KEY_LAYOUT[i]] = true;
	}
}

void test_command()
{
	static_assert(cp_pro::OUTPUT_REPORT_SIZE == 64);
	static_assert(sizeof(cp_pro::KEY_REPORT_REQUEST) == 4);
	assert(cp_pro::KEY_REPORT_REQUEST[0] == 0x03);
	assert(cp_pro::KEY_REPORT_REQUEST[1] == 0x02);
	assert(cp_pro::KEY_REPORT_REQUEST[2] == 0xF0);
	assert(cp_pro::KEY_REPORT_REQUEST[3] == 0x1D);
}

void test_normalization()
{
	assert(cp_pro::normalizeDistance(0) == 0);
	assert(cp_pro::normalizeDistance(1) == 6);
	assert(cp_pro::normalizeDistance(20) == 128);
	assert(cp_pro::normalizeDistance(40) == 255);
	assert(cp_pro::normalizeDistance(255) == 255);

	uint8_t previous = 0;
	for (uint16_t distance = 0; distance <= 255; ++distance)
	{
		const uint8_t value = cp_pro::normalizeDistance(static_cast<uint8_t>(distance));
		assert(value >= previous);
		previous = value;
	}
}

void test_decoding()
{
	cp_pro::DecodedKeyReport decoded{};
	std::array<uint8_t, 64> report{};
	report[0] = 4;
	report[1] = 8;
	report[2] = 3;
	report[3] = 4;
	report[5] = 32;
	assert(cp_pro::decodeKeyReport(report.data(), report.size(), decoded));
	assert(decoded.key == KEY_ENTER);
	assert(decoded.value == 204);

	report[2] = 2;
	report[5] = 0;
	assert(cp_pro::decodeKeyReport(report.data(), report.size(), decoded));
	assert(decoded.key == KEY_ENTER);
	assert(decoded.value == 0);

	report[2] = 1;
	report[3] = 68;
	report[5] = 255;
	assert(cp_pro::decodeKeyReport(report.data(), report.size(), decoded));
	assert(decoded.key == KEY_ESCAPE);
	assert(decoded.value == 255);

	report[2] = 0xF1;
	assert(!cp_pro::decodeKeyReport(report.data(), report.size(), decoded));
	report[2] = 3;
	report[3] = 69;
	assert(!cp_pro::decodeKeyReport(report.data(), report.size(), decoded));
	report[3] = 4;
	report[0] = 6;
	assert(!cp_pro::decodeKeyReport(report.data(), report.size(), decoded));
	report[0] = 4;
	assert(!cp_pro::decodeKeyReport(report.data(), 7, decoded));
}
}

int main()
{
	test_layout();
	test_command();
	test_normalization();
	test_decoding();
	std::cout << "CPPRO protocol tests passed\n";
	return 0;
}
