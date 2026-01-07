#include "MAC.h"
#include "Demodulation.h"
#include "AudioDevice.h"
#include "Modulation.h"
#include "Virtual.h"
#include "Utils.h"


std::vector<std::deque<bool>> split_data(std::vector<bool> data) {
    std::vector<std::deque<bool>> results;
    auto frame_begin = data.begin();
    while (frame_begin < data.end())
    {
        auto remaining_bits = std::distance(frame_begin, data.end());
        size_t frame_size = static_cast<size_t>(std::min(remaining_bits, static_cast<decltype(remaining_bits)>(BITS_PER_FRAME)));
        auto frame_end = frame_begin;
        std::advance(frame_end, frame_size);
        std::deque<bool> frame(frame_begin, frame_end);
        results.push_back(std::move(frame));
        frame_begin = frame_end;
    }
    return results;
}

std::vector<bool> flatten(Mutex_FIFO<std::pair<int, std::deque<bool>>>& fifo) {
    std::vector<bool> result;
     std::pair<int, std::deque<bool>> item;
     while (fifo.pop(item)) {
         std::deque<bool>& payload = item.second;
         result.insert(result.end(), payload.begin(), payload.end());
     }
     return result;
}

std::vector<bool> flatten(Mutex_FIFO<std::deque<bool>>& fifo) {
    std::vector<bool> result;
    std::deque<bool> item;
    while (fifo.pop(item)) {
        result.insert(result.end(), item.begin(), item.end());
    }
    return result;
}

Mutex_FIFO<float> Sending_FIFO, Receiving_FIFO;
Mutex_FIFO<std::deque<bool>> MAC_FIFO;
Mutex_FIFO<std::pair<int, std::deque<bool>>> INTER_FIFO;
std::atomic<bool> channel_is_idle;

ThreadFlag demo_thread_flag;
ThreadFlag virtual_thread_flag;
ThreadFlag mac_thread_flag;

int main(int argc, char* argv[])
{

    AudioDeviceManager dev_manager;
    initialize_dev_manager(dev_manager);

    //SharedMemoryComm comm("SharedMemBuffer", 4096, false);

    AudioDevice audio_device(Sending_FIFO, Receiving_FIFO, channel_is_idle, demo_thread_flag);
    Demodulator demodulator(Receiving_FIFO, MAC_FIFO, mac_thread_flag, demo_thread_flag);
    Modulator modulator(Sending_FIFO);
    //MAC mac(MAC_FIFO, INTER_FIFO, modulator, channel_is_idle, mac_thread_flag, virtual_thread_flag);
	//Virtual virtual_device(INTER_FIFO, comm, mac, virtual_thread_flag);
 
    dev_manager.addAudioCallback(&audio_device);
    demodulator.startThread();
    //mac.startThread();
    //virtual_device.startThread();

    std::vector<bool> data = readFromFile("input.bin");
    std::vector<std::deque<bool>> frames = split_data(data);
    for (const auto& frame : frames) {
        modulator.modulate(frame);
    }
    /*std::vector<bool> data = readBinFile("input.bin");
    std::deque<bool> payload(data.begin(), data.end());
    int dest;
    std::cout << "DEST: ";
    std::cin >> dest;
    mac.send_data(payload, dest, DATA_TYPE);*/


    
	//virtual_device.ping("www.baidu.com", 4);
    
	//virtual_device.http_get("www.example.com", "0x12345678");

    std::string command;
    while (true) {
        std::cin >> command;
        if (command == "exit") {
            break;
		}
    }

    dev_manager.removeAudioCallback(&audio_device);
    demodulator.signalThreadShouldExit();
    //mac.signalThreadShouldExit();
	//virtual_device.signalThreadShouldExit();

    compare(flatten(MAC_FIFO), data);
    writeToFile(flatten(MAC_FIFO), "proj1_out");

    //compare(flatten(INTER_FIFO), data);
    //writeBinFile("ANSWER.bin", flatten(INTER_FIFO));
    
    return 0;
}

