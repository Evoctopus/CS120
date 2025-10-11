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
    int frame_length = 107;
    int frame_size = frame_length * SAMPLES_PER_BIT;


public:
    Receiver() {
        
		syncFIFO.resize(PREAMBLE_LENGTH, 0.0f);
        power = 0.0f;
        start_index = 0;
        syncPower_localMax = 0.0f;
        state = SYNC;
    }

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override {
        sampleRate = device->getCurrentSampleRate();
        juce::Logger::writeToLog("Audio device started with sample rate: " + juce::String(sampleRate));
        chirp = generateChirp(PREAMBLE_LENGTH, sampleRate);
        carrier = generateCarrierWave(10000, sampleRate);
    }

    void audioDeviceStopped() override {
        juce::Logger::writeToLog("Audio device stopped.");
    }

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
        int numInputChannels,
        float* const* outputChannelData,
        int numOutputChannels,
        int numSamples,
        const juce::AudioIODeviceCallbackContext& context) override {

        for (int i = 0; i < numSamples; ++i) {

            float current_sample = inputChannelData[0][i];
            power = power * (1 - 1.0f / 64.0f) + current_sample * current_sample / 64.0f;
            decodedFIFO.push_back(current_sample);

            if (state == SYNC) {
                
				syncFIFO.erase(syncFIFO.begin());
                syncFIFO.push_back(current_sample);

                float syncPower = 0.0f;
                for (int j = 0; j < PREAMBLE_LENGTH; ++j) {
                    syncPower += syncFIFO[j] * chirp[j];
                }
                syncPower /= 200.0f; 

                if (syncPower > 0.05f) {
					printf("Sync power: %f, signal power: %f\n", syncPower, power);
                }

                if (syncPower > power * 2 && syncPower > syncPower_localMax && syncPower > 0.05f) {
                    syncPower_localMax = syncPower;
                    start_index = time + i;
                    decodedFIFO.clear();
                }
                else if ((time + i - start_index > 200) && (start_index != 0)) {
					printf("Preamble detected at index %d, sync power: %f\n", start_index, syncPower_localMax);
                    syncPower_localMax = 0.0f;
                    state = DECODE;
                }
            }
            else if (state == DECODE) {
                
                if (decodedFIFO.size() == frame_size) {
                  
                    std::vector<float> demodulated(frame_size);
                    for (int j = 0; j < frame_size; ++j) {
                        demodulated[j] = decodedFIFO[j] * carrier[j];
                    }
                    std::vector<float> decodeFIFO_removecarrier = smooth(demodulated, 10);

                    std::vector<int> decoded_bits(frame_length);
                    for (int j = 0; j < frame_length; ++j) {
                        int start = 10 + j * SAMPLES_PER_BIT;
                        int end = 30 + j * SAMPLES_PER_BIT;
                        double bit_power = 0.0f;
                        for (int k = start; k < end; ++k) {
                            bit_power += decodeFIFO_removecarrier[k];
                        }
                        decoded_bits[j] = (bit_power > 0) ? 1 : 0;
                    }
                    start_index = 0;
                    decodedFIFO.clear();
                    state = SYNC;
                }
            }
        }
		time += numSamples;
    }
};