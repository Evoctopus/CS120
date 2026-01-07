#include "MAC.h"
#include "Demodulation.h"
#include "AudioDevice.h"
#include "Utils.h"
#include "Modulation.h"
#include "Virtual.h"

using namespace juce;


Mutex_FIFO<float> Sending_FIFO, Receiving_FIFO;
Mutex_FIFO<std::deque<bool>> MAC_FIFO;
Mutex_FIFO<std::pair<int, std::deque<bool>>> INTER_FIFO;
std::atomic<bool> channel_is_idle;

ThreadFlag demo_thread_flag;
ThreadFlag virtual_thread_flag;
ThreadFlag mac_thread_flag;

//std::vector<bool> flatten(Mutex_FIFO<std::pair<uint8_t*, size_t>>& fifo) {
//    std::vector<bool> result;
//    std::pair<uint8_t*, size_t> item;
//    while (fifo.pop(item)) {
//        std::deque<bool> buffer;
//        uint8_to_deque_bool(item.first, item.second, buffer);
//        result.insert(result.end(), buffer.begin() + ETH_HDR_LEN * 8, buffer.end());
//        delete[] item.first;
//    }
//	return result;
//}

void initialize_dev_manager(AudioDeviceManager& dev_manager) {
    dev_manager.initialiseWithDefaultDevices(1, 1);
    AudioDeviceManager::AudioDeviceSetup dev_info;
    dev_info = dev_manager.getAudioDeviceSetup();
    dev_info.sampleRate = SAMPLE_RATE;
    dev_manager.setAudioDeviceSetup(dev_info, false);
}


std::vector<std::string> split_by_space(const std::string& str) {
    std::vector<std::string> res;
    std::stringstream ss(str);
    std::string token;
    while (ss >> token) {
        res.push_back(token);
    }
    return res;
}

int main(int argc, char* argv[])
{

    

    AudioDeviceManager dev_manager;
    initialize_dev_manager(dev_manager);

    SharedMemoryComm comm("SharedMemBuffer", 4096, false);

    AudioDevice audio_device(Sending_FIFO, Receiving_FIFO, channel_is_idle, demo_thread_flag);
    Demodulator demodulator(Receiving_FIFO, MAC_FIFO, mac_thread_flag, demo_thread_flag);
    Modulator modulator(Sending_FIFO);
    MAC mac(MAC_FIFO, INTER_FIFO, modulator, channel_is_idle, mac_thread_flag, virtual_thread_flag);
	Virtual virtual_device(INTER_FIFO, comm, mac, virtual_thread_flag);
    //IpV4PacketHandler ipv4(mac, INTER_FIFO, global_address, audio_thread_flag);
    //IpV4PacketHandler ipv4(mac, INTER_FIFO, local_address, audio_thread_flag);
    
 
    //std::vector<bool> data = readBinFile("INPUT.bin");
    //split_data_and_send(data, mac, local_address);

    dev_manager.addAudioCallback(&audio_device);
    demodulator.startThread();
    mac.startThread();
	virtual_device.startThread();
    

    std::string command;
	//virtual_device.ping("www.baidu.com", 4);
    
	virtual_device.http_get("www.example.com", "0x12345678");


    std::cin >> command;
    /*while (true) {
        std::cin >> command;
        if (command == "exit") {
            break;
		}
    }*/

    dev_manager.removeAudioCallback(&audio_device);
    demodulator.signalThreadShouldExit();
    mac.signalThreadShouldExit();
	virtual_device.signalThreadShouldExit();

    //compare(flatten(INTER_FIFO), data);
    //writeBinFile("ANSWER.bin", INTER_FIFO.vectorize());
    
    return 0;
}

