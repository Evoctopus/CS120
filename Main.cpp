#include "Ether.h"
#include "MAC.h"
#include "Demodulation.h"
#include "AudioDevice.h"
#include "Utils.h"
#include "Modulation.h"

using namespace juce;

Mutex_FIFO<float> Sending_FIFO, Receiving_FIFO;
Mutex_FIFO<std::deque<bool>> MAC_FIFO;
Mutex_FIFO<std::pair<int, std::deque<bool>>> INTER_FIFO;
std::atomic<bool> channel_is_idle;

std::vector<bool> flatten(Mutex_FIFO<std::pair<uint8_t*, size_t>>& fifo) {
    std::vector<bool> result;
    std::pair<uint8_t*, size_t> item;
    while (fifo.pop(item)) {
        std::deque<bool> buffer;
        uint8_to_deque_bool(item.first, item.second, buffer);
        result.insert(result.end(), buffer.begin() + ETH_HDR_LEN * 8, buffer.end());
        delete[] item.first;
    }
	return result;
}

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

void split_data_and_send(std::vector<float>& data, MAC& mac, int dst) {
    int seconds = 1;
    printf("Waiting for %d seconds\n", seconds);
    std::this_thread::sleep_for(std::chrono::seconds(seconds));

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
        uint8_t bytes_buffer[1514];
        size_t len = deque_bool_to_uint8(frame, bytes_buffer);
        mac.send_data(frame, dst, DATA_TYPE);
        frame_begin = frame_end;
    }
}

int main(int argc, char* argv[])
{
    AudioDeviceManager dev_manager;
    initialize_dev_manager(dev_manager);

    ADDRESS local_address("192.168.137.1", "76-3A-F4-48-A9-C7");
    ADDRESS dst_address("10.20.102.251", "C8-CB-9E-77-7D-C9");

    AudioDevice audio_device(Sending_FIFO, Receiving_FIFO, channel_is_idle);
    Demodulator demodulator(Receiving_FIFO, MAC_FIFO);
    Modulator modulator(Sending_FIFO);
    MAC mac(MAC_FIFO, INTER_FIFO, modulator, channel_is_idle, 0);
    IpV4PacketHandler ipv4(mac, INTER_FIFO, local_address);
    

    ipv4.add_to_routing_table(dst_address, true, 1);
    

    //std::vector<bool> data = readBinFile("INPUT.bin");
    //split_data_and_send(data, mac, local_address);

    dev_manager.addAudioCallback(&audio_device);
    demodulator.startThread();
    mac.startThread();
    ipv4.start_capture();

    //ipv4.pinging(local_address, local_address, 0, true, 10);
    
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    getchar();

    dev_manager.removeAudioCallback(&audio_device);
    demodulator.signalThreadShouldExit();
    mac.signalThreadShouldExit();
    ipv4.stop_capture();

    //compare(flatten(INTER_FIFO), data);
    //writeBinFile("ANSWER.bin", INTER_FIFO.vectorize());
    
    return 0;
}

