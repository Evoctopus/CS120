#pragma once

#include "Utils.h"

using namespace juce;


void initialize_dev_manager(AudioDeviceManager& dev_manager) {
    dev_manager.initialiseWithDefaultDevices(1, 1);
    AudioDeviceManager::AudioDeviceSetup dev_info;
    dev_info = dev_manager.getAudioDeviceSetup();
    dev_info.sampleRate = SAMPLE_RATE;
    dev_manager.setAudioDeviceSetup(dev_info, false);
}

class AudioDevice : public AudioIODeviceCallback {

public:

    Mutex_FIFO<float> &sending_fifo, &receiving_fifo;
    std::atomic<bool> &channel_is_idle;
	ThreadFlag& demo_thread_flag;

    AudioDevice(Mutex_FIFO<float>& Sending_FIFO, Mutex_FIFO<float>& Receiving_FIFO, std::atomic<bool>& Channel_is_idle, ThreadFlag& demo_thread_flag_) :
		sending_fifo(Sending_FIFO), receiving_fifo(Receiving_FIFO), channel_is_idle(Channel_is_idle), demo_thread_flag(demo_thread_flag_) {
    }

    void audioDeviceAboutToStart(AudioIODevice* device) override {}
    void audioDeviceStopped() override {}

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
        
        float power = 0.0f;
        for (int i = 0; i < numSamples; ++i) {
            float sample = inputChannelData[0][i];
            power += sample * sample;
        }
        power /= numSamples;
        if (power >= 0.001f) {
            channel_is_idle = false;
            receiving_fifo.push_batch(inputChannelData[0], numSamples);
            demo_thread_flag.wake_up();
        }
        else {
            channel_is_idle = true;
        }
    }
};