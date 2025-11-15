#include <fstream>
#include <random>

#include "HammingCode.h"
#include "Utils.h"
#include "Modulation.h"
#include "Demodulation.h"
#include "Mutex_FIFO.h"
#include "AudioDevice.h"
#include "MAC.h"
#include "CRC.h"

using namespace juce;

Mutex_FIFO<float> Sending_FIFO, Receiving_FIFO;
Mutex_FIFO<std::deque<bool>> MAC_FIFO;
Mutex_FIFO<bool> App_FIFO;
std::atomic<bool> channel_is_idle;

void initialize_dev_manager(AudioDeviceManager& dev_manager) {
    dev_manager.initialiseWithDefaultDevices(1, 1);
    AudioDeviceManager::AudioDeviceSetup dev_info;
    dev_info = dev_manager.getAudioDeviceSetup();
    dev_info.sampleRate = SAMPLE_RATE;
    dev_manager.setAudioDeviceSetup(dev_info, false);
}

void flatten_MAC() {
    std::deque<bool> signal;
    while (MAC_FIFO.pop(signal)) {
        App_FIFO.push_batch(signal);
    }
    return;
}

int main(int argc, char* argv[])
{
    AudioDeviceManager dev_manager;
    initialize_dev_manager(dev_manager);

    int transmit;
    char c;

    printf("Welcome! Your IP: %d\n", ADDRESS);
    std::cout << "Press xxx to transmit to IPxxx, If xxx is invalid, nothing will be sent" << std::endl;

    std::cin >> transmit;

    

	Demodulator demodulator(Receiving_FIFO, MAC_FIFO);
	Modulator modulator(Sending_FIFO);
	AudioDevice audio_device(Sending_FIFO, Receiving_FIFO, channel_is_idle);
	MAC mac(MAC_FIFO, App_FIFO, modulator, channel_is_idle);
    
    std::vector<bool> data = readFromFile("large.txt");
    dev_manager.addAudioCallback(&audio_device);
    demodulator.startThread();
    mac.startThread();
    

    /*auto now = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    std::cout << "当前时间点（毫秒）: " << ms << std::endl;
    std::deque<bool> frame(50, false);
    mac.send_data(frame, transmit);*/
    
    
    int seconds = generate_random_backoff(6, 10);
    printf("Wait for %d seconds\n", seconds);
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    if (transmit == 1 || transmit == 0)
    {
        auto frame_begin = data.begin();
        //for (int i=0; i<10; ++i)
        while (frame_begin < data.end())
        {
            auto remaining_bits = std::distance(frame_begin, data.end());
            size_t frame_size = static_cast<size_t>(std::min(remaining_bits, static_cast<decltype(remaining_bits)>(BITS_PER_FRAME)));
            auto frame_end = frame_begin;
            std::advance(frame_end, frame_size);
            std::deque<bool> frame(frame_begin, frame_end);

            //modulator.modulate(frame);
            while (!mac.send_data(frame, transmit)) {}
            frame_begin = frame_end;
        }
    }

	std::cin >> c;
    //demodulator.Decode(Receiving_FIFO.vectorize());
    dev_manager.removeAudioCallback(&audio_device);
    demodulator.signalThreadShouldExit();
    mac.signalThreadShouldExit();

    //flatten_MAC();
    compare(App_FIFO.vectorize(), data);
    //writeToFile(App_FIFO.vectorize(), "output.txt", '0');
    
    return 0;
}