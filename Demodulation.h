#pragma once


#include "Utils.h"
#include "Modulation.h"
#include "Mutex_FIFO.h"

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

    int time = 0;
    Mutex_FIFO<float> &receiving_fifo;
    Mutex_FIFO<std::deque<bool>> &mac_fifo;

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


public:

   
	Demodulator(Mutex_FIFO<float>& Receiving_FIFO, Mutex_FIFO<std::deque<bool>>& MAC_FIFO) :
		juce::Thread("Demodulator"), receiving_fifo(Receiving_FIFO), mac_fifo(MAC_FIFO)
    {
        syncFIFO.resize(PREAMBLE_LENGTH, 0.0f);
        state = SYNC;
        chirp = generateChirp();
        carrier1 = generateCarrierWave(FREQUENCY1);
		carrier2 = generateCarrierWave(FREQUENCY2);
    }

    void writeLog(bool append = false) {

        
        //Decode();
		//printf("writing log...\n");
        writeToFile(frame_buffer, "received_signal.txt", '\n', append);
        writeToFile(syncPower_debug, "sync_power.txt", '\n', append);
        writeToFile(power_debug, "power.txt", '\n');
        printf("Frame %d decoded\n", frame_detected);
        //writeToFile(start_index_debug, "start_index.txt", '\n');
        //writeToFile(windows, "windows.txt", '\n');
        //writeToFile(demodulated_debug, "demodulated.txt", '\n');
        //writeToFile(detected_chirp, "detected_chirp.txt", '\n');
        //std::cout << frame_detected << std::endl;
    }

    int get_sample_length(int length) {
        return length * SAMPLES_PER_BIT;
    }

    void run() override {

        float current_sample;
        while (!threadShouldExit()) {

            if (receiving_fifo.pop(current_sample)) {
                frame_buffer.push_back(current_sample);
                
                power = power * (1 - 1.0f / 64.0f) + current_sample * current_sample / 64.0f;
                power_debug.push_back(power);

                if (state == SYNC) {

                    syncFIFO.pop_front();
                    syncFIFO.push_back(current_sample);

                    decodedFIFO.push_back(current_sample);
                    float syncPower = 0.0f;
                    for (int j = 0; j < PREAMBLE_LENGTH; ++j) {
                        syncPower += syncFIFO[j] * chirp[j];
                    }
					syncPower /= 100.0f;
                    syncPower_debug.push_back(syncPower);

                    if (syncPower > syncPower_localMax && syncPower > SYNC_POWER_BOARDER){
                        syncPower_localMax = syncPower;
                        start_index = time;
                        decodedFIFO.clear();
                    }
                    else if ((time - start_index > PREAMBLE_LENGTH / 2) && (start_index != 0)) {
                        printf("Preamble detected at index %d, sync power: %f\n", start_index, syncPower_localMax);
                        syncPower_localMax = 0.0f;
                        start_index = 0;
                        std::fill(syncFIFO.begin(), syncFIFO.end(), 0.0f);
                        state = DECODE;
                        frame_detected++;
                    }
                }
                else if (state == DECODE) {

                    syncPower_debug.push_back(0.0f);
                    decodedFIFO.push_back(current_sample);
                    size_t decoded_size = decodedFIFO.size();

                    if (decoded_size >= get_sample_length(LENGTH_BITS) && frame_length == 0) {

                        auto end = decodedFIFO.begin() + get_sample_length(LENGTH_BITS);
                        std::deque<float> length_field(decodedFIFO.begin(), end);
                        decodedFIFO.erase(decodedFIFO.begin(), end);
                        std::deque<bool> decoded_bits = extract_data_LC(length_field, LENGTH_BITS);
                        frame_length = decode_header(LENGTH_BITS, decoded_bits);
                        decoded_size -= get_sample_length(LENGTH_BITS);
                        //printf("Frame length %d\n", frame_length);
                    }
                    if (frame_length != 0 && decoded_size >= get_sample_length(frame_length)) {
                        std::deque<bool> decoded_bits = extract_data_LC(decodedFIFO, frame_length);
                        mac_fifo.push(std::move(decoded_bits));
                        decodedFIFO.clear();
                        state = SYNC;
                        frame_length = 0;
                    }
                }
                time++;
            }
        }
        writeLog();
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



    void Decode(const std::vector<float>& data) {

        int length = data.size();
        std::vector<bool> decoded_bits;
        for (int i = 0; i < length; ++i) {

            float current_sample = data[i];
            power = power * (1 - 1.0f / 64.0f) + current_sample * current_sample / 64.0f;

            power_debug.push_back(power);
            syncPower_debug.push_back(0.0f);

            if (state == SYNC) {


                syncFIFO.erase(syncFIFO.begin());
                syncFIFO.push_back(current_sample);

                decodedFIFO.push_back(current_sample);

                float syncPower = 0.0f;
                for (int j = 0; j < PREAMBLE_LENGTH; ++j) {
                    syncPower += syncFIFO[j] * chirp[j];
                }
                syncPower /= 100.0f;
                syncPower_debug[i] = syncPower;

                if (syncPower > syncPower_localMax && syncPower > SYNC_POWER_BOARDER) {
                    syncPower_localMax = syncPower;
                    start_index = i;
                    decodedFIFO.clear();
                }
                else if ((i - start_index > PREAMBLE_LENGTH / 2) && (start_index != 0)) {
                    printf("Preamble detected at index %d, sync power: %f\n", start_index, syncPower_localMax);
                    syncPower_localMax = 0.0f;
                    start_index = 0;
                    std::fill(syncFIFO.begin(), syncFIFO.end(), 0.0f);
                    state = DECODE;
                    frame_detected++;
                }
            }
            else if (state == DECODE) {
                syncPower_debug.push_back(0.0f);
                decodedFIFO.push_back(current_sample);
                size_t decoded_size = decodedFIFO.size();

                if (decoded_size >= get_sample_length(LENGTH_BITS) && frame_length == 0) {
                    auto end = decodedFIFO.begin() + get_sample_length(LENGTH_BITS);
                    std::deque<float> length_field(decodedFIFO.begin(), end);
                    decodedFIFO.erase(decodedFIFO.begin(), end);
                    printf("start decoding length\n");
                    std::deque<bool> decoded_bits = extract_data_LC(length_field, LENGTH_BITS);
                    frame_length = decode_header(LENGTH_BITS, decoded_bits);
                    decoded_size -= get_sample_length(LENGTH_BITS);
                    printf("Frame length %d\n", frame_length);
                }
                if (frame_length != 0 && decoded_size >= get_sample_length(frame_length)) {
                    std::deque<bool> decoded_bits = extract_data_LC(decodedFIFO, frame_length);
                    mac_fifo.push(std::move(decoded_bits));
                    decodedFIFO.clear();
                    state = SYNC;
                    frame_length = 0;
                }
            }
        }
        printf("Decoded done, find %d frames \n", frame_detected);
        writeLog();
    }
};