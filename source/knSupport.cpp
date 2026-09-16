// p1f support -- the one symbol the moved engines need from the utility side:
// printWithPowers. The four rep engines forward-declare the function themselves, so no
// header is needed.
//
// The superscripts are unconditional -- deliberately NOT gated on stdout being a terminal. Every
// runs\ case pipes the engine's output through PowerShell into its .log, so such a gate would mean
// a log NEVER showed a superscript: `2^9` in the file, `2⁹` only if you ran the exe by hand. Same
// type, two spellings, for a reason no reader of the log could see.
//
// Unconditional is safe ONLY because the bats set [Console]::OutputEncoding to UTF-8 -- without it
// PowerShell decodes this UTF-8 as the OEM code page and re-encodes the result, and `⁹`
// (E2 81 B9) lands in the log as CE 93 C3 BC E2 95 A3. Measured, not assumed: change one of the
// two and you must change both.
#include <string>
#include <cctype>

std::string printWithPowers(const std::string& strInput) {
	auto str = strInput.c_str();

	std::string result;

	while (*str) {
		if (*str == '^') {

			// Convert all consecutive digits to superscripts
			while (*++str && std::isdigit(static_cast<unsigned char>(*str))) {
				char digit = *str;

				switch (digit) {
				case '0': result += "\xE2\x81\xB0"; break; // ⁰
				case '1': result += "\xC2\xB9";     break; // ¹
				case '2': result += "\xC2\xB2";     break; // ²
				case '3': result += "\xC2\xB3";     break; // ³

				case '4': result += "\xE2\x81\xB4"; break; // ⁴
				case '5': result += "\xE2\x81\xB5"; break; // ⁵
				case '6': result += "\xE2\x81\xB6"; break; // ⁶
				case '7': result += "\xE2\x81\xB7"; break; // ⁷
				case '8': result += "\xE2\x81\xB8"; break; // ⁸
				case '9': result += "\xE2\x81\xB9"; break; // ⁹
				}
			}
		}
		else
			result += *str++;
	}

	return result;
}
