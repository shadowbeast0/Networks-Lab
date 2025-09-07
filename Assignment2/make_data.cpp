#include <random>
#include <fstream>
#include <vector>

int main() {
    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> lenDist(10, 120);
    std::uniform_int_distribution<int> byteDist(0, 255);
    std::ofstream out("data.txt", std::ios::binary);

    for (int i = 1; i <= 10; ++i) {
        int L = lenDist(rng);
        std::vector<unsigned char> buf;
        buf.reserve(L);
        for (int j = 0; j < L; ++j) {
            unsigned char b;
            do { b = static_cast<unsigned char>(byteDist(rng)); } while (b == 0x0A || b == 0x0D);
            buf.push_back(b);
        }
        out.write(reinterpret_cast<const char*>(buf.data()), buf.size());
        out.put('\n');
    }
    return 0;
}
