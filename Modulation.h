#pragma once

#include <vector>
#include "Utils.h"

using namespace juce;

#define PREAMBLE_LENGTH 480
#define BITS_PER_FRAME 100
#define SAMPLES_PER_BIT 48
#define SILENCE_LENGTH 100
#define LENGTH_BITS 16

std::vector<float> generateChirp(int length, int sample_rate) {
    std::vector<float> preamble(length);
    double current_phase = 0.0;
    double freq_start = 2000.0;
    double freq_end = 10000.0;

    double freq_sweep_rate = (freq_end - freq_start) / (length / 2.0);

    for (int i = 0; i < length; ++i) {
        double current_freq;
        if (i < length / 2) {
            current_freq = freq_start + i * freq_sweep_rate;
        }
        else {
            current_freq = freq_end - (i - length / 2) * freq_sweep_rate;
        }

        current_phase += 2.0 * PI * current_freq / sample_rate;
        while (current_phase > 2.0 * PI) current_phase -= 2.0 * PI;
        while (current_phase < -2.0 * PI) current_phase += 2.0 * PI;
        preamble[i] = static_cast<float>(sin(current_phase));
    }
    return preamble;
}

std::vector<float> generateSilence(int length) {
    return std::vector<float>(length, 0.0f);
}

std::vector<float> generateCarrierWave(int frequecy, int sample_rate) {
    std::vector<float> carrier(sample_rate);
    double omega = 2.0 * PI * frequecy;
    for (int i = 0; i < sample_rate; ++i) {
        double t = static_cast<double>(i) / sample_rate;
        carrier[i] = static_cast<float>(sin(omega * t));
    }
    return carrier;
}

std::vector<float> modulate(const std::vector<bool>& data, int sample_rate) {

    std::vector<float> chirp = generateChirp(PREAMBLE_LENGTH, sample_rate);
    std::vector<float> silence = generateSilence(SILENCE_LENGTH);
    std::vector<float> carrier = generateCarrierWave(10000, sample_rate);

    std::size_t total_bits = data.size();
    int frame_num = (total_bits + BITS_PER_FRAME - 1) / BITS_PER_FRAME;

    std::vector<float> output_track;
    output_track.reserve(frame_num * (PREAMBLE_LENGTH + SILENCE_LENGTH) + total_bits * SAMPLES_PER_BIT);


    auto frame_begin = data.begin();
    for (int i = 0; i < frame_num; ++i) {
        int length = BITS_PER_FRAME > total_bits ? total_bits : BITS_PER_FRAME;
        total_bits -= length;

        output_track.insert(output_track.end(), chirp.begin(), chirp.end());

        std::vector<bool> frame(frame_begin, frame_begin + length);
        frame_begin += length;

        /*frame = hammingEncode(frame);
        length = frame.size();

        std::vector<bool> length_bits = dec2bin(length, LENGTH_BITS);
        frame.insert(frame.begin(), length_bits.begin(), length_bits.end());*/

        int sample_idx = 0;
        for (bool bit : frame) {
            float symbol = bit ? 1.0f : -1.0f;
            for (int k = 0; k < SAMPLES_PER_BIT; ++k) {
                output_track.push_back(symbol * carrier[sample_idx]);
                sample_idx++;
            }
        }
        output_track.insert(output_track.end(), silence.begin(), silence.end());
    }
    return output_track;
}

class Transmitter : public AudioIODeviceCallback {

public:

    int time = 0;
    std::vector<float> signal;
    bool* finished;

    Transmitter(std::vector<float>& wave, bool* finished) {
        signal = wave;
        this->finished = finished;
    }
    void audioDeviceAboutToStart(AudioIODevice* device) override {}
    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
        int numInputChannels,
        float* const* outputChannelData,
        int numOutputChannels,
        int numSamples,
        const AudioIODeviceCallbackContext& context) {

        float data;
        for (int i = 0; i < numSamples; i++) {
            if (time < signal.size())
            {
                data = signal[time++];
            }
            else {
                data = 0;
                *finished = true;
            }
            outputChannelData[0][i] = data;
        }
    }
};