#include <fstream>
#include <random>

#include "HammingCode.h"
#include "Utils.h"
#include "Modulation.h"
#include "Demodulation.h"
#include "Mutex_FIFO.h"
#include "AudioDevice.h"
#include "MAC.h"

using namespace juce;

void initialize_dev_manager(AudioDeviceManager& dev_manager) {
    dev_manager.initialiseWithDefaultDevices(1, 1);
    AudioDeviceManager::AudioDeviceSetup dev_info;
    dev_info = dev_manager.getAudioDeviceSetup();
    dev_info.sampleRate = SAMPLE_RATE;
    dev_manager.setAudioDeviceSetup(dev_info, false);
}

int main(int argc, char* argv[])
{
    AudioDeviceManager dev_manager;
    initialize_dev_manager(dev_manager);

    int transmit;
    char c;

    std::cin >> transmit;

	std::cout << "Press any key to stop..." << std::endl;

    Mutex_FIFO<float> Sending_FIFO, Receiving_FIFO;
    Mutex_FIFO<std::deque<bool>> MAC_FIFO;
    Mutex_FIFO<bool> App_FIFO;


    std::vector<float> signal;
	Demodulator demodulator(Receiving_FIFO, MAC_FIFO);
	Modulator modulator(Sending_FIFO);
	AudioDevice audio_device(Sending_FIFO, Receiving_FIFO);
	MAC mac(MAC_FIFO, App_FIFO, modulator);
    
    std::vector<bool> data = readFromFile("large.txt");
    dev_manager.addAudioCallback(&audio_device);
    demodulator.startThread();
    mac.startThread();
    

	auto frame_begin = data.begin();
    //for (int i=0; i<20; ++i)
    while (frame_begin < data.end()) 
    {

        auto frame_end = frame_begin + BITS_PER_FRAME;
        if (frame_end > data.end()) frame_end = data.end();
        std::deque<bool> frame(frame_begin, frame_end);

        //modulator.modulate(frame);
        while (!mac.send_data(frame, 1)) {}
        frame_begin = frame_end;
	}

    //mac.send_ACK(1);
	std::cin >> c;
    dev_manager.removeAudioCallback(&audio_device);
	//demodulator.Decode(Receiving_FIFO.vectorize());
    demodulator.signalThreadShouldExit();
    mac.signalThreadShouldExit();

    writeToFile(App_FIFO.vectorize(), "output.txt", '0');

    return 0;
}
