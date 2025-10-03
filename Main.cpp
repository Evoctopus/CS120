/*
  ==============================================================================

    This file contains the basic startup code for a JUCE application.

  ==============================================================================
*/

#include <JuceHeader.h>
#include <fstream>

#define PI acos(-1)

using namespace juce;

std::vector<std::vector<float>> recordedData;


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



class Recorder : public AudioIODeviceCallback {

public:

    std::fstream record_file;

    Recorder() { //record_file.open("recorded_data.txt", std::ios::out); 
    }

    void audioDeviceAboutToStart(AudioIODevice* device) override {}

    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
        int numInputChannels,
        float* const* outputChannelData,
        int numOutputChannels,
        int numSamples,
        const AudioIODeviceCallbackContext& context) {
        // Generate Sine Wave Data

   //     for (int i = 0; i < numSamples; i++) {
   //         // Write the sample into the output channel 
			//record_file << inputChannelData[0][i] << " ";
   //     }
    }

    ~Recorder()
    {
		//record_file.close();
    }
};

//==============================================================================
int main(int argc, char* argv[])
{
    /* Initialize Player */
    AudioDeviceManager dev_manager;
    dev_manager.initialiseWithDefaultDevices(1, 1);
    AudioDeviceManager::AudioDeviceSetup dev_info;
    dev_info = dev_manager.getAudioDeviceSetup();
    dev_info.sampleRate = 48000; // Setup sample rate to 48000 Hz
    dev_manager.setAudioDeviceSetup(dev_info, false);

    /* Add callback to AudioDeviceManager */

    Tester tester;
	Recorder recorder;
   
    dev_manager.addAudioCallback(&tester);


    std::cout << "Playing..." << std::endl;

    getchar();
    dev_manager.removeAudioCallback(&tester);



    return 0;
}
