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

using namespace juce;

//==============================================================================
int main(int argc, char* argv[])
{
    AudioDeviceManager dev_manager;
    dev_manager.initialiseWithDefaultDevices(1, 1);
    AudioDeviceManager::AudioDeviceSetup dev_info;
    dev_info = dev_manager.getAudioDeviceSetup();
    dev_info.sampleRate = SAMPLE_RATE; 
    dev_manager.setAudioDeviceSetup(dev_info, false);

    int transmit;
    char c;

    std::cin >> transmit;

	std::cout << "Press any key to stop..." << std::endl;

    if (transmit == 1) {
        std::vector<bool> data = readFromFile("input.txt");
        std::queue<float> modulated_signal = modulate(data, SAMPLE_RATE);
        
        /*Receiver receiver;
		receiver.frame_buffer = modulated_signal;
        receiver.Decode();*/

        /*/Tester tester;
		dev_manager.addAudioCallback(&tester);
		std::cin >> c;
        dev_manager.removeAudioCallback(&tester);*/

        Transmitter transmitter(modulated_signal);
        Receiver receiver;
        dev_manager.addAudioCallback(&transmitter);
        
		std::cin >> c;
        dev_manager.removeAudioCallback(&transmitter);
        
	}
    else {
        Receiver receiver;
        dev_manager.addAudioCallback(&receiver);
        std::cin >> c;
		dev_manager.removeAudioCallback(&receiver);

		/*std::vector<bool> decoded_bits = readFromFile("decoded_bits.txt");

		std::vector<bool> data = readFromFile("input.txt");
        int length = data.size();
        int error = 0;
        for (int i = 0; i < length; ++i) {
            if (decoded_bits[i] != data[i]) error++;
        }
		printf("BER: %.4f\n", (float)error / length);*/
    }
    return 0;
}
