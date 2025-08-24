#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <algorithm>

using std::string;
using std::unordered_map;
using std::vector;

// ------------------ Bitstring helpers ------------------
inline string trim01(const string& s) {
    string t; t.reserve(s.size());
    for (char c : s) if (c == '0' || c == '1') t.push_back(c);
    return t;
}

inline string u16_to_bits(uint16_t x) {
    string b(16, '0');
    for (int i = 15; i >= 0; --i) { b[15 - i] = ((x >> i) & 1) ? '1' : '0'; }
    return b;
}

inline uint16_t bits_to_u16(const string& b) {
    uint16_t v = 0;
    for (char c : b) { v = (uint16_t)((v << 1) | (c == '1')); }
    return v;
}

// ------------------ Checksum (16-bit, one's complement) ------------------
inline uint16_t checksum16_compute(const string& bits) {
    // assumes bits length is a multiple of 16
    uint32_t sum = 0;
    for (size_t i = 0; i < bits.size(); i += 16) {
        uint16_t w = bits_to_u16(bits.substr(i, 16));
        sum += w;
        // wrap-around carry
        while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    uint16_t cs = (uint16_t)~sum;
    return cs == 0 ? 0xFFFFu : cs; // avoid all-zero checksum
}

inline string checksum16_append(const string& data_bits) {
    string padded = data_bits;
    size_t rem = padded.size() % 16;
    if (rem) padded.append(16 - rem, '0');
    uint16_t cs = checksum16_compute(padded);
    return padded + u16_to_bits(cs);
}

inline bool checksum16_verify(const string& code_bits) {
    if (code_bits.empty() || (code_bits.size() % 16) != 0) return false;
    // Sum including checksum should be 0xFFFF
    uint32_t sum = 0;
    for (size_t i = 0; i < code_bits.size(); i += 16) {
        uint16_t w = bits_to_u16(code_bits.substr(i, 16));
        sum += w;
        while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    return (uint16_t)sum == 0xFFFFu;
}

// ------------------ CRC helpers ------------------
inline string xor_block(const string& a, const string& b) {
    // a and b same length
    string r; r.resize(a.size());
    for (size_t i = 0; i < a.size(); ++i) r[i] = (a[i] == b[i]) ? '0' : '1';
    return r;
}

inline string mod2_divide(const string& dividend, const string& generator) {
    // returns remainder of dividend / generator (mod 2), generator[0] == '1'
    string rem = dividend;
    size_t g = generator.size();
    for (size_t i = 0; i + g <= rem.size(); ++i) {
        if (rem[i] == '1') {
            for (size_t j = 0; j < g; ++j) {
                rem[i + j] = (rem[i + j] == generator[j]) ? '0' : '1';
            }
        }
    }
    return rem.substr(rem.size() - (g - 1));
}

inline string crc_make_codeword(const string& data_bits, const string& generator) {
    // append (g-1) zeros then divide, append remainder
    size_t rlen = generator.size() - 1;
    string padded = data_bits + string(rlen, '0');
    string rem = mod2_divide(padded, generator);
    return data_bits + rem;
}

inline bool crc_verify_codeword(const string& code_bits, const string& generator) {
    string rem = mod2_divide(code_bits, generator);
    return std::all_of(rem.begin(), rem.end(), [](char c){ return c == '0'; });
}

inline const unordered_map<string, string>& crc_generators() {
    static const unordered_map<string, string> m = {
        // standard polynomials in bit-string form with MSB = highest-degree term
        {"crc8",  "100000111"},                  // x^8  + x^2 + x + 1 (CRC-8-ATM, with leading 1)
        {"crc10", "11000110011"},                // x^10 + x^9 + x^5 + x^4 + x + 1
        {"crc16", "11000000000000101"},          // x^16 + x^15 + x^2 + 1 (CRC-16-IBM)
        {"crc32", "100000100110000010001110110110111"} // CRC-32 (IEEE 802.3)
    };
    return m;
}

inline bool is_crc_scheme(const string& s) {
    const auto& m = crc_generators();
    return m.find(s) != m.end();
}
