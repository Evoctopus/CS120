/*
  ==============================================================================

    This file contains the basic startup code for a JUCE application.

  ==============================================================================
*/

#include <JuceHeader.h>
#include <fstream>
#include <random>

#include "HammingCode.h"
#include "Utils.h"

#define PI acos(-1)

#define SAMPLE_RATE 48000


using namespace juce;

class Tester : public AudioIODeviceCallback {

public:

    int time = 0;

    Tester() {}
    void audioDeviceAboutToStart(AudioIODevice* device) override {}
    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
        int numInputChannels,
        float* const* outputChannelData,
        int numOutputChannels,
        int numSamples,
        const AudioIODeviceCallbackContext& context) {
        // Generate Sine Wave Data
        double dPhasePerSample = 2 * (double)PI / (double)SAMPLE_RATE;
        float data;
        for (int i = 0; i < numSamples; i++) {

            data = sin(PI / 24.0f * time) + sin(PI / 2.4f * time);
            time++;
            time %= 48;
            outputChannelData[0][i] = data;
        }
    }
};


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


std::vector<float> generateChirp(int length) {
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

        current_phase += 2.0 * PI * current_freq / SAMPLE_RATE;
        while (current_phase > 2.0 * PI) current_phase -= 2.0 * PI;
        while (current_phase < -2.0 * PI) current_phase += 2.0 * PI;
        preamble[i] = static_cast<float>(sin(current_phase));
    }
    return preamble;
}

std::vector<float> generateSilence(int length) {
    return std::vector<float>(length, 0.0f);
}

std::vector<float> modulate(const std::vector<bool>& data) {
    #define PREAMBLE_LENGTH 480
    #define BITS_PER_FRAME 10000
    #define SAMPLES_PER_BIT 48
    #define SILENCE_LENGTH 100
    #define LENGTH_BITS 16
    const double carrier_omega = 20000.0 * PI;

    std::vector<float> chirp = generateChirp(PREAMBLE_LENGTH);
    std::vector<float> silence = generateSilence(SILENCE_LENGTH);

    std::size_t total_bits = data.size();
	int frame_num = (total_bits + BITS_PER_FRAME - 1) / BITS_PER_FRAME;

    std::vector<float> output_track;
    output_track.reserve(frame_num * (PREAMBLE_LENGTH + SILENCE_LENGTH) + total_bits * SAMPLES_PER_BIT);

    int sample_idx = 0;
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

        
        for (bool bit : frame){
            float symbol = bit ? 1.0f : -1.0f;
            for (int k = 0; k < SAMPLES_PER_BIT; ++k) {
                double t = static_cast<double>(sample_idx) / SAMPLE_RATE;
                output_track.push_back(symbol * static_cast<float>(sin(carrier_omega * t)));
                sample_idx++;
            }
        }
        output_track.insert(output_track.end(), silence.begin(), silence.end());
    }
    return output_track;
}

//==============================================================================
int main(int argc, char* argv[])
{
    AudioDeviceManager dev_manager;
    dev_manager.initialiseWithDefaultDevices(1, 1);
    AudioDeviceManager::AudioDeviceSetup dev_info;
    dev_info = dev_manager.getAudioDeviceSetup();
    dev_info.sampleRate = SAMPLE_RATE; 
    dev_manager.setAudioDeviceSetup(dev_info, false);


    /*Tester tester;
    dev_manager.addAudioCallback(&tester);
    std::cout << "Playing..." << std::endl;
    getchar();
    dev_manager.removeAudioCallback(&tester);*/

    std::vector<bool> data = readFromFile("input.txt");

    std::vector<float> modulated_signal = modulate(data);
    
    bool finished = false;
    Transmitter transmitter(modulated_signal, &finished);
    dev_manager.addAudioCallback(&transmitter);
    while (!finished) {}
    dev_manager.removeAudioCallback(&transmitter);
    
    return 0;
}
