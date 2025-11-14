#pragma once

#include "Utils.h"

class CRC8 {
private:
    uint8_t polynomial;
    uint8_t table[256];

public:
    CRC8(uint8_t poly = 0x07) : polynomial(poly) {
        generateTable();
    }

    void generateTable() {
        for (int i = 0; i < 256; ++i) {
            uint8_t crc = i;
            for (int j = 0; j < 8; ++j) {
                if (crc & 0x80) {
                    crc = (crc << 1) ^ polynomial;
                }
                else {
                    crc <<= 1;
                }
                crc &= 0xFF;
            }
            table[i] = crc;
        }
    }

    uint8_t calculate(const std::deque<bool>& data) const {
        uint8_t crc = 0x00;
        size_t size = data.size();
        size_t byteCount = (size + 7) / 8;

        for (size_t i = 0; i < byteCount; ++i) {
            uint8_t byte = 0;
            for (int j = 0; j < 8; ++j) {
                size_t pos = i * 8 + j;
                if (pos < size && data[pos]) {
                    byte |= (1 << (7 - j));
                }
            }
            crc = table[crc ^ byte];
        }
        return crc;
    }

    std::deque<bool> getCRCBits(const std::deque<bool>& data) const {
        uint8_t crc = calculate(data);
        std::deque<bool> crcBits(8);
        for (int i = 0; i < 8; ++i) {
            crcBits.push_back((crc >> (7 - i)) & 1);
        }
        return crcBits;
    }
};
