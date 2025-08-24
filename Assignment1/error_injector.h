// error_injector.h (Windows-safe)
#pragma once

// Prevent/undo Windows min/max macros breaking std::min/std::max
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <string>
#include <iostream>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <cmath>

enum class ErrorType {
    SINGLE_BIT = 0,
    TWO_ISOLATED = 1,
    ODD_ERRORS = 2,
    BURST = 3
};

inline const char* errorTypeName(ErrorType t) {
    switch (t) {
        case ErrorType::SINGLE_BIT:   return "single_bit";
        case ErrorType::TWO_ISOLATED: return "two_isolated_single_bits";
        case ErrorType::ODD_ERRORS:   return "odd_number_of_errors";
        case ErrorType::BURST:        return "burst";
    }
    return "unknown";
}

class ErrorInjector {
public:
    ErrorInjector() { std::srand((unsigned)std::time(nullptr)); }

    std::string inject(const std::string& in, ErrorType type) {
        if (in.empty()) return in;
        std::string s = in;
        int n = (int)s.size();

        auto flip = [&](int pos) {
            s[pos] = (s[pos] == '0') ? '1' : '0';
        };

        switch (type) {
            case ErrorType::SINGLE_BIT: {
                int p = std::rand() % n;
                flip(p);
                std::cerr << "[Injector] SINGLE_BIT at " << p << "\n";
                break;
            }
            case ErrorType::TWO_ISOLATED: {
                if (n < 2) { flip(0); break; }
                int p1 = std::rand() % n;
                int p2 = std::rand() % n;
                while (p2 == p1 || std::abs(p2 - p1) < 2) p2 = std::rand() % n;
                flip(p1);
                flip(p2);
                std::cerr << "[Injector] TWO_ISOLATED at " << p1 << "," << p2 << "\n";
                break;
            }
            case ErrorType::ODD_ERRORS: {
                int candidates[3] = {1, 3, 5};
                int flips = candidates[std::rand() % 3];
                std::cerr << "[Injector] ODD_ERRORS flips=" << flips << "\n";
                for (int i = 0; i < flips; ++i) flip(std::rand() % n);
                break;
            }
            case ErrorType::BURST: {
                if (n <= 3) { for (int i = 0; i < n; ++i) flip(i); break; }

                int start = std::rand() % (n - 3);
                int len   = 3 + std::rand() % 32;
                int end   = std::min(start + len, n);

                std::cerr << "[Injector] BURST window start=" << start
                          << " len=" << (end - start) << "\n";

                int flips = 3 + std::rand() % (end - start);
                std::cerr << "[Injector] No of flips: " << flips;
                std::cerr << " [Injector] Flips Pos:- ";

                for (int i = 0; i < flips; ++i) {
                    int pos = start + std::rand() % (end - start);
                    flip(pos);
                    std::cerr << pos << " ";
                }
                std::cerr << "\n";
                break;
            }
        }
        return s;
    }

    ErrorType randomType() const {
        int v = std::rand() % 4;
        return static_cast<ErrorType>(v);
    }

    ErrorType chooseType(int a) const {
        return static_cast<ErrorType>(a);
    }
};
