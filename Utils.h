#pragma once


#include <vector>
#include <random>
#include <fstream>
#include <cstring>


std::vector<bool> dec2bin(int num, int length) {
	std::vector<bool> bits(length, false);
	for (int i = length - 1; i >= 0 && num > 0; --i) {
		bits[i] = num & 1;
		num >>= 1;
	}
	return bits;
}

int bin2dec(const std::vector<bool>& bits) {
	int num = 0;
	for (bool bit : bits) {
		num = (num << 1) | bit;
	}
	return num;
}

std::vector<bool> generateRandomBits(int num_bits) {
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<int> bit_dist(0, 1);

	std::vector<bool> bits;
	bits.reserve(num_bits);

	for (int i = 0; i < num_bits; ++i) {
		bits.push_back(bit_dist(gen));
	}
	return bits;
}

void writeToFile(const std::vector<bool>& array, const std::string filename) {
	std::ofstream outfile(filename);
	for (const bool& bit : array)
		outfile << bit;
	outfile.close();
}

std::vector<bool> readFromFile(const std::string filename) {
	std::ifstream infile(filename);
	std::vector<bool> array;
	char ch;
	while (infile.get(ch)) {
		if (ch == '0')
			array.push_back(false);
		else if (ch == '1')
			array.push_back(true);
	}
	infile.close();
	return array;
}