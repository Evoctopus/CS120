#pragma once

#include "Utils.h"
#include "Mutex_FIFO.h"
#include "Modulation.h"
#include "CRC.h"

using namespace std;


class MAC : public juce::Thread
{

private:

	Mutex_FIFO<std::deque<bool>>& mac_fifo;
	Mutex_FIFO<bool>& output_fifo;
	CRC8 crc_handler;
	std::mutex mtx;

	struct Frame {
		std::deque<bool> data;  // 数据 payload
		optional<chrono::time_point<std::chrono::steady_clock>> send_time;
		optional<chrono::time_point<std::chrono::steady_clock>> last_try;
		int resend_count;
		int backoff_time;
		int sequence_num;
	};
	int  LAR = 0, LFS = 0;
	Frame sent_frames[SWS]; 
	
	std::thread timeout_thread;                        
	std::atomic<bool> stop_timeout_thread{ false };
	std::atomic<bool>& channel_is_idle;

	std::list<std::deque<bool>> ack_queue;

	struct ReceivedFrame {
		std::deque<bool> data;
		int seq_num = 0;
	};
	ReceivedFrame received_frames[RWS];
	int LFR = 0;

	Modulator& modulator;

	enum State { IDLE, RxFrame, TxFrame, TxACK, ACK_TIMEOUT } state = IDLE;

	int ack_received = 0;

	bool is_ack_valid(int ack) const {
		// 序号回绕处理：判断 ack_num 是否在 [base, next) 范围内
		return ack > LAR && ack <= LFS;
	}

	bool is_in_receive_window(int seq_num) const {
		return seq_num > LFR && seq_num <= LFR + RWS;
	}

	int generate_random_backoff() const {
		
		static std::mt19937 generator;

		std::uniform_int_distribution<int> distribution(10, 80);

		return distribution(generator);
	}

	void check_timeouts() {
		while (!stop_timeout_thread) {
			std::this_thread::sleep_for(std::chrono::milliseconds(5)); 
			std::lock_guard<std::mutex> lock(mtx);
			auto now = std::chrono::steady_clock::now();
			for (int i = LAR + 1; i <= LFS; ++i) {
				int idx = i % SWS;
				Frame& frame = sent_frames[idx];
				if (frame.send_time.has_value()) {
					auto time = frame.send_time.value();
					auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - time);
					if (duration.count() >= TIMEOUT_MS) {

						if (frame.resend_count >= MAX_RESEND) {
							std::cerr << "[发送方] 帧重传次数过多，链接错误，终止程序\n";
							exit(-1);
						}
						frame.backoff_time = 0;
						frame.send_time.reset();
						frame.last_try.reset();
						frame.resend_count++;
					}
				}
				else if (frame.last_try.has_value()) {
					auto time = frame.last_try.value();
					auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - time);
					if (duration.count() >= frame.backoff_time) {
						try_to_send(frame);
					}
				}
				else try_to_send(frame);
			}

			for (auto it = ack_queue.begin(); it != ack_queue.end(); ) { // 不在这里递增迭代器
				if (channel_is_idle) {
					modulator.modulate(*it);
					it = ack_queue.erase(it);
					debug_log(0);
					printf("Sent ACK!\n");
				}
				else {
					++it;
				}
			}

		}
	}

	void try_to_send(Frame& frame) {
		auto now = std::chrono::steady_clock::now();
		if (channel_is_idle) {
			modulator.modulate(frame.data);
			frame.send_time = now;
			debug_log(1);
			std::cout << "[发送方] 发送帧：序号=" << frame.sequence_num << "，数据长度=" << frame.data.size() << " bits" << "\n";
		}
		else {
			frame.backoff_time = generate_random_backoff();
			debug_log(1);
			std::cout << "[发送方] 发送帧：序号=" << frame.sequence_num << "... 信道拥堵，等待" << frame.backoff_time << "毫秒\n";
			frame.last_try = now;
		}
	}

public:

	MAC(Mutex_FIFO<std::deque<bool>>& MAC_FIFO, Mutex_FIFO<bool>& Output_FIFO, Modulator& Modulator, std::atomic<bool>& Channel_is_idle) :
		juce::Thread("MAC"), mac_fifo(MAC_FIFO), output_fifo(Output_FIFO), modulator(Modulator), channel_is_idle(Channel_is_idle)
	{}

	~MAC() {
		stop_timeout_thread = true;
		if (timeout_thread.joinable()) {
			timeout_thread.join();
		}
	}

	bool send_data(const std::deque<bool>& data, int dest) {

		std::lock_guard<std::mutex> lock(mtx);  // 线程安全

		if (LFS - LAR >= SWS) {
			//std::cout << "SLIDING WINDOWS FULL LOAD!\n";
			return false;
		}

		std::deque<bool> frame_buffer;
		frame_buffer = data;
		encode_mac_header(frame_buffer, 1, dest, LFS);

		LFS++;
		// 构造数据帧
		Frame frame;
		frame.data = std::move(frame_buffer);  // 复制数据（可优化为移动语义）
		frame.resend_count = 0;
		frame.backoff_time = 0;
		frame.sequence_num = LFS;

		//modulator.modulate(frame.data);
		
		int idx = LFS % SWS;
		sent_frames[idx] = std::move(frame);
		return true;
	}

	void handle_ack(int ack_num) {
		
		std::lock_guard<std::mutex> lock(mtx);  

		ack_received++;
		debug_log(1);
		std::cout << "[发送方] 收到 ACK：确认号=" << ack_num << "\n";

		// 检查 ACK 是否有效（确认号是否在已发送但未确认的范围内）
		if (!is_ack_valid(ack_num)) {
			debug_log(1);
			printf(" ACK 无效，忽略\n");
			return;
		}

		LAR = ack_num;
		debug_log(1);
		std::cout << "[发送方] 窗口滑动，新 LAR=" << LAR << "\n";
	}

	void debug_log(int type) {
		if (type == 1) {
			printf("[发送方]窗口(%d %d)：", LAR, LFS);
		}
		else printf("[接收方]窗口(%d %d)：", LFR, LFR+RWS);
	}
	
	void handle_frame(const std::deque<bool>& data, int sequence_num, int src) {
		std::lock_guard<std::mutex> lock(mtx); // 线程安全

		sequence_num++;
		debug_log(0);
		std::cout << "[接收方] 收到数据帧：序号=" << sequence_num << "，数据长度=" << data.size() << " bytes\n";

		// 1. 检查帧序号是否在接收窗口内
		if (!is_in_receive_window(sequence_num)) {
			debug_log(0);
			printf("【接收方】数据帧无效，忽略\n");
			send_ACK(src, LFR);
			return;
		}

		int idx = sequence_num % RWS;
		ReceivedFrame& frame = received_frames[idx];

		if (frame.seq_num != sequence_num) {
			frame.seq_num = sequence_num;
			frame.data.assign(data.begin(), data.end());
			int i;
			for (i = LFR + 1; i <= LFR + RWS; ++i) {
				int idx = i % RWS;
				ReceivedFrame& frame = received_frames[idx];
				if (frame.seq_num != i) {
					LFR = i - 1;
					break;
				}
				output_fifo.push_batch(frame.data);
				frame.seq_num = 0;
			}
			if (i == LFR + RWS) LFR = i;
		}

		debug_log(0);
		printf("【接收方】更新滑动窗口：LFR: %d\n", LFR);
		send_ACK(src, LFR);
		debug_log(0);
		printf("【接收方】发送ACK，确认号：%d\n", LFR);
		return;
	}
	

	void run() override {

		timeout_thread = std::thread(&MAC::check_timeouts, this);
		std::deque<bool> receiving_buffer;

		while (!threadShouldExit()) {

			if (mac_fifo.pop(receiving_buffer))
				if (decode_crc(receiving_buffer)) {
					int dest = decode_header(DEST_BITS, receiving_buffer);
					if (dest == ADDRESS)
					{
						int src = decode_header(SRC_BITS, receiving_buffer);
						int type = decode_header(TYPE_BITS, receiving_buffer);
						int sequence_num = decode_header(FRAME_SEQUENCY_BITS, receiving_buffer);
						if (type == 1) {     // Data frame
							handle_frame(receiving_buffer, sequence_num, src);
						}
						else {  //ACK frame
							handle_ack(sequence_num);
						}	
					}
					else { 
						debug_log(0);//Discarded
						printf("[接收方] 收到数据帧：目的地%d 非本机", dest);
					}
				}
				else {
					debug_log(0);
					printf("[接收方] CRC校验失败！\n");
				}
		}
	}

	void encode_crc(std::deque<bool>& payload) const {
		int crc = crc_handler.calculate(payload);
		encode_header(crc, payload, CRC_BITS);
		return;
	}

	void encode_mac_header(std::deque<bool>& payload, int type, int dest, int sequence_num) const {
		encode_header(sequence_num, payload, FRAME_SEQUENCY_BITS);
		encode_header(type, payload, TYPE_BITS);
		encode_header(ADDRESS, payload, SRC_BITS);
		encode_header(dest, payload, DEST_BITS);
		encode_crc(payload);
		return;
	}

	bool decode_crc(std::deque<bool>& payload) {
		int crc = decode_header(CRC_BITS, payload);
		int crc_code = crc_handler.calculate(payload);
		return crc == crc_code;
	}

	void send_ACK(int dest, int sequence_num) {
		std::deque<bool> ack_frame;
		encode_mac_header(ack_frame, 0, dest, sequence_num);
		ack_queue.push_back(ack_frame);
		return;
	}
};