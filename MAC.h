#pragma once

#include "Utils.h"
#include "Mutex_FIFO.h"
#include "Modulation.h"
#include "CRC.h"

#include "Logger.h"


class MAC : public juce::Thread
{

private:

	Log_Handlr sending_logger, receiving_logger;



	Mutex_FIFO<std::deque<bool>>& mac_fifo;
	Mutex_FIFO<bool>& output_fifo;
	CRC8 crc_handler;
	std::mutex mtx;

	struct Frame {
		std::deque<bool> data;  // 数据 payload
		std::optional<std::chrono::time_point<std::chrono::steady_clock>> send_time;
		std::optional<std::chrono::time_point<std::chrono::steady_clock>> last_try;
		int resend_count;
		int backoff_time;
		int sequence_num;
	};
	int  LAR = 0, LFS = 0;
	Frame sent_frames[SWS]; 
	
	std::thread timeout_thread;                        
	std::atomic<bool> stop_timeout_thread{ false };
	std::atomic<bool>& channel_is_idle;

	std::atomic<bool> ack_pending;
	int ack_backoff;
	std::chrono::time_point<std::chrono::steady_clock> ack_send_time;
	int DEST;

	struct ReceivedFrame {
		std::deque<bool> data;
		int seq_num = 0;
	};
	ReceivedFrame received_frames[RWS];
	int LFR = 0;

	Modulator& modulator;

	enum State { IDLE, RxFrame, TxFrame, TxACK, ACK_TIMEOUT } state = IDLE;

	int ack_received = 0;
	bool Complete = false;

	bool is_ack_valid(int ack) const {
		// 序号回绕处理：判断 ack_num 是否在 [base, next) 范围内
		return ack > LAR && ack <= LFS;
	}

	bool is_in_receive_window(int seq_num) const {
		return seq_num > LFR && seq_num <= LFR + RWS;
	}

	int generate_random_backoff(int min = 10, int max = 100) const {
		
		static std::mt19937 generator;

		std::uniform_int_distribution<int> distribution(min, max);

		return distribution(generator);
	}

	void check_timeouts() {
		ack_send_time = std::chrono::steady_clock::now();
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
						//debug_log(1);
						//printf("Resending Frame%d for %d times\n", frame.sequence_num, frame.resend_count);
						sending_logger.log_message(format("Resending Frame",frame.sequence_num, " for ", frame.resend_count, " times"));
						if (frame.resend_count >= MAX_RESEND) {
							std::cerr << "[Sender] Link Error\n";
							sending_logger.log_message(format("Link Error"));
							exit(-1);
						}
						frame.backoff_time = 0;
						frame.send_time.reset();
						frame.last_try.reset();
						frame.resend_count++;
					}
				}
				/*else if (frame.last_try.has_value()) {
					auto time = frame.last_try.value();
					auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - time);
					if (duration.count() >= frame.backoff_time) {
						try_to_send(frame);
					}
				}
				else try_to_send(frame);*/
			}

			if (ack_pending && channel_is_idle) {
				receiving_logger.log_message(format("Channel is free, send ACK", LFR));
				std::deque<bool> ack_frame;
				encode_mac_header(ack_frame, 0, DEST, LFR);
				modulator.modulate(ack_frame);
				ack_pending = false;
			}

			if (!Complete && LFR == 250) {
				Complete = true;
				printf("Received complete!\n");
			}

			/*if (DEST == -1) continue;
			auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - ack_send_time);
			if (duration.count() >= ack_backoff) {
				ack_send_time = now;
				ack_backoff = generate_random_backoff(200, 300);
				if (channel_is_idle) {
					std::deque<bool> ack_frame;
					encode_mac_header(ack_frame, 0, DEST, LFR);
					modulator.modulate(ack_frame);
					printf("Receiver[]: Sending ACK, SeqNum: %d TO %d\n", LFR, DEST);
				}
			}*/
		}
	}

	void try_to_send(Frame& frame) {
		sending_logger.log_message(format("Want to send Frame", frame.sequence_num, " Length=", frame.data.size(), " bits"));
		auto now = std::chrono::steady_clock::now();
		if (channel_is_idle) {
			modulator.modulate(frame.data);
			frame.send_time = now;
			//debug_log(1);
			//std::cout << "Channel Free, Sending Frame" << frame.sequence_num << " Length=" << frame.data.size() << " bits" << "\n";
			sending_logger.log_message(format("Channel Free, Sending Frame", frame.sequence_num, " Length=", frame.data.size(), " bits"));
		}
		else {
			frame.backoff_time = generate_random_backoff();
			//debug_log(1);
			//std::cout << "Channel Busy, Frame" << frame.sequence_num << " Waiting for" << frame.backoff_time << " ms\n";
			sending_logger.log_message(format("Channel Busy, Frame", frame.sequence_num, " Waiting for", frame.backoff_time, " ms"));
			frame.last_try = now;
		}
	}

public:

	MAC(Mutex_FIFO<std::deque<bool>>& MAC_FIFO, Mutex_FIFO<bool>& Output_FIFO, Modulator& Modulator, std::atomic<bool>& Channel_is_idle) :
		juce::Thread("MAC"), mac_fifo(MAC_FIFO), output_fifo(Output_FIFO), modulator(Modulator), channel_is_idle(Channel_is_idle)
	{
		sending_logger.init("Sender.log");
		receiving_logger.init("Receiver.log");
	}

	~MAC() {
		stop_timeout_thread = true;
		if (timeout_thread.joinable()) {
			timeout_thread.join();
		}
	}

	bool send_data(const std::deque<bool>& data, int dest) {

		std::lock_guard<std::mutex> lock(mtx);  // 线程安全

		
		if (LFS - LAR >= SWS) {
			//sending_logger.log_message(format("Sliding window full, fail to send"));
			return false;
		}
		
		std::deque<bool> frame_buffer;
		frame_buffer = data;
		encode_mac_header(frame_buffer, 1, dest, LFS);

		LFS++;
		// 构造Frame
		Frame frame;
		frame.data = std::move(frame_buffer);  // 复制数据（可优化为移动语义）
		frame.resend_count = 0;
		frame.backoff_time = 0;
		frame.sequence_num = LFS;

		auto now = std::chrono::steady_clock::now();
		frame.send_time = now;
		modulator.modulate(frame.data);
		
		int idx = LFS % SWS;
		sent_frames[idx] = std::move(frame);
		sending_logger.log_message(format("Adding Frame",LFS, " to sliding window "));
		return true;
	}

	void handle_ack(int ack_num) {
		
		std::lock_guard<std::mutex> lock(mtx);  

		ack_received++;
		// 检查 ACK 是否有效（确认号是否在已发送但未确认的范围内）
		if (!is_ack_valid(ack_num)) {
			sending_logger.log_message(format("ACK", ack_num, " Out of Range (", LAR, ", ", LFS,")"));
			return;
		}
		LAR = ack_num;
		sending_logger.log_message(format("Received ACK", ack_num, " received, updating LAR to ", LAR));
	}

	void debug_log(int type) {
		if (type == 1) {
			printf("[Sender](%d %d): ", LAR, LFS);
		}
		else printf("[Receiver](%d %d): ", LFR, LFR+RWS);
	}
	
	void handle_frame(const std::deque<bool>& data, int sequence_num, int src) {
		std::lock_guard<std::mutex> lock(mtx); // 线程安全

		sequence_num++;

		// 1. 检查帧序号是否在接收Window内
		if (!is_in_receive_window(sequence_num)) {
			receiving_logger.log_message(format("Frame", sequence_num, " out of window"));
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
				if (frame.seq_num == 0) {
					break;
				}
				output_fifo.push_batch(frame.data);
				frame.seq_num = 0;
				LFR = i;
			}
		}
		receiving_logger.log_message(format("Frame", sequence_num, " accepted, updating LFR and try to send ACK ", LFR));
		send_ACK(src, LFR);	
		return;
	}
	

	void run() override {

		timeout_thread = std::thread(&MAC::check_timeouts, this);
		std::deque<bool> receiving_buffer;

		while (!threadShouldExit()) {

			if (Complete) continue;

			if (mac_fifo.pop(receiving_buffer) && receiving_buffer.size() >= MAC_HEADER_LENGTH)
			{
				receiving_logger.log_message(format("Received a frame"));
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
						//debug_log(0);//Discarded
						//printf("Other Node's frame\n", dest);
						receiving_logger.log_message(format("The frame is sent to ", dest, " not to me"));
					}
				}
				else {
					//debug_log(0);
					//printf("CRC Failed\n");
					receiving_logger.log_message(format("The frame doesn't pass the crc test or satisfy minimum length, Discarded"));
				}
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
		//ack_pending = true;
		//DEST = dest;
		std::deque<bool> ack_frame;
		encode_mac_header(ack_frame, 0, dest, sequence_num);
		modulator.modulate(ack_frame);
		return;
	}
};