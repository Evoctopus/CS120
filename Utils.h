#pragma once

#include <JuceHeader.h>
#include <vector>
#include <random>
#include <fstream>
#include <cstring>
#include <queue>
#include <chrono>
#include <format>


#define ADDRESS 1
#define SAMPLE_RATE 96000
#define PI acos(-1)


#define PREAMBLE_LENGTH 240
#define BITS_PER_FRAME 800
#define SAMPLES_PER_BIT 6
#define SILENCE_LENGTH 100
#define FREQUENCY1 8000
#define FREQUENCY2 16000

#define LENGTH_BITS 10

/*-------------MAC---------------*/
#define DEST_BITS 1
#define SRC_BITS 1
#define TYPE_BITS 1
#define CRC_BITS 8
#define FRAME_SEQUENCY_BITS 8

#define MAC_HEADER_LENGTH DEST_BITS+SRC_BITS+TYPE_BITS+CRC_BITS+ FRAME_SEQUENCY_BITS

#define MAX_RESEND 10
#define SWS 1
#define RWS 1
#define TIMEOUT_MS 200


std::vector<bool> dec2bin(int num, int length) {
	std::vector<bool> bits(length, false);
	for (int i = length - 1; i >= 0 && num > 0; --i) {
		bits[i] = num & 1;
		num >>= 1;
	}
	return bits;
}



std::deque<bool> generateRandomBits(int num_bits) {
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<int> bit_dist(0, 1);

	std::deque<bool> bits;

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

	if (payload.size() < bit_num)
	{
		printf("Cannot decode %d bits from %d frame\n", bit_num, payload.size());
		exit(-1);
	}

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

template<typename... Args>
std::string format(Args&&... args) {
	std::ostringstream oss;
	(oss << ... << std::forward<Args>(args));
	return oss.str();
}


void compare(const std::vector<bool>& output, const std::vector<bool>& input) {
	size_t len1 = output.size();
	size_t len2 = input.size();
	printf("Comparing output with length %d and input with length %d\n", len1, len2);

	int matched = 0;
	for (size_t i = 0; i < len2; ++i) {
		if (i >= len1) break;
		if (output[i] == input[i]) {
			matched++;
		}
	}
	printf("Matched: %d/%d\n", matched, len2);
	printf("Accuracy: %f\%\n", 1.0f * matched / len2);
}

