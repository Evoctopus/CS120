#include <fstream>
#include <random>

#include "HammingCode.h"
#include "Utils.h"
#include "Modulation.h"
#include "Demodulation.h"
#include "Mutex_FIFO.h"
#include "AudioDevice.h"

using namespace juce;

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

    Mutex_FIFO<float> Sending_FIFO, Receiving_FIFO;
    Mutex_FIFO<bool >MAC_FIFO;


    std::vector<float> signal;
	Demodulator demodulator(Receiving_FIFO, MAC_FIFO);
	AudioDevice audio_device(Sending_FIFO, Receiving_FIFO);

    
    std::vector<bool> data = readFromFile("large.txt");
    dev_manager.addAudioCallback(&audio_device);
    modulate(data, Sending_FIFO);

    demodulator.startThread();
 
	std::cin >> c;
    dev_manager.removeAudioCallback(&audio_device);
	//demodulator.Decode(Receiving_FIFO.vectorize());
    demodulator.signalThreadShouldExit();

    writeToFile(MAC_FIFO.vectorize(), "output.txt", '0');
	//writeToFile(signal, "received_signal.txt", '\n');
    return 0;
}
