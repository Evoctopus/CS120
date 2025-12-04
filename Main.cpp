#include "Utils.h"
#include "Modulation.h"
#include "Demodulation.h"
#include "Mutex_FIFO.h"
#include "AudioDevice.h"
#include "MAC.h"
#include "Ether.h"

#include "HammingCode.h"

using namespace juce;

Mutex_FIFO<float> Sending_FIFO, Receiving_FIFO;
Mutex_FIFO<std::deque<bool>> MAC_FIFO;
Mutex_FIFO<std::pair<int, std::deque<bool>>> INTER_FIFO;
std::atomic<bool> channel_is_idle;

std::vector<bool> flatten(Mutex_FIFO<std::pair<int, std::deque<bool>>>& fifo) {
    std::vector<bool> result;
    std::pair<int, std::deque<bool>> item;
    while (fifo.pop(item)) {
        result.insert(result.end(), item.second.begin(), item.second.end());
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

int main(int argc, char* argv[])
{
    AudioDeviceManager dev_manager;
    initialize_dev_manager(dev_manager);

	int customized_mac = 0;
    /*std::cout << "Your CUSTOMIZED MAC: ";
    std::cin >> customized_mac;*/

    ADDRESS address;
    address.customized_mac = customized_mac;
    address.ipv4 = inet_addr("100.66.114.73");
    mac_str_to_uint8("74-3A-F4-48-A9-C7", address.mac);

    AudioDevice audio_device(Sending_FIFO, Receiving_FIFO, channel_is_idle);
    Demodulator demodulator(Receiving_FIFO, MAC_FIFO);
    Modulator modulator(Sending_FIFO);
    MAC mac(MAC_FIFO, INTER_FIFO, modulator, channel_is_idle, customized_mac);
    IpV4PacketHandler ipv4(mac, INTER_FIFO, address);
	

    ipv4.list_devices();
	int dev_index = 0;
	std::cout << "Select the device index to capture IPv4 packets: ";
	std::cin >> dev_index;
    ipv4.open_device(dev_index);
    
    ADDRESS dst;
    dst.customized_mac = 0;
    dst.ipv4 = inet_addr("100.66.114.73");
    mac_str_to_uint8("74-3A-F4-48-A9-C7", dst.mac);


    //std::vector<bool> data = readBinFile("INPUT.bin");

    dev_manager.addAudioCallback(&audio_device);
    demodulator.startThread();
    mac.startThread();
    ipv4.start_capture();

    ipv4.pinging(dst, true, 3);
    
    //auto now = std::chrono::steady_clock::now();
    //auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    //std::cout << "当前时间点（毫秒）: " << ms << std::endl;
    //std::deque<bool> frame = generateRandomBits(BITS_PER_FRAME);
    //mac.send_data(frame, transmit);

    /*int seconds = 3;
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
        mac.send_data(frame, 0);
        frame_begin = frame_end;
    }*/
    
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

std::vector<std::string> split_command_line(const std::string& cmd_line) {
    std::vector<std::string> args;    // 最终参数列表
    std::string current_arg;          // 当前正在拼接的参数
    bool in_quote = false;            // 是否处于引号内
    char quote_char = '\0';           // 引号类型（" 或 '）
    bool escape_next = false;         // 是否转义下一个字符

    for (char c : cmd_line) {
        // 1. 处理转义字符（\ 开头）
        if (escape_next) {
            current_arg += c;
            escape_next = false;
            continue;
        }

        if (c == '\\') {
        escape_next = true;
        continue;
        }

        // 3. 处理引号（"" 或 ''）
        if ((c == '"' || c == '\'') && !in_quote) {
            in_quote = true;
            quote_char = c;
            continue;
        }
        else if (c == quote_char && in_quote) {
            in_quote = false;
            quote_char = '\0';
            continue;
        }

        // 4. 处理空格（引号内的空格视为参数一部分）
        if (std::isspace(static_cast<unsigned char>(c)) && !in_quote) {
            // 非空参数才加入列表
            if (!current_arg.empty()) {
                args.push_back(current_arg);
                current_arg.clear();
            }
            continue;
        }
        current_arg += c;
    }

    // 6. 处理最后一个参数（避免遗漏）
    if (!current_arg.empty()) {
        args.push_back(current_arg);
    }

    return args;
}