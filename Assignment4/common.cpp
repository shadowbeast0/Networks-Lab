#include <vector>
#include <string>
#include <stdexcept>
#include "common.h"

int next_pow2(int x)
{
    int p = 1;
    while (p < x)
        p <<= 1;
    return p;
}

std::vector<std::vector<int>> walsh(int n)
{
    std::vector<std::vector<int>> H{{1}};
    while ((int)H.size() < n)
    {
        int m = H.size();
        std::vector<std::vector<int>> T(2 * m, std::vector<int>(2 * m));
        for (int i = 0; i < m; i++)
            for (int j = 0; j < m; j++)
            {
                T[i][j] = H[i][j];
                T[i][j + m] = H[i][j];
                T[i + m][j] = H[i][j];
                T[i + m][j + m] = -H[i][j];
            }
        H.swap(T);
    }
    return H;
}

std::vector<int> encode_bit(int bit, const std::vector<int> &code)
{
    int b = bit ? 1 : -1;
    std::vector<int> out(code.size());
    for (size_t i = 0; i < code.size(); ++i)
        out[i] = b * code[i];
    return out;
}

int decode_bit(const std::vector<int> &chips, const std::vector<int> &code)
{
    long long dp = 0;
    for (size_t i = 0; i < chips.size(); ++i)
        dp += 1LL * chips[i] * code[i];
    return (dp >= 0) ? 1 : 0;
}

std::string chips_to_wire(const std::vector<int> &chips)
{
    std::string s(chips.size(), ' ');
    for (size_t i = 0; i < chips.size(); ++i)
        s[i] = (chips[i] >= 0) ? '+' : '-';
    return s;
}
