#pragma once

#include <JuceHeader.h>
#include <vector>
#include <random>
#include <fstream>
#include <cstring>
#include <queue>
#include <chrono>



#define SAMPLE_RATE 96000
#define PI acos(-1)


#define PREAMBLE_LENGTH 240
#define BITS_PER_FRAME 200
#define SAMPLES_PER_BIT 12
#define SILENCE_LENGTH 100
#define FREQUENCY1 8000
#define FREQUENCY2 16000

#define LENGTH_BITS 8
#define LENGTH_FIELD_SIZE (LENGTH_BITS + 1) / 2 * SAMPLES_PER_BIT

/*-------------MAC---------------*/
#define DEST_BITS 1
#define SRC_BITS 1
#define TYPE_BITS 1
#define CRC_BITS 8
#define ADDRESS 1

#define FRAME_SEQUENCY_BITS 8
#define MAX_RESEND 3

#define SWS 100
#define RWS 100

#define TIMEOUT_MS 3000


std::vector<bool> dec2bin(int num, int length) {
	std::vector<bool> bits(length, false);
	for (int i = length - 1; i >= 0 && num > 0; --i) {
		bits[i] = num & 1;
		num >>= 1;
	}
	return bits;
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


template <typename T>
void print_deque(std::deque<T> q, std::string s = "") {
	std::cout << s << std::endl;
	for (const T& data : q) std::cout << data << " ";
	std::cout << std::endl;
}



int decode_header(int bit_num, std::deque<bool>& payload) {

	int result = 0;
	while (bit_num > 0) {
		result = (result << 1) | payload.front();
		payload.pop_front();
		bit_num--;
	}
	return result;
}

void encode_header(int data, std::deque<bool>& payload, int bit_num) {

	if (data >= 1 << bit_num) {
		std::cerr << "Error: data exceeds the specified bit number." << std::endl;
		return; 
	}
	while (data > 0 || bit_num > 0) {
		payload.push_front(data & 1);
		data = data >> 1; 
		bit_num--;
	}
	return;
}

