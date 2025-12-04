#pragma once

#include <JuceHeader.h>
#include <vector>
#include <random>
#include <fstream>
#include <cstring>
#include <queue>
#include <chrono>
#include <format>
#include <algorithm>


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
#define DEST_BITS 2
#define SRC_BITS 2
#define TYPE_BITS 1
#define CRC_BITS 8
#define FRAME_SEQUENCY_BITS 8

#define MAC_HEADER_LENGTH DEST_BITS + SRC_BITS + TYPE_BITS + FRAME_SEQUENCY_BITS
#define MAX_RESEND 20
#define SWS 1
#define RWS 1
#define TIMEOUT_MS 200

std::vector<bool> readBinFile(const std::string& filename) {
	// 以二进制模式打开文件
	std::ifstream inFile(filename, std::ios::in | std::ios::binary);
	if (!inFile.is_open()) {
		std::cerr << "Error: Cannot open FILE " << filename << std::endl;
		return {};
	}

	// 获取文件大小
	inFile.seekg(0, std::ios::end);
	std::streampos fileSize = inFile.tellg();
	inFile.seekg(0, std::ios::beg);

	// 预先分配 vector 空间，提高效率
	std::vector<bool> data;
	data.reserve(static_cast<size_t>(fileSize) * 8);

	char byte;
	while (inFile.read(&byte, sizeof(byte))) {
		// 从字节中逐一提取 bit（从高位到低位）
		for (int i = 7; i >= 0; --i) {
			bool bit = (byte >> i) & 1;
			data.push_back(bit);
		}
	}

	inFile.close();
	std::cout << "Successfully read " << data.size() << " from FILE " << filename <<  std::endl;
	return data;
}

void writeBinFile(const std::string& filename, const std::vector<bool>& data) {
	// 以二进制模式打开文件，若文件不存在则创建，若存在则覆盖
	std::ofstream outFile(filename, std::ios::out | std::ios::binary);
	if (!outFile.is_open()) {
		std::cerr << "Error: Cannot open FILE " << filename << std::endl;
		return;
	}

	char byte = 0;
	for (size_t i = 0; i < data.size(); ++i) {
		// 将当前 bit 写入 byte 的对应位置（从高位到低位）
		byte |= (data[i] ? 1 : 0) << (7 - (i % 8));

		// 每 8 个 bit 组成一个 byte，写入文件
		if ((i + 1) % 8 == 0) {
			outFile.write(&byte, sizeof(byte));
			byte = 0; // 重置 byte，准备下一个
		}
	}

	// 处理最后不足 8 位的部分
	if (data.size() % 8 != 0) {
		outFile.write(&byte, sizeof(byte));
	}

	outFile.close();
	std::cout << "Successfully write " << data.size() << " bits to FILE " << filename << std::endl;
	return;
}



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



uint64_t decode_header(int bit_num, std::deque<bool>& payload) {

	if (payload.size() < bit_num)
	{
		return -1;
	}

	uint64_t result = 0;
	while (bit_num > 0) {
		result = (result << 1) | payload.front();
		payload.pop_front();
		bit_num--;
	}
	return result;
}

void encode_header(uint64_t data, std::deque<bool>& payload, int bit_num) {

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
	printf("Accuracy: %f\%\n", 100.0f * matched / len2);
}


int mac_str_to_uint8(const char* mac_str, uint8_t out_mac[6]) {
	// 1. 空指针/空字符串校验
	if (mac_str == nullptr || strlen(mac_str) == 0) {
		fprintf(stderr, "Error: MAC string is empty\n");
		return 1;
	}

	char clean_mac[13] = { 0 }; // 存储去除分隔符后的纯16进制字符（6字节=12个16进制位）
	int clean_idx = 0;
	int len = strlen(mac_str);

	// 2. 遍历字符串，去除分隔符（-/:），提取纯16进制字符
	for (int i = 0; i < len; i++) {
		char c = mac_str[i];
		// 跳过分隔符（- 或 :）
		if (c == '-' || c == ':') {
			continue;
		}
		// 校验字符是否为合法16进制（0-9, a-f, A-F）
		if (!isxdigit(static_cast<unsigned char>(c))) {
			fprintf(stderr, "Error: Invalid character '%c' in MAC string\n", c);
			return 3;
		}
		// 超出12个字符（6字节），直接报错
		if (clean_idx >= 12) {
			fprintf(stderr, "Error: MAC string is too long\n");
			return 4;
		}
		clean_mac[clean_idx++] = c;
	}

	// 3. 校验去除分隔符后的长度是否为12（6字节）
	if (clean_idx != 12) {
		fprintf(stderr, "Error: MAC string length invalid (need 12 hex chars after remove separators)\n");
		return 4;
	}

	// 4. 将12个16进制字符转为6个uint8_t（每2个字符=1字节）
	for (int i = 0; i < 6; i++) {
		// 截取2个字符（如 "74" → 0x74）
		char hex_byte[3] = { 0 };
		strncpy(hex_byte, &clean_mac[i * 2], 2);
		// 16进制字符串转整数（支持大小写）
		char* end_ptr;
		long val = strtol(hex_byte, &end_ptr, 16);
		// 二次校验（防止转换失败）
		if (end_ptr != hex_byte + 2 || val < 0 || val > 255) {
			fprintf(stderr, "Error: Failed to convert '%s' to byte\n", hex_byte);
			return 2;
		}
		out_mac[i] = static_cast<uint8_t>(val);
	}

	return 0;
}

