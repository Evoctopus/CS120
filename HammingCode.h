#pragma once

#include "Utils.h"

int calculateParityBits(int data_bits) {
    int r = 1;
    while ((1 << r) < data_bits + r + 1) {
        r++;
    }
    return r;
}

bool isPowerOfTwo(int n) {
    return (n > 0) && ((n & (n - 1)) == 0);
}

std::deque<bool> hammingEncode(const std::deque<bool>& data) {
    int k = data.size();
    if (k <= 0) return {};

    int r = calculateParityBits(k);
    int total_bits = k + r;

    std::deque<bool> encoded(total_bits, 0);

    int data_idx = 0;
    for (int i = 0; i < total_bits; ++i) {
        int pos = i + 1;
        if (!isPowerOfTwo(pos)) {
            encoded[i] = data[data_idx++];
        }
    }

    for (int i = 0; i < r; ++i) {
        int p_pos = 1 << i;
        int p_bit = 0;

        for (int j = p_pos; j < total_bits; ++j) {
            int pos = j + 1;
            if (pos & p_pos) {
                p_bit ^= encoded[j];
            }
        }
        encoded[p_pos - 1] = p_bit;
    }
    return encoded;
}


std::deque<bool> hammingDecode(const std::deque<bool>& encoded_data) {

    int total_bits = encoded_data.size();
    if (total_bits <= 0) {
        return {};
    }

    int r = 0;
    while ((1 << r) < total_bits + 1) {
        r++;
    }
    int k = total_bits - r;

    int error_pos = 0;
    for (int i = 0; i < r; ++i) {
        int p_pos = 1 << i;
        int calculated_p = 0;
        for (int j = p_pos; j <= total_bits; ++j) {
            if (j & p_pos) {
                calculated_p ^= encoded_data[j - 1];
            }
        }
        if (calculated_p != 0) {
            error_pos += p_pos;
        }
    }

    std::deque<bool> decoded_data = encoded_data;
    if (error_pos != 0) {
        decoded_data[error_pos - 1] = !decoded_data[error_pos - 1];
    }

    std::deque<bool> original_data;
    for (int i = 0; i < total_bits; ++i) {
        if (!isPowerOfTwo(i + 1)) {
            original_data.push_back(decoded_data[i]);
        }
    }
    return original_data;
}