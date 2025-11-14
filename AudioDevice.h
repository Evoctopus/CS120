#pragma once


#include "Utils.h"

using namespace juce;

class AudioDevice : public AudioIODeviceCallback {

public:

    int time = 0;
    std::queue<float> signal;
    int length;
    std::vector<float> frame_buffer;

    Mutex_FIFO<float> &sending_fifo, &receiving_fifo;

    AudioDevice(Mutex_FIFO<float>& Sending_FIFO, Mutex_FIFO<float>& Receiving_FIFO) : 
		sending_fifo(Sending_FIFO), receiving_fifo(Receiving_FIFO) {}

    void audioDeviceAboutToStart(AudioIODevice* device) override {}
    void audioDeviceStopped() override {
        //writeToFile(receiving_fifo.vectorize(), "received_signal.txt", '\n');
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
    }
};