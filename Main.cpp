/*
  ==============================================================================

    This file contains the basic startup code for a JUCE application.

  ==============================================================================
*/

#include <JuceHeader.h>
#include <fstream>
#include <random>

#define PI acos(-1)

#define SAMPLE_RATE 48000
#define SAMPLES_PER_BIT 48

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

        int sampleRate = 48000;
        float dPhasePerSample = 2 * PI / (float)sampleRate;
        float initPhase = 0;
        float data;

        for (int i = 0; i < numSamples; i++) {

            data = sin(dPhasePerSample * 1000 * time) + sin(dPhasePerSample * 10000 * time);
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

std::vector<bool> generateRandomBits(int num_bits) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> bit_dist(0, 1);

    std::vector<bool> bits;
    bits.reserve(num_bits);

    for (int i = 0; i < num_bits; ++i) {
        bits.push_back(bit_dist(gen));
    }
    return bits;
}

void writeToFile(const std::vector<bool>& array) {
    std::ofstream outfile("input.txt");
    for (const bool& bit : array)
        outfile << bit;
    outfile.close();
}


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

//==============================================================================
int main(int argc, char* argv[])
{
    AudioDeviceManager dev_manager;
    dev_manager.initialiseWithDefaultDevices(1, 1);
    AudioDeviceManager::AudioDeviceSetup dev_info;
    dev_info = dev_manager.getAudioDeviceSetup();
    dev_info.sampleRate = SAMPLE_RATE; 
    dev_manager.setAudioDeviceSetup(dev_info, false);


   /* Tester tester;
    dev_manager.addAudioCallback(&tester);
    std::cout << "Playing..." << std::endl;
    getchar();
    dev_manager.removeAudioCallback(&tester);*/

    const double carrier_omega = 20000.0 * PI;

#define PREAMBLE_LENGTH 4800
    std::vector<float> chirp = generateChirp(PREAMBLE_LENGTH);
    std::vector<bool> frame = generateRandomBits(10000);

    writeToFile(frame);

    std::size_t total_bits = frame.size();
    int wave_length = total_bits * SAMPLES_PER_BIT;

    std::vector<float> frame_wave(wave_length);
    for (int j = 0; j < total_bits; ++j) {
        float symbol = frame[j] ? 1.0f : -1.0f;
        for (int k = 0; k < SAMPLES_PER_BIT; ++k) {
            int sample_idx = j * SAMPLES_PER_BIT + k;
            double t = static_cast<double>(sample_idx) / SAMPLE_RATE;
            frame_wave[sample_idx] = symbol * static_cast<float>(sin(carrier_omega * t));
        }
    }

    std::vector<float> frame_wave_pre;
    frame_wave_pre.reserve(PREAMBLE_LENGTH + wave_length);
    frame_wave_pre.insert(frame_wave_pre.end(), chirp.begin(), chirp.end());
    frame_wave_pre.insert(frame_wave_pre.end(), frame_wave.begin(), frame_wave.end());

    bool finished = false;
    Transmitter transmitter(frame_wave_pre, &finished);
    dev_manager.addAudioCallback(&transmitter);
    while (!finished) {}
    dev_manager.removeAudioCallback(&transmitter);
    return 0;
}
