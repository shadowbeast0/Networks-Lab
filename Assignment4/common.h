#pragma once
#include <vector>
#include <string>
#include <stdexcept>

int next_pow2(int x);

std::vector<std::vector<int>> walsh(int n);
std::vector<int> encode_bit(int bit, const std::vector<int> &code);

int decode_bit(const std::vector<int> &chips, const std::vector<int> &code);
std::string chips_to_wire(const std::vector<int> &chips);
