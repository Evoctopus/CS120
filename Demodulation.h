#pragma once


#include "Utils.h"
#include "Modulation.h"
#include "Mutex_FIFO.h"

using namespace juce;


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
    Mutex_FIFO<bool> &mac_fifo;

    std::vector<float> syncFIFO;
    std::vector<float> decodedFIFO;
    std::vector<float> chirp;
    std::vector<float> carrier;
    std::vector<float> tmp_buffer;
    std::queue<bool> decoded_bits;

    float power = 0.0f;
    int start_index = 0;
    float syncPower_localMax = 0.0f;
    enum State { SYNC, DECODE } state = SYNC;
    int frame_length = BITS_PER_FRAME;
    int frame_size = frame_length * SAMPLES_PER_BIT;
    bool finished = false;

    std::vector<float> frame_buffer;
    std::vector<float> syncPower_debug;
    std::vector<float> power_debug;
    std::vector<bool> start_index_debug;
    std::vector<float> windows;
    std::vector<float> demodulated_debug;
    std::vector<float> detected_chirp;

    int frame_detected = 0;


public:

   
	Demodulator(Mutex_FIFO<float>& Receiving_FIFO, Mutex_FIFO<bool>& MAC_FIFO) : 
		juce::Thread("Demodulator"), receiving_fifo(Receiving_FIFO), mac_fifo(MAC_FIFO)
    {
        syncFIFO.resize(PREAMBLE_LENGTH, 0.0f);
        state = SYNC;
        chirp = generateChirp();
        carrier = generateCarrierWave(SAMPLES_PER_BIT);
    }

    void writeLog(bool append = false) {

        //Decode();
		//printf("writing log...\n");
        //writeToFile(frame_buffer, "received_signal.txt", '\n', append);
        writeToFile(syncPower_debug, "sync_power.txt", '\n', append);
        //writeToFile(power_debug, "power.txt", '\n');
        //writeToFile(start_index_debug, "start_index.txt", '\n');
        //writeToFile(windows, "windows.txt", '\n');
        //writeToFile(demodulated_debug, "demodulated.txt", '\n');
        //writeToFile(detected_chirp, "detected_chirp.txt", '\n');
        //std::cout << frame_detected << std::endl;
    }

    void run() override {

        float current_sample;
        
		printf("Demodulation thread started.\n");
        while (!threadShouldExit()) {

            if (receiving_fifo.pop(current_sample)) {
                frame_buffer.push_back(current_sample);
                
                power = power * (1 - 1.0f / 64.0f) + current_sample * current_sample / 64.0f;
                power_debug.push_back(power);

                if (state == SYNC) {

                    windows.push_back(0.0f);
                    demodulated_debug.push_back(0.0f);

                    syncFIFO.erase(syncFIFO.begin());
                    syncFIFO.push_back(current_sample);

                    tmp_buffer.push_back(current_sample);
                    float syncPower = 0.0f;
                    for (int j = 0; j < PREAMBLE_LENGTH; ++j) {
                        syncPower += syncFIFO[j] * chirp[j];
                    }
					syncPower /= 100.0f;
                    syncPower_debug.push_back(syncPower);

                    if (syncPower > syncPower_localMax && syncPower > 0.6f) {
                        syncPower_localMax = syncPower;
                        start_index = time;
                        tmp_buffer.clear();
                    }
                    else if ((time - start_index > PREAMBLE_LENGTH) && (start_index != 0)) {
                        printf("Preamble detected at index %d, sync power: %f\n", start_index, syncPower_localMax);
                        syncPower_localMax = 0.0f;
                        start_index = 0;
                        std::fill(syncFIFO.begin(), syncFIFO.end(), 0.0f);
                        state = DECODE;

                        decodedFIFO.assign(tmp_buffer.begin(), tmp_buffer.end());
                        tmp_buffer.clear();

                        frame_detected++;
                    }
                }
                else if (state == DECODE) {

                    syncPower_debug.push_back(0.0f);
                    decodedFIFO.push_back(current_sample);
                    if (decodedFIFO.size() >= frame_size) {

                        std::vector<float> demodulated(frame_size);
                        for (int j = 0; j < frame_size; ++j) {
                            demodulated[j] = decodedFIFO[j] * carrier[j % SAMPLES_PER_BIT];
                        }
                        demodulated_debug.insert(demodulated_debug.end(), demodulated.begin(), demodulated.end());
                        //std::vector<float> decodeFIFO_removecarrier = smooth(demodulated, 10);

                        for (int j = 0; j < frame_length; ++j) {
                            float bit_power = std::accumulate(demodulated.begin() + j * SAMPLES_PER_BIT,
                                demodulated.begin() + (j + 1) * SAMPLES_PER_BIT, 0.0f);
                            decoded_bits.push(bit_power > 0);
                        }
                        if (decoded_bits.size() >= 1024) {
                            mac_fifo.push_batch(std::move(decoded_bits));
                            std::queue<bool> empty_queue;
                            std::swap(decoded_bits, empty_queue);
                        }
                        decodedFIFO.clear();
                        state = SYNC;
                    }
                }
                time++;
            }
        }
        mac_fifo.push_batch(decoded_bits);
        printf("Frame %d decoded\n", frame_detected);
    }

    void Decode(const std::vector<float>& data) {

        int length = data.size();
        std::vector<bool> decoded_bits;
        for (int i = 0; i < length; ++i) {

            float current_sample = data[i];
            power = power * (1 - 1.0f / 64.0f) + current_sample * current_sample / 64.0f;

            power_debug.push_back(power);
            syncPower_debug.push_back(0.0f);
            start_index_debug.push_back(0);

            if (state == SYNC) {

                windows.push_back(0.0f);
                demodulated_debug.push_back(0.0f);

                syncFIFO.erase(syncFIFO.begin());
                syncFIFO.push_back(current_sample);

                float syncPower = 0.0f;
                for (int j = 0; j < PREAMBLE_LENGTH; ++j) {
                    syncPower += syncFIFO[j] * chirp[j];
                }
                syncPower /= 100.0f;
                syncPower_debug[i] = syncPower;

                if (syncPower > syncPower_localMax && syncPower > 0.7f) {
                    syncPower_localMax = syncPower;
                    start_index = i;
                }
                else if ((i - start_index > PREAMBLE_LENGTH) && (start_index != 0)) {
                    printf("Preamble detected at index %d, sync power: %f\n", start_index, syncPower_localMax);
                    syncPower_localMax = 0.0f;
                    std::fill(syncFIFO.begin(), syncFIFO.end(), 0.0f);
                    state = DECODE;

                    start_index_debug[start_index] = 1;
                    decodedFIFO.assign(data.begin() + start_index, data.begin() + i);
                    /* detected_chirp.insert(detected_chirp.end(), frame_buffer.begin() + start_index - PREAMBLE_LENGTH, frame_buffer.begin() + start_index);*/
                    start_index = 0;
                    frame_detected++;
                }
            }
            else if (state == DECODE) {
                decodedFIFO.push_back(current_sample);
                if (decodedFIFO.size() >= frame_size) {

                    std::vector<float> demodulated(frame_size);
                    for (int j = 0; j < frame_size; ++j) {
                        demodulated[j] = decodedFIFO[j] * carrier[j % SAMPLES_PER_BIT];
                    }
                    //demodulated_debug.insert(demodulated_debug.end(), demodulated.begin(), demodulated.end());
                    //std::vector<float> decodeFIFO_removecarrier = smooth(demodulated, 10);

                    for (int j = 0; j < frame_length; ++j) {
                        float bit_power = std::accumulate(demodulated.begin() + j * SAMPLES_PER_BIT,
                            demodulated.begin() + (j + 1) * SAMPLES_PER_BIT, 0.0f);
                        decoded_bits.push_back((bit_power > 0) ? 1 : 0);
                    }
                    decodedFIFO.clear();
                    state = SYNC;
                }
            }
        }
        printf("Decoded done, find %d frames \n", frame_detected);
        writeToFile(decoded_bits, "output.txt", '0');
        writeLog();
    }
};