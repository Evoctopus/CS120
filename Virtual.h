#pragma once

#include "Utils.h"
#include "MAC.h"
#include "SharedMem.h"

#define DST_ADDRESS 1

#define ICMP_REQUEST_TYPE 2
#define ICMP_REPLY_TYPE 3
#define HTTP_REPLY_TYPE 4
#define HTTP_REQUEST_TYPE 5

#define PING_TIMEOUT_MS 2000



class Virtual : public juce::Thread
{

private:

	MAC& mac;

	Mutex_FIFO<std::pair<int, std::deque<bool>>>& virtual_fifo;
	SharedMemoryComm& comm;

	ThreadFlag& virtual_thread_flag;

	std::mutex ping_mtx_;              
	std::condition_variable ping_cv_;
	std::atomic<bool> icmp_echo_received = false;

	std::thread memory_thread;
	std::atomic<bool> stop_memory_thread{ false };

	std::ofstream html_file;

	void memory_process() {

		while (!stop_memory_thread.load()) {
			std::vector<uint8_t> data = comm.ReadData();
			if (!data.empty()) {
				std::cout << "Received " << data.size() << " bytes" << std::endl;
				std::string received_str(data.begin(), data.end());
				std::vector<std::string> tokens = splitString(received_str, ':', 2);
				if (tokens[0] == "C++") continue;

				std::string& type_str = tokens[1];
				if (type_str == "ping") {
					std::string ping_success = tokens[2];
					if (ping_success == "successful")
					{
						std::deque<bool> data;
						mac.send_data(data, DST_ADDRESS, ICMP_REPLY_TYPE);
					}
				}
				else if (type_str == "http") {
					std::vector<uint8_t> content(tokens[2].begin(), tokens[2].end());
					std::deque<bool> buffer;
					size_t len = uint8_to_deque_bool(content.data(), content.size(), buffer);
					mac.send_data(buffer, DST_ADDRESS, HTTP_REPLY_TYPE);
				}
				std::cout << "Data: " << received_str << std::endl;
			}
		}
	}

public:

	Virtual(Mutex_FIFO<std::pair<int, std::deque<bool>>>& VIRTUAL_FIFO, SharedMemoryComm& COMM, MAC& mac_, ThreadFlag& virtual_thread_flag_) :
		juce::Thread("Virtual"), virtual_fifo(VIRTUAL_FIFO), comm(COMM), mac(mac_), virtual_thread_flag(virtual_thread_flag_)
	{
		html_file.open("example.html", std::ios::out | std::ios::trunc);
	}

	~Virtual() {
		html_file.close();
		stop_memory_thread.store(true);
		comm.Close();
		if (memory_thread.joinable()) {
			memory_thread.join();
		}
	}


	void ping(const std::string& ip, int count=4) {
		std::vector<uint8_t> ping_request(ip.begin(), ip.end());
		std::deque<bool> buffer;
		size_t len = uint8_to_deque_bool(ping_request.data(), ping_request.size(), buffer);
		std::cout << "Pinging " << ip << " with " << len << " bytes of data." << std::endl;
		for (int i = 0; i < count; i++)
		{
			uint64_t send_time = get_timestamp_milliseconds();
			icmp_echo_received.store(false);
			mac.send_data(buffer, DST_ADDRESS, ICMP_REQUEST_TYPE);
			std::unique_lock<std::mutex> ulock(ping_mtx_);
			bool timeout = ping_cv_.wait_for(ulock, std::chrono::milliseconds(PING_TIMEOUT_MS),
				[this]() { return icmp_echo_received.load(); });
			if (timeout == false) {
				std::cout << "Request timed out " << ip << std::endl;
				icmp_echo_received.store(false);
				continue;
			}
			uint64_t rtt = get_timestamp_milliseconds() - send_time;
			std::cout << "Reply from " << ip << ": bytes=" << len << " time=" << rtt << std::endl;
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
		}
	}

	

	void http_get(const std::string& url, const std::string seq_num="0x12345678") {
		std::string http_string = url + ":" + seq_num;
		std::vector<uint8_t> http_request(http_string.begin(), http_string.end());
		std::deque<bool> buffer;
		size_t len = uint8_to_deque_bool(http_request.data(), http_request.size(), buffer);
		mac.send_data(buffer, DST_ADDRESS, HTTP_REQUEST_TYPE);
	}


	void run() override {

		memory_thread = std::thread(&Virtual::memory_process, this);
		std::pair<int, std::deque<bool>> receiving_buffer;

		while (!threadShouldExit()) {

			virtual_thread_flag.sleep();
			while (virtual_fifo.pop(receiving_buffer))
			{
				int type = receiving_buffer.first;
				std::deque<bool>& payload = receiving_buffer.second;

				if (type == ICMP_REQUEST_TYPE) {
					uint8_t data[1514];
					size_t len = deque_bool_to_uint8(payload, data);
					std::string ping_request = "C++:ping:";
					std::vector<uint8_t> vec_data(ping_request.begin(), ping_request.end());
					vec_data.insert(vec_data.end(), data, data + len);
					comm.WriteData(vec_data);
				}
				else if (type == HTTP_REQUEST_TYPE) {
					uint8_t data[1514];
					size_t len = deque_bool_to_uint8(payload, data);
					std::string http_request = "C++:http:";
					std::vector<uint8_t> vec_data(http_request.begin(), http_request.end());
					vec_data.insert(vec_data.end(), data, data + len);
					comm.WriteData(vec_data);
				}	
				else if (type == ICMP_REPLY_TYPE) {
					icmp_echo_received.store(true);
					ping_cv_.notify_all();
				}
				else if (type == HTTP_REPLY_TYPE) {

					char http_reply[1514];
					size_t len = deque_bool_to_uint8(payload, (uint8_t*)http_reply);
					html_file << http_reply;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		}
	}
};