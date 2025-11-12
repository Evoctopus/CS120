#pragma once

#include <vector>
#include "Utils.h"

using namespace juce;

#define PREAMBLE_LENGTH 440
#define BITS_PER_FRAME 200
#define SAMPLES_PER_BIT 12
#define SILENCE_LENGTH 100
#define FREQUENCY 8000

std::vector<float> generateChirp(int length, int sample_rate) {
    float cycle = 1.0 / float(SAMPLE_RATE);
    std::vector<float> Preamble(PREAMBLE_LENGTH, 0.0f);
    std::vector<float> f_p(PREAMBLE_LENGTH, 0.0f);
    std::vector<float> omega(PREAMBLE_LENGTH, 0.0f);

	double f0 = 2000.0;
    double f1 = 10000.0;
    float freq_step = (float(f1 - f0) / float(PREAMBLE_LENGTH)) * 2.0;
    f_p[0] = f0;
    f_p[PREAMBLE_LENGTH / 2] = f1;
    for (int i = 1; i < PREAMBLE_LENGTH / 2; i++)
        f_p[i] = f_p[i - 1] + freq_step;
    for (int i = PREAMBLE_LENGTH / 2 + 1; i < PREAMBLE_LENGTH; i++)
        f_p[i] = f_p[i - 1] - freq_step;
    for (int i = 1; i < PREAMBLE_LENGTH; i++)
    {
        omega[i] = omega[i - 1] + ((f_p[i] + f_p[i - 1]) / 2.0) * cycle;
    }
    for (int i = 0; i < PREAMBLE_LENGTH; i++)
        Preamble[i] = sin(2 * PI * omega[i]);
	return Preamble;
}

std::vector<float> generateSilence(int length) {
    return std::vector<float>(length, 0.0f);
}

std::vector<float> generateCarrierWave(int length) {
    std::vector<float> carrier(length);
    double omega = 2.0 * PI * FREQUENCY;
    for (int i = 0; i < length; ++i) {
        double t = static_cast<double>(i) / SAMPLE_RATE;
        carrier[i] = static_cast<float>(sin(omega * t));
    }
    return carrier;
}


std::vector<float> PSK(const std::vector<bool>& frame, const std::vector<float>& carrier) {

    int length = frame.size();
    std::vector<float> modulated_signal(length * SAMPLES_PER_BIT);
    int sample_idx = 0;
    for (int i = 0; i < length; ++i) {
        float symbol = frame[i] ? 1.0f : -1.0f;
        for (int k = 0; k < SAMPLES_PER_BIT; ++k) {
            int idx = i * SAMPLES_PER_BIT + k;
            modulated_signal[idx] = symbol * carrier[k];
        }
    }
    return modulated_signal;
}

void push_vector(std::queue<float>& q, const std::vector<float>& v) {
    for (const auto& item : v) {
        q.push(item);
    }
}

std::queue<float> modulate(const std::vector<bool>& data, int sample_rate) {

    std::vector<float> chirp = generateChirp(PREAMBLE_LENGTH, sample_rate);
    std::vector<float> silence = generateSilence(SILENCE_LENGTH);
    std::vector<float> carrier = generateCarrierWave(SAMPLES_PER_BIT);

    std::size_t total_bits = data.size();
    int frame_num = (total_bits + BITS_PER_FRAME - 1) / BITS_PER_FRAME;

    std::queue<float> output_track;
    std::vector<float> warm_up = generateCarrierWave(10000);

	push_vector(output_track, warm_up);

    auto frame_begin = data.begin();
    for (int i = 0; i < frame_num; ++i) {
        int length = BITS_PER_FRAME > total_bits ? total_bits : BITS_PER_FRAME;
        total_bits -= length;
        
		push_vector(output_track, chirp);

        std::vector<bool> frame(frame_begin, frame_begin + length);
        frame_begin += length;

        /*frame = hammingEncode(frame);
        length = frame.size();

        std::vector<bool> length_bits = dec2bin(length, LENGTH_BITS);
        frame.insert(frame.begin(), length_bits.begin(), length_bits.end());*/

        for (int i = 0; i < length; ++i) {
            float symbol = frame[i] ? 1.0f : -1.0f;
            for (int k = 0; k < SAMPLES_PER_BIT; ++k) {
                output_track.push(symbol * carrier[k]);
            }
        }
    }
    return output_track;
}

class Transmitter : public AudioIODeviceCallback {

public:

    int time = 0;
    std::queue<float> signal;
    int length;
    std::vector<float> frame_buffer;

    Transmitter(std::queue<float>& wave) {
        signal = wave;
    }
    void audioDeviceAboutToStart(AudioIODevice* device) override {}
    void audioDeviceStopped() override {
        writeToFile(frame_buffer, "received_signal.txt", '\n');
    }

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
        int numInputChannels,
        float* const* outputChannelData,
        int numOutputChannels,
        int numSamples,
        const AudioIODeviceCallbackContext& context) {

        float data;
        
        for (int i = 0; i < numSamples; i++) {
			frame_buffer.push_back(inputChannelData[0][i]);

            if (!signal.empty()) {
                data = signal.front();
                signal.pop();
            }
            else {
                data = 0.0f;
			}
            outputChannelData[0][i] = data;
        }
    }
};

