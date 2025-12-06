#pragma once

#include "Utils.h"
#include "Modulation.h"

#define MAX_BACKOFF_COUNT 5
#define BASE_BACK_OFF 50
#define TIMEOUT_MS 200
#define MAX_RESEND 10

#define DEST_BITS 2
#define SRC_BITS 2
#define TYPE_BITS 2
#define SEQUENCE_BITS 10
#define MAC_HEADER_LENGTH DEST_BITS+SRC_BITS+TYPE_BITS+SEQUENCE_BITS
 
#define LISTENING_TIME 10

#define DATA_TYPE 0
#define ACK_TYPE 1
#define ICMP_REQUEST_TYPE 2
#define ICMP_REPLY_TYPE 3

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

	std::mutex* audio_mtx;                  // 互斥锁，保护条件变量和共享标志
	std::condition_variable* audio_cv;      // 条件变量，用于唤醒线程
	bool* audio_wakeup;        // 唤醒标志（核心：等待的“条件”）


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
		sending_logger.log_message(format("Start listening ", LISTENING_TIME, "ms"));
		auto now = start_time;

		while (std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count() <= LISTENING_TIME)
		{
			now = std::chrono::steady_clock::now();
			if (!channel_is_idle) {
				sending_logger.log_message(format("Channel Busy, Frame", frame.sequence_num));
				frame.generate_random_backoff();
				return;
			}
		}
		now = std::chrono::steady_clock::now();
		modulator.modulate(frame.data);
		frame.send_time = now;
		//std::cout << "[Sender] Send Frame " << frame.sequence_num << " Length=" << frame.data.size() << " bits" << "\n";
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

	void set_ipv4_lock(std::mutex* audio_mtx, std::condition_variable* audio_cv, bool* audio_wakeup) {
		this->audio_mtx = audio_mtx;
		this->audio_cv = audio_cv;
		this->audio_wakeup = audio_wakeup;
	}

	void send_data(const std::deque<bool>& data, int dest, int type) {

		std::lock_guard<std::mutex> lock(mtx);  // 线程安全

		std::deque<bool> frame_buffer;
		frame_buffer = data;

		encode_mac_header(frame_buffer, type, dest, LFS);


		Frame frame;
		frame.data = std::move(frame_buffer); 
		frame.resend_count = 0;
		frame.sequence_num = LFS;
		frame.retry_count = 0;

		/*auto now = std::chrono::steady_clock::now();
		frame.send_time = now;
		modulator.modulate(frame.data);*/
		
		if (!Tx_pending) {
			sending_logger.log_message(format("Sending Frame", LFS));
			current_sending_frame = frame;
			Tx_pending.store(true);
		}
		else {
			sending_logger.log_message(format("Ack not received, add Frame", LFS, " to buffer"));
			frames_buffer.push_back(std::move(frame));
		}
		
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
		update_frame_buffer();
		
	}

	void update_frame_buffer() {
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

		output_fifo.push(std::make_pair(DATA_TYPE, data));
		wake_up_upper_thread();

		printf("[Receiver] Received Frame %d\n", sequence_num);
		receiving_logger.log_message(format("Accept Frame ", sequence_num));
		send_ACK(src, sequence_num);	
		return;
	}

	void send_icmp_reply(int dst, int sequence_num) {
		std::deque<bool> frame;
		encode_mac_header(frame, ICMP_REPLY_TYPE, dst, sequence_num);
		modulator.modulate(frame);
	}

	void send_icmp_request(int dst) {
		
		std::deque<bool> data;
		send_data(data, dst, ICMP_REQUEST_TYPE);
	}

	void wake_up_upper_thread() {
		std::lock_guard<std::mutex> lock(*audio_mtx);
		*audio_wakeup = true;
		audio_cv->notify_one();
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
					int sequence_num = decode_header(SEQUENCE_BITS, receiving_buffer);
					switch (type) {
					case DATA_TYPE:
						handle_frame(receiving_buffer, sequence_num, src);
						break;
					case ACK_TYPE:
						handle_ack(sequence_num);
						break;
					case ICMP_REQUEST_TYPE:
						receiving_logger.log_message(format("Receive Request ", sequence_num));
						send_icmp_reply(src, sequence_num);
						break;
					case ICMP_REPLY_TYPE:
						update_frame_buffer();
						output_fifo.push(std::make_pair(ICMP_REPLY_TYPE, std::move(receiving_buffer)));
						receiving_logger.log_message(format("Receive Reply ", sequence_num, " Waking up Ip Handler to handle"));
						wake_up_upper_thread();
						break;
					default:
						receiving_logger.log_message(format("Unknown type ", type));
					}
							
				}
				else {
					receiving_logger.log_message(format("The frame is sent to ", dest, " not to me"));
				}
			}
		}
	}

	void encode_mac_header(std::deque<bool>& payload, int type, int dest, int sequence_num) const {
		encode_header(sequence_num, payload, SEQUENCE_BITS);
		encode_header(type, payload, TYPE_BITS);
		encode_header(src, payload, SRC_BITS);
		encode_header(dest, payload, DEST_BITS);
		return;
	}

	void send_ACK(int dest, int sequence_num) {
		std::deque<bool> ack_frame;
		encode_mac_header(ack_frame, ACK_TYPE, dest, sequence_num);
		modulator.modulate(ack_frame);
		return;
	}
};