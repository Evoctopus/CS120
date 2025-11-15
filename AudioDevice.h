#pragma once


#include "Utils.h"

using namespace juce;

class AudioDevice : public AudioIODeviceCallback {

public:

    Mutex_FIFO<float> &sending_fifo, &receiving_fifo;
    std::atomic<bool> &channel_is_idle;

    std::vector<float> power_debug;
    std::vector<float> frame_buffer;

    AudioDevice(Mutex_FIFO<float>& Sending_FIFO, Mutex_FIFO<float>& Receiving_FIFO, std::atomic<bool>& Channel_is_idle) :
		sending_fifo(Sending_FIFO), receiving_fifo(Receiving_FIFO), channel_is_idle(Channel_is_idle) {}

    void audioDeviceAboutToStart(AudioIODevice* device) override {}
    void audioDeviceStopped() override {
        writeToFile(frame_buffer, "received_signal.txt", '\n');
        writeToFile(power_debug, "power.txt", '\n');
    }

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
        int numInputChannels,
        float* const* outputChannelData,
        int numOutputChannels,
        int numSamples,
        const AudioIODeviceCallbackContext& context) {

		size_t samples = sending_fifo.pop_batch(outputChannelData[0], numSamples);
		//if (samples != numSamples && samples > 0) printf("Warning: sent only %zu out of %d samples\n", samples, numSamples);
        for (int i = samples; i < numSamples; ++i) {
            outputChannelData[0][i] = 0.0f; 
		}
		//printf("Sent %zu samples\n", samples);
        receiving_fifo.push_batch(inputChannelData[0], numSamples);


        float power = 0.0f;
        for (int i = 0; i < numSamples; ++i) {
            float sample = inputChannelData[0][i];
            power += sample * sample;
            frame_buffer.push_back(sample);
        }
        power /= numSamples;
        if (power >= 0.05f) channel_is_idle = false;
        else channel_is_idle = true;
        power_debug.push_back(power);

    }
};