#pragma once
#include "Utils.h"
#include "MAC.h"
#include "HammingCode.h"

using namespace juce;

#define SYNC_POWER_BOARDER 0.2f

std::vector<float> smooth(const std::vector<float>& x, int window_size) {
    if (window_size <= 1) return x;

    int length = x.size();
    std::vector<float> y(length);
    float sum = 0.0f;

    for (int i = 0; i < length; ++i) {
        if (i < window_size) {
            sum += x[i];
            y[i] = sum / (i + 1);
        }
        else {
            sum += x[i] - x[i - window_size];
            y[i] = sum / window_size;
        }
    }
    return y;
}

class Demodulator : public juce::Thread {

private:

    Log_Handlr demodulating_logger;
    CRC8 crc_handler;
    
    int time = 0;
    Mutex_FIFO<float> &receiving_fifo;
    Mutex_FIFO<std::deque<bool>> &mac_fifo;

    ThreadFlag& mac_thread_flag;
    ThreadFlag& demo_thread_flag;

    std::deque<float> syncFIFO;
    std::deque<float> decodedFIFO;

    std::vector<float> chirp;
    std::vector<float> carrier1;
    std::vector<float> carrier2;
    
    float power = 0.0f;
    int start_index = 0;
    float syncPower_localMax = 0.0f;
    enum State { SYNC, DECODE } state = SYNC;
    int frame_length = 0;

    std::vector<float> frame_buffer;
    std::vector<float> syncPower_debug;
    std::vector<float> power_debug;

    int frame_detected = 0;

    bool decode_crc(std::deque<bool>& payload) {
        int crc = decode_header(CRC_BITS, payload);
        if (crc == -1) return false;
        int crc_code = crc_handler.calculate(payload);
        return crc == crc_code;
    }

    int get_sample_length(int length) {
        return length * SAMPLES_PER_BIT;
    }

public:

   
	Demodulator(Mutex_FIFO<float>& RECEIVING_FIFO, Mutex_FIFO<std::deque<bool>>& MAC_FIFO, ThreadFlag& mac_thread_flag_, ThreadFlag& demo_thread_flag_) :
		juce::Thread("Demodulator"), receiving_fifo(RECEIVING_FIFO), mac_fifo(MAC_FIFO), mac_thread_flag(mac_thread_flag_), demo_thread_flag(demo_thread_flag_)
    {
        syncFIFO.resize(PREAMBLE_LENGTH, 0.0f);
        state = SYNC;
        chirp = generateChirp();
        carrier1 = generateCarrierWave(FREQUENCY1);
		carrier2 = generateCarrierWave(FREQUENCY2);

        demodulating_logger.init("Demodulating Log.log");
    }

    void writeLog(bool append = false) {

        writeToFile(frame_buffer, "received_signal.txt", '\n', append);
        writeToFile(syncPower_debug, "sync_power.txt", '\n', append);
        writeToFile(power_debug, "power.txt", '\n');

        //std::cout << frame_detected << std::endl;
    }

    void run() override {

        float current_sample;
        float buffer[512];
        while (!threadShouldExit()) {
            
            demo_thread_flag.sleep();
            size_t samples = receiving_fifo.pop_batch(buffer, 512);
            while (samples)
            {
                for (int i = 0; i < samples; ++i) {
                    //frame_buffer.push_back(current_sample);
                    //power = power * (1 - 1.0f / 64.0f) + current_sample * current_sample / 64.0f;
                    //power_debug.push_back(power);
                    current_sample = buffer[i];
                    if (state == SYNC) {

                        syncFIFO.pop_front();
                        syncFIFO.push_back(current_sample);

                        decodedFIFO.push_back(current_sample);
                        float syncPower = 0.0f;
                        for (int j = 0; j < PREAMBLE_LENGTH; ++j) {
                            syncPower += syncFIFO[j] * chirp[j];
                        }
                        syncPower /= 100.0f;
                        //syncPower_debug.push_back(syncPower);

                        if (syncPower > syncPower_localMax && syncPower > SYNC_POWER_BOARDER) {
                            syncPower_localMax = syncPower;
                            start_index = time;
                            decodedFIFO.clear();
                        }
                        else if ((time - start_index > PREAMBLE_LENGTH / 2) && (start_index != 0)) {
                            demodulating_logger.log_message(format("Preamble detected with sync power: ", syncPower_localMax));
                            syncPower_localMax = 0.0f;
                            start_index = 0;
                            time = 0;
                            std::fill(syncFIFO.begin(), syncFIFO.end(), 0.0f);
                            state = DECODE;
                        }
                    }
                    else if (state == DECODE) {

                        //syncPower_debug.push_back(0.0f);
                        decodedFIFO.push_back(current_sample);
                        size_t decoded_size = decodedFIFO.size();

                        if (decoded_size >= get_sample_length(LENGTH_BITS) && frame_length == 0) {

                            auto end = decodedFIFO.begin() + get_sample_length(LENGTH_BITS);
                            std::deque<float> length_field(decodedFIFO.begin(), end);
                            decodedFIFO.erase(decodedFIFO.begin(), end);
                            std::deque<bool> decoded_bits = extract_data_LC(length_field, LENGTH_BITS);
                            frame_length = decode_header(LENGTH_BITS, decoded_bits);
                            decoded_size -= get_sample_length(LENGTH_BITS);

                        }
                        if (frame_length != 0 && decoded_size >= get_sample_length(frame_length)) {
                            std::deque<bool> decoded_bits = extract_data_LC(decodedFIFO, frame_length);
                            if (decode_crc(decoded_bits)) {
                                frame_detected++;
                                demodulating_logger.log_message(format("Frame length ", frame_length - CRC_BITS));
                                //decoded_bits = hammingDecode(decoded_bits);
                                mac_fifo.push(std::move(decoded_bits));
                                mac_thread_flag.wake_up();
                            }
                            else {
                                demodulating_logger.log_message(format("The frame doesn't pass the crc test"));
                            }
                            decodedFIFO.clear();
                            state = SYNC;
                            frame_length = 0;
                        }
                    }
                    time++;
                }
                samples = receiving_fifo.pop_batch(buffer, 512);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
        demodulating_logger.log_message(format("Frame ", frame_detected, " decoded"));
        //writeLog();
    }

    std::deque<bool> extract_data_PSK(const std::deque<float>& signal, int bit_num) {

        size_t size = signal.size();
        std::vector<float> demodulated1(size);
		std::vector<float> demodulated2(size);
        for (int j = 0; j < size; ++j) {
            demodulated1[j] = signal[j] * carrier1[j % SAMPLES_PER_BIT];
			demodulated2[j] = signal[j] * carrier2[j % SAMPLES_PER_BIT];
        }
        
        std::deque<bool> decoded_bits;
        int i = 0;
        while (i < bit_num) {
            int j = i / 2;
            float bit_power = std::accumulate(demodulated1.begin() + j * SAMPLES_PER_BIT,
                demodulated1.begin() + (j + 1) * SAMPLES_PER_BIT, 0.0f);
            decoded_bits.push_back(bit_power > 0.0f);
            i++;
			if (i >= bit_num) break;
            bit_power = std::accumulate(demodulated2.begin() + j * SAMPLES_PER_BIT,
                demodulated2.begin() + (j + 1) * SAMPLES_PER_BIT, 0.0f);
			decoded_bits.push_back(bit_power > 0.0f);
			i++;
        }
        return decoded_bits;
    }

    std::deque<bool> extract_data_LC(const std::deque<float>& signal, int bit_num) {

        std::deque<bool> decoded_bits;
        for(int i = 0; i < bit_num; ++i)
        {
            float bit_power = 0.0f;
            for (int k = 0; k < SAMPLES_PER_BIT; ++k) {
                if (k < SAMPLES_PER_BIT / 2)
                    bit_power += signal[i * SAMPLES_PER_BIT + k];
                else
                    bit_power -= signal[i * SAMPLES_PER_BIT + k];
            }
            decoded_bits.push_back(bit_power > 0.0f);
        }
        return decoded_bits;
    }
};