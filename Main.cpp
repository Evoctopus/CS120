/*
  ==============================================================================

    This file contains the basic startup code for a JUCE application.

  ==============================================================================
*/


#include <fstream>
#include <random>

#include "HammingCode.h"
#include "Utils.h"
#include "Modulation.h"
#include "Demodulation.h"

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

            data = sin(PI / 24.0f * time) + 100.0 * sin(PI / 2.4f * time);
            time++;
            time %= 48;
            outputChannelData[0][i] = data;
        }
    }
};


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
    std::vector<float> modulated_signal = modulate(data, SAMPLE_RATE);

    std::vector<float> chirp;

	chirp = generateChirp(48000, SAMPLE_RATE);
    
    /*bool finished = false;
    Transmitter transmitter(modulated_signal, &finished);
    dev_manager.addAudioCallback(&transmitter);
    while (!finished) {}
    dev_manager.removeAudioCallback(&transmitter);*/

    Receiver receiver;

	dev_manager.addAudioCallback(&receiver);
    getchar();
	dev_manager.removeAudioCallback(&receiver);
    
    
    return 0;
}
