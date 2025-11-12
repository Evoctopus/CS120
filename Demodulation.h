#pragma once


#include "Utils.h"
#include "Modulation.h"

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

class Receiver : public juce::AudioIODeviceCallback {

private:
    int sampleRate;
    int time = 0;

    std::vector<float> syncFIFO;
    std::vector<float> decodedFIFO;
    std::vector<float> chirp;
    std::vector<float> carrier;
    float power = 0.0f;
    int start_index = 0;
    float syncPower_localMax = 0.0f;

    enum State { SYNC, DECODE } state = SYNC;
    int frame_length = BITS_PER_FRAME;
    int frame_size = frame_length * SAMPLES_PER_BIT;
    bool finished = false;

    std::vector<bool> decoded_bits;


    std::vector<float> syncPower_debug;
    std::vector<float> power_debug;
    std::vector<bool> start_index_debug;
    std::vector<float> windows;
    std::vector<float> demodulated_debug;
    std::vector<float> detected_chirp;

    int frame_detected = 0;


public:

    std::vector<float> frame_buffer;
    Receiver() {

        syncFIFO.resize(PREAMBLE_LENGTH, 0.0f);
        power = 0.0f;
        start_index = 0;
        syncPower_localMax = 0.0f;
        state = SYNC;
        sampleRate = 48000;
        chirp = generateChirp(PREAMBLE_LENGTH, sampleRate);
        carrier = generateCarrierWave(SAMPLES_PER_BIT);
    }

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override {
        sampleRate = device->getCurrentSampleRate();
        juce::Logger::writeToLog("Audio device started with sample rate: " + juce::String(sampleRate));
    }

    void audioDeviceStopped() override {

        Decode();

        writeToFile(frame_buffer, "received_signal.txt", '\n');
        writeToFile(syncPower_debug, "sync_power.txt", '\n');
        writeToFile(power_debug, "power.txt", '\n');
        writeToFile(start_index_debug, "start_index.txt", '\n');
        writeToFile(windows, "windows.txt", '\n');
        //writeToFile(decoded_bits, "decoded_bits.txt", '0');
        writeToFile(demodulated_debug, "demodulated.txt", '\n');
        writeToFile(detected_chirp, "detected_chirp.txt", '\n');


        std::cout << frame_detected << std::endl;
    }

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
        int numInputChannels,
        float* const* outputChannelData,
        int numOutputChannels,
        int numSamples,
        const juce::AudioIODeviceCallbackContext& context) override {

        if (finished) return;
        for (int i = 0; i < numSamples; ++i) {

            float current_sample = inputChannelData[0][i];

            frame_buffer.push_back(current_sample);
            //         power = power * (1 - 1.0f / 64.0f) + current_sample * current_sample / 64.0f;

                     //power_debug.push_back(power);
            //         syncPower_debug.push_back(0.0f);
                     //start_index_debug.push_back(0);

            //         if (state == SYNC) {
            //             
                     //	windows.push_back(0.0f);
                     //	demodulated_debug.push_back(0.0f);

                     //	syncFIFO.erase(syncFIFO.begin());
            //             syncFIFO.push_back(current_sample);

            //             float syncPower = 0.0f;
            //             for (int j = 0; j < PREAMBLE_LENGTH; ++j) {
            //                 syncPower += syncFIFO[j] * chirp[j];
            //             }
            //             syncPower = std::abs(syncPower / 100.0f); 
            //             syncPower_debug[time] = syncPower;

            //             if (syncPower > syncPower_localMax && syncPower > 0.03f) {
            //                 syncPower_localMax = syncPower;
            //                 start_index = time;
            //             }
            //             else if ((time - start_index > 480) && (start_index != 0)) {
                     //		printf("Preamble detected at index %d, sync power: %f\n", start_index, syncPower_localMax);
            //                 syncPower_localMax = 0.0f;
            //                 std::fill(syncFIFO.begin(), syncFIFO.end(), 0.0f);
            //                 state = DECODE;

            //                 int last_peak = 0;
            //                 for (auto it = start_index; it < time; it++) {
            //                     if (std::abs(frame_buffer[it]) > 0.15f && it > last_peak) {
            //                         last_peak = it;
                     //			}
            //                 }

            //                 decodedFIFO.assign(frame_buffer.begin() + last_peak, frame_buffer.begin() + time);
                     //		start_index_debug[start_index] = 1;
                     //		detected_chirp.insert(detected_chirp.end(), frame_buffer.begin() + start_index - PREAMBLE_LENGTH, frame_buffer.begin() + start_index);
            //                 
            //                 frame_detected++;
            //             }
            //         }
            //        else if (state == DECODE) {
                     //	decodedFIFO.push_back(current_sample);
            //             if (decodedFIFO.size() >= frame_size) {
            //               
            //                 std::vector<float> demodulated(frame_size);
            //                 for (int j = 0; j < frame_size; ++j) {
            //                     demodulated[j] = decodedFIFO[j] * carrier[j % sampleRate];
            //                 }
                     //		demodulated_debug.insert(demodulated_debug.end(), demodulated.begin(), demodulated.end());
            //                 std::vector<float> decodeFIFO_removecarrier = smooth(demodulated, 10);

            //                 windows.insert(windows.end(), decodeFIFO_removecarrier.begin(), decodeFIFO_removecarrier.end());
            //                 for (int j = 0; j < frame_length; ++j) {
            //                     int mid = SAMPLES_PER_BIT / 2 + j * SAMPLES_PER_BIT;
            //                     int start = mid - 10;
            //                     int end = mid + 10;
            //                     double bit_power = 0.0f;
            //                     for (int k = start; k < end; ++k) {
            //                         bit_power += decodeFIFO_removecarrier[k];
            //                     }
            //                     decoded_bits.push_back((bit_power > 0) ? 1 : 0);
            //                 }
            //                 start_index = 0;
            //                 writeToFile(decodedFIFO, "decoded_fifo.txt", '\n');
            //                 decodedFIFO.clear();
            //                 state = SYNC;
            //             }
            //             /*if (decoded_bits.size() == BITS_PER_FRAME) {
                     //		printf("Frame decoded\n");
            //                 finished = true;
                     //	}*/
                    //}
                    //time++;
        }

    }

    void Decode() {

        int length = frame_buffer.size();
        for (int i = 0; i < length; ++i) {

            float current_sample = frame_buffer[i];
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

                if (syncPower > syncPower_localMax && syncPower > 0.05f) {
                    syncPower_localMax = syncPower;
                    start_index = i;
                }
                else if ((i - start_index > PREAMBLE_LENGTH) && (start_index != 0)) {
                    printf("Preamble detected at index %d, sync power: %f\n", start_index, syncPower_localMax);
                    syncPower_localMax = 0.0f;
                    std::fill(syncFIFO.begin(), syncFIFO.end(), 0.0f);
                    state = DECODE;

                    start_index_debug[start_index] = 1;
                    decodedFIFO.assign(frame_buffer.begin() + start_index, frame_buffer.begin() + i);
                    start_index_debug[start_index] = 1;
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
                    demodulated_debug.insert(demodulated_debug.end(), demodulated.begin(), demodulated.end());
                    //std::vector<float> decodeFIFO_removecarrier = smooth(demodulated, 10);

                    for (int j = 0; j < frame_length; ++j) {
                        float bit_power = std::accumulate(demodulated.begin() + j * SAMPLES_PER_BIT,
                            demodulated.begin() + (j + 1) * SAMPLES_PER_BIT, 0.0f);
                        decoded_bits.push_back((bit_power > 0) ? 1 : 0);
                    }
                    decodedFIFO.clear();
                    state = SYNC;
                }
                if (decoded_bits.size() == 10000) {
                    printf("Frame decoded\n");
                    break;
                }
            }
        }
        writeToFile(decoded_bits, "output.txt", '0');
    }

    void writeLog() {
        writeToFile(frame_buffer, "received_signal.txt", '\n');
        writeToFile(syncPower_debug, "sync_power.txt", '\n');
        writeToFile(power_debug, "power.txt", '\n');
        writeToFile(start_index_debug, "start_index.txt", '\n');
        writeToFile(windows, "windows.txt", '\n');

        writeToFile(demodulated_debug, "demodulated.txt", '\n');
        writeToFile(detected_chirp, "detected_chirp.txt", '\n');
        std::cout << frame_detected << std::endl;
    }
};