#pragma once

#include "Utils.h"
#include "Mutex_FIFO.h"
#include "Modulation.h"
#include "CRC.h"
#include "Logger.h"

# define MAX_BACKOFF_COUNT 5
# define BASE_BACK_OFF 50

 
class MAC : public juce::Thread
{

private:

	Log_Handlr sending_logger, receiving_logger;

	Mutex_FIFO<std::deque<bool>>& mac_fifo;
	Mutex_FIFO<std::pair<int, std::deque<bool>>>& output_fifo;
	Modulator& modulator;
	std::atomic<bool>& channel_is_idle;
	CRC8 crc_handler;
	std::mutex mtx;
	int src;

	std::thread timeout_thread;
	std::atomic<bool> stop_timeout_thread{ false };

	struct Frame {
		std::deque<bool> data; 
		std::optional<std::chrono::time_point<std::chrono::steady_clock>> send_time;
		int resend_count;
		int retry_count;
		int sequence_num;

		void generate_random_backoff() {

			std::random_device rd;
			std::mt19937 gen(rd());

			retry_count = std::min(retry_count + 1, MAX_BACKOFF_COUNT);
			int max = BASE_BACK_OFF << retry_count;
			
			std::uniform_int_distribution<> distrib(BASE_BACK_OFF, max);
			int backoff_time = distrib(gen);
			std::this_thread::sleep_for(std::chrono::milliseconds(backoff_time));
		}
	};
	int  LFS = 0;

	std::deque<Frame> frames_buffer;
	Frame current_sending_frame;
	std::atomic<bool> Tx_pending{ false };

	void check_timeouts() {

		while (!stop_timeout_thread) {
			std::this_thread::sleep_for(std::chrono::milliseconds(5)); 
			if (!Tx_pending) continue;
\
			auto now = std::chrono::steady_clock::now();
			
			Frame& frame = current_sending_frame;
			if (frame.send_time.has_value()) {
				auto time = frame.send_time.value();
				auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - time);
				if (duration.count() >= TIMEOUT_MS) {
					
					if (frame.resend_count >= MAX_RESEND) {
						std::cerr << "[Sender] Link Error\n";
						sending_logger.log_message(format("Link Error"));
						exit(-1);
					}
					frame.send_time.reset();
					frame.resend_count++;
					sending_logger.log_message(format("Resending Frame", frame.sequence_num, " for ", frame.resend_count, " times"));
					frame.generate_random_backoff();
				}
			}
			else  {
				try_to_send(frame);
			}
		}
	}

	void try_to_send(Frame& frame) {
		sending_logger.log_message(format("Want to send Frame", frame.sequence_num, " Length=", frame.data.size(), " bits"));

		auto start_time = std::chrono::steady_clock::now();
		sending_logger.log_message(format("Start listening 50 ms"));
		auto now = start_time;

		/*while (std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count() <= 50)
		{
			now = std::chrono::steady_clock::now();
			if (!channel_is_idle) {
				sending_logger.log_message(format("Channel Busy, Frame", frame.sequence_num));
				frame.generate_random_backoff();
				return;
			}
		}*/
		now = std::chrono::steady_clock::now();
		modulator.modulate(frame.data);
		frame.send_time = now;
		std::cout << "[Sender] Send Frame " << frame.sequence_num << " Length=" << frame.data.size() << " bits" << "\n";
		sending_logger.log_message(format("Channel Free, Send Frame", frame.sequence_num, " Length=", frame.data.size(), " bits"));
	}

public:

	MAC(Mutex_FIFO<std::deque<bool>>& MAC_FIFO, Mutex_FIFO<std::pair<int, std::deque<bool>>>& Output_FIFO, Modulator& Modulator, std::atomic<bool>& Channel_is_idle, int src) :
		juce::Thread("MAC"), mac_fifo(MAC_FIFO), output_fifo(Output_FIFO), modulator(Modulator), channel_is_idle(Channel_is_idle), src(src)
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

	void send_data(const std::deque<bool>& data, int dest) {

		std::lock_guard<std::mutex> lock(mtx);  // 线程安全

		std::deque<bool> frame_buffer;
		frame_buffer = data;
		encode_crc(frame_buffer);
		encode_mac_header(frame_buffer, 1, dest, LFS);


		Frame frame;
		frame.data = std::move(frame_buffer); 
		frame.resend_count = 0;
		frame.sequence_num = LFS;
		frame.retry_count = 0;

		/*auto now = std::chrono::steady_clock::now();
		frame.send_time = now;
		modulator.modulate(frame.data);*/
		
		if (!Tx_pending) {
			current_sending_frame = frame;
			Tx_pending = true;
		}
		else {
			frames_buffer.push_back(std::move(frame));
		}
		sending_logger.log_message(format("Add Frame", LFS, " to buffer"));
		LFS++;
		return;
	}

	void handle_ack(int ack_num) {
		
		std::lock_guard<std::mutex> lock(mtx);  
		sending_logger.log_message(format("Received ACK ", ack_num));
		if (ack_num != current_sending_frame.sequence_num) {
			sending_logger.log_message(format("Invalid ACK", ack_num));
			return;
		}

		if (frames_buffer.empty()) {
			Tx_pending = false;
			return;
		}
		else {
			current_sending_frame = frames_buffer.front();
			frames_buffer.pop_front();
			Tx_pending = true;
		}
	}
	
	void handle_frame(const std::deque<bool>& data, int sequence_num, int src) {
		std::lock_guard<std::mutex> lock(mtx); 

		output_fifo.push(std::make_pair(src, data));

		printf("[Receiver] Received Frame %d\n", sequence_num);
		receiving_logger.log_message(format("Accept Frame ", sequence_num));
		send_ACK(src, sequence_num);	
		return;
	}
	

	void run() override {

		timeout_thread = std::thread(&MAC::check_timeouts, this);
		std::deque<bool> receiving_buffer;

		while (!threadShouldExit()) {

			if (mac_fifo.pop(receiving_buffer) && receiving_buffer.size() >= MAC_HEADER_LENGTH)
			{
				int dest = decode_header(DEST_BITS, receiving_buffer);
				if (dest == src) {
					int src = decode_header(SRC_BITS, receiving_buffer);
					int type = decode_header(TYPE_BITS, receiving_buffer);
					int sequence_num = decode_header(FRAME_SEQUENCY_BITS, receiving_buffer);
					if (type == 1) {
						if (decode_crc(receiving_buffer)) {
							handle_frame(receiving_buffer, sequence_num, src);
						}
						else receiving_logger.log_message(format("The frame ", sequence_num," doesn't pass the crc test"));
					}
					else { 
						handle_ack(sequence_num);
					}		
				}
				else {
					receiving_logger.log_message(format("The frame is sent to ", dest, " not to me"));
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
		encode_header(src, payload, SRC_BITS);
		encode_header(dest, payload, DEST_BITS);
		return;
	}

	bool decode_crc(std::deque<bool>& payload) {
		int crc = decode_header(CRC_BITS, payload);
		if (crc == -1) return false;
		int crc_code = crc_handler.calculate(payload);
		return crc == crc_code;
	}

	void send_ACK(int dest, int sequence_num) {
		std::deque<bool> ack_frame;
		encode_mac_header(ack_frame, 0, dest, sequence_num);
		modulator.modulate(ack_frame);
		return;
	}
};