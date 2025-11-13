#pragma once

#include <JuceHeader.h>
#include <vector>
#include <random>
#include <fstream>
#include <cstring>
#include <queue>



#define SAMPLE_RATE 96000
#define PI acos(-1)


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

template <typename T>
void writeToFile(const std::vector<T>& array, const std::string filename, char connect = '0', bool append=false){
	
	std::ios_base::openmode mode = append ? std::ios_base::app : std::ios_base::out;
	std::ofstream outfile(filename, mode);;
	for (const auto& bit : array) {
		outfile << bit;
		if (connect != '0') outfile << connect;
	}
	outfile.close();
}

inline void writeLog(float message, const std::string& filename = "log.txt") {
	std::ofstream logfile(filename, std::ios::app); // 追加模式
	if (!logfile.is_open()) return;

	logfile << message << std::endl;
	logfile.close();
}

std::vector<bool> readFromFile(const std::string filename, char ignore = '2') {
	std::ifstream infile(filename);
	std::vector<bool> array;
	char ch;
	while (infile.get(ch)) {
		if (ch == ignore) continue;	
		if (ch == '0')
			array.push_back(false);
		else if (ch == '1')
			array.push_back(true);
	}
	infile.close();
	return array;
}

template <typename T1, typename T2>
auto dot_product(const std::vector<T1>& a, const std::vector<T2>& b) -> decltype(T1()* T2()) {

	if (a.size() != b.size()) {
		throw std::invalid_argument("dot_product: Vectors must be of the same size.");
	}

	using ResultType = decltype(T1()* T2());
	ResultType result = ResultType();

	for (size_t i = 0; i < a.size(); ++i) {
		result += a[i] * b[i];
	}

	return result;
}

template <typename T>
std::vector<T> queue_to_vector(std::queue<T> q) {
	std::vector<T> v;
	v.reserve(q.size());
	while (!q.empty()) {
		v.push_back(q.front());
		q.pop();
	}
	return v;
}


