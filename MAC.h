#pragma once

#include "Utils.h"
#include "Modulation.h"

#define MAX_BACKOFF_COUNT 5
#define BASE_BACK_OFF 50
#define TIMEOUT_MS 200
#define MAX_RESEND 10

#define DEST_BITS 2
#define SRC_BITS 2
#define SEQUENCE_BITS 8
#define TYPE_BITS 3
#define MAC_HEADER_LENGTH DEST_BITS+SRC_BITS+TYPE_BITS+SEQUENCE_BITS
 
#define LISTENING_TIME 10

#define DATA_TYPE 0
#define ACK_TYPE 1


class MAC : public juce::Thread
{

private:

	Log_Handlr sending_logger, receiving_logger;

	Mutex_FIFO<std::deque<bool>>& mac_fifo;
	Mutex_FIFO<std::pair<int, std::deque<bool>>>& output_fifo;
	Modulator& modulator;
	std::atomic<bool>& channel_is_idle;

	std::mutex mtx;
	int local_mac;

	std::thread timeout_thread;
	std::atomic<bool> stop_timeout_thread{ false };

	ThreadFlag& mac_thread_flag;
	ThreadFlag& audio_thread_flag;


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
	int LFS = 0;
	int expected_seq = 0;

	std::deque<Frame> frames_buffer;
	Frame current_sending_frame;
	std::atomic<bool> Tx_pending{ false };

	void check_timeouts() {

		while (!stop_timeout_thread) {
			std::this_thread::sleep_for(std::chrono::milliseconds(5)); 
			if (!Tx_pending) continue;
			
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

	MAC(Mutex_FIFO<std::deque<bool>>& MAC_FIFO, 
		Mutex_FIFO<std::pair<int, std::deque<bool>>>& INTER_FIFO, 
		Modulator& modulator_, 
		std::atomic<bool>& channel_is_idle_, 
		ThreadFlag& mac_thread_flag_,
		ThreadFlag& audio_thread_flag_) :
		juce::Thread("MAC"), mac_fifo(MAC_FIFO), output_fifo(INTER_FIFO), modulator(modulator_), channel_is_idle(channel_is_idle_),
		mac_thread_flag(mac_thread_flag_), audio_thread_flag(audio_thread_flag_)
	{
		sending_logger.init("Sender.log");
		receiving_logger.init("Receiver.log");
		printf("Your local audio MAC address: ");
		scanf("%d", &local_mac);
	}

	~MAC() {
		stop_timeout_thread = true;
		if (timeout_thread.joinable()) {
			timeout_thread.join();
		}
	}

	void send_data(const std::deque<bool>& data, int dest, int type)
	{	
		sending_logger.log_message(format("Preparing to send data of length ", data.size(), " bits to MAC ", dest, " of type ", type));
		if (data.size() == 0) add_to_queue(std::deque<bool>(), dest, type);
		auto frame_begin = data.begin();
		while (frame_begin < data.end())
		{
			auto remaining_bits = std::distance(frame_begin, data.end());
			size_t frame_size = static_cast<size_t>(std::min(remaining_bits, static_cast<decltype(remaining_bits)>(BITS_PER_FRAME)));
			auto frame_end = frame_begin;
			std::advance(frame_end, frame_size);
			std::deque<bool> frame(frame_begin, frame_end);
			add_to_queue(frame, dest, type);
			frame_begin = frame_end;
		}
	}
	
	void add_to_queue(std::deque<bool>& frame_buffer, int dest, int type) {

		encode_mac_header(frame_buffer, type, dest, LFS);
		Frame frame;
		frame.data = std::move(frame_buffer);
		frame.resend_count = 0;
		frame.sequence_num = LFS;
		frame.retry_count = 0;

		sending_logger.log_message(format("Adding Frame", LFS, " to queue"));

		/*auto now = std::chrono::steady_clock::now();
		frame.send_time = now;
		modulator.modulate(frame.data);*/

		if (!Tx_pending) {
			current_sending_frame = frame;
			Tx_pending.store(true);
		}
		else {
			frames_buffer.push_back(std::move(frame));
		}
		LFS++;
		return;
	}

	void update_frame_buffer(int seq) {
		
		if (seq != current_sending_frame.sequence_num) return;
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
	

	void run() override {

		timeout_thread = std::thread(&MAC::check_timeouts, this);
		std::deque<bool> receiving_buffer;

		while (!threadShouldExit()) {

			mac_thread_flag.sleep();
			while (mac_fifo.pop(receiving_buffer))
			{
				if (receiving_buffer.size() < MAC_HEADER_LENGTH) continue;
				int dest = decode_header(DEST_BITS, receiving_buffer);
				if (dest == local_mac) {
					int src = decode_header(SRC_BITS, receiving_buffer);
					int seq = decode_header(SEQUENCE_BITS, receiving_buffer);
					int type = decode_header(TYPE_BITS, receiving_buffer);
					if (type == ACK_TYPE) {
						sending_logger.log_message(format("Received ACK ", seq));
						update_frame_buffer(seq);
					}
					else {
						if (seq != expected_seq) {
							send_ACK(src, seq);
							receiving_logger.log_message(format("Expected Seq ", expected_seq, " but got ", seq));
							continue;
						}
						else {
							expected_seq++;
						}
						receiving_logger.log_message(format("Received Frame",seq, ": type", type));
						//printf("Received Frame%d\n", seq);
						output_fifo.push(std::make_pair(type, std::move(receiving_buffer)));
						send_ACK(src, seq);
						audio_thread_flag.wake_up();
					}
				}
				else {
					receiving_logger.log_message(format("The frame is sent to ", dest, " not to me"));
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		}
	}

	void encode_mac_header(std::deque<bool>& payload, int type, int dest, int seq) const {
		encode_header(type, payload, TYPE_BITS);
		encode_header(seq, payload, SEQUENCE_BITS);
		encode_header(local_mac, payload, SRC_BITS);
		encode_header(dest, payload, DEST_BITS);
		return;
	}

	void send_ACK(int dest, int seq) {
		receiving_logger.log_message(format("Send ACK ", seq));
		std::deque<bool> ack_frame;
		encode_mac_header(ack_frame, ACK_TYPE, dest, seq);
		modulator.modulate(ack_frame);
		return;
	}
};