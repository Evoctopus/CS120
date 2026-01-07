#pragma once

#define NOMINMAX

#include <JuceHeader.h>
#include <vector>
#include <random>
#include <fstream>
#include <cstring>
#include <queue>
#include <chrono>
#include <format>
#include <algorithm>
#include "Mutex_FIFO.h"
#include "CRC.h"
#include "Logger.h"
#include "SharedMem.h"


#define SAMPLE_RATE 96000
#define PI acos(-1)


#define PREAMBLE_LENGTH 240
#define BITS_PER_FRAME 800
#define SAMPLES_PER_BIT 6
#define SILENCE_LENGTH 100
#define FREQUENCY1 8000
#define FREQUENCY2 16000

#define LENGTH_BITS 10


struct ThreadFlag {
	std::mutex mtx;                  // 互斥锁，保护条件变量和共享标志
	std::condition_variable cv;      // 条件变量，用于唤醒线程
	bool wakeup = false;          // 唤醒标志（核心：等待的“条件”）

	void sleep() {
		std::unique_lock<std::mutex> lock(mtx);
		wakeup = false;
		cv.wait(lock, [this]() { return wakeup; });
	}

	void wake_up() {
		if (wakeup) return;
		std::lock_guard<std::mutex> lock(mtx);
		wakeup = true;
		cv.notify_one();
	}
};


std::vector<std::string> splitString(const std::string& str, char delimiter, int count=-1) {
	std::vector<std::string> result;
	size_t start = 0;  // 子串起始位置
	size_t end = str.find(delimiter);  // 查找第一个分隔符的位置

	while (end != std::string::npos && count != 0) {  // 只要找到分隔符就继续
		// 截取[start, end)区间的子串（不包含分隔符）
		result.push_back(str.substr(start, end - start));
		start = end + 1;  // 更新起始位置为分隔符的下一个字符
		end = str.find(delimiter, start);  // 从新起始位置继续查找分隔符
		count--;
	}

	// 处理最后一段子串
	result.push_back(str.substr(start));

	return result;
}


uint64_t get_timestamp_milliseconds() {
	return std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
}

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


size_t deque_bool_to_uint8(
	const std::deque<bool>& bool_deque,
	uint8_t out_bytes[1514],
	bool big_endian = false
) {

	// 1. 参数合法性校验
	if (bool_deque.empty()) {
		fprintf(stderr, "Error: Input deque is empty\n");
		return 0;
	}
	if (out_bytes == nullptr) {
		fprintf(stderr, "Error: out_bytes is null pointer\n");
		return 0;
	}
	memset(out_bytes, 0, 1514);

	uint8_t current_byte = 0; // 临时存储当前打包的字节
	size_t bit_idx = 0;       // 当前bit在字节中的位置（0~7）
	size_t byte_idx = 0;      // 当前写入的字节索引

	for (bool bit : bool_deque) {
		if (bit) {
			if (big_endian) {
				current_byte |= (1 << (7 - bit_idx));
			}
			else {
				current_byte |= (1 << bit_idx);
			}
		}
		bit_idx++;
		if (bit_idx == 8) {
			out_bytes[byte_idx++] = current_byte;
			current_byte = 0;
			bit_idx = 0;
		}
	}
	if (bit_idx > 0) {
		out_bytes[byte_idx++] = current_byte;
	}
	return byte_idx;
}

size_t calc_uint8_to_deque_bool_len(const uint8_t in_bytes[],
	size_t in_len,
	size_t total_bits = 0) {
	if (in_bytes == nullptr || in_len == 0) return 0;
	size_t max_bits = in_len * 8;
	return (total_bits == 0 || total_bits > max_bits) ? max_bits : total_bits;
}

size_t uint8_to_deque_bool(
	const uint8_t in_bytes[],
	size_t in_len,
	std::deque<bool>& out_deque,
	size_t total_bits = 0,
	bool big_endian = false
) {
	// 1. 参数合法性校验
	if (in_bytes == nullptr || in_len == 0) {
		fprintf(stderr, "Error: Invalid input (null pointer or empty array)\n");
		out_deque.clear();
		return 0;
	}

	// 2. 清空输出队列，计算实际要拆分的 bit 数
	out_deque.clear();
	size_t actual_bits = calc_uint8_to_deque_bool_len(in_bytes, in_len, total_bits);
	if (actual_bits == 0) {
		return 0;
	}

	size_t bit_count = 0; // 已拆分的 bit 数
	// 3. 逐字节拆分 bit
	for (size_t byte_idx = 0; byte_idx < in_len && bit_count < actual_bits; byte_idx++) {
		uint8_t current_byte = in_bytes[byte_idx];
		// 逐 bit 拆分当前字节（0~7位）
		for (size_t bit_idx = 0; bit_idx < 8 && bit_count < actual_bits; bit_idx++) {
			bool bit_val = false;
			if (big_endian) {
				// 大端：bit_idx=0 → 取第7位，bit_idx=1 → 取第6位...
				bit_val = (current_byte >> (7 - bit_idx)) & 0x01;
			}
			else {
				// 小端：bit_idx=0 → 取第0位，bit_idx=1 → 取第1位...（默认）
				bit_val = (current_byte >> bit_idx) & 0x01;
			}
			out_deque.push_back(bit_val);
			bit_count++;
		}
	}
	return bit_count;
}

void print_mac(const uint8_t* mac, const char* prefix) {

	printf("%s%02x:%02x:%02x:%02x:%02x:%02x\n", prefix,
		mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

bool compare_mac(const uint8_t* mac1, const uint8_t* mac2) {
	for (int i = 0; i < 6; ++i) {
		if (mac1[i] != mac2[i]) return false;
	}
	return true;
}
