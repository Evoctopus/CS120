#define NOMINMAX
#include <winsock2.h>
#pragma comment(lib, "wpcap.lib")
#pragma comment(lib, "ws2_32.lib")

#include "Utils.h"
#include "MAC.h"
#include <pcap.h>

#define PING_TIMEOUT_MS 2000

#define ICMP_ECHO 1
#define ICMP_REPLY 0
#define ICMP_REQUEST 8




uint64_t get_timestamp_milliseconds() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    uint64_t timestamp = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    timestamp /= 10000; // 100 纳秒 -> 毫秒
    timestamp -= 11644473600000ULL; // 转换为 Unix 毫秒时间戳
    return timestamp;
}

size_t deque_bool_to_uint8(
    const std::deque<bool>& bool_deque,
    uint8_t out_bytes[1514],
    bool big_endian = false
) {

    // 1. 参数合法性校验
    if (bool_deque.empty()) {
        fprintf(stderr, "Error: Input deque is empty\n");
        return 0;
    }
    if (out_bytes == nullptr) {
        fprintf(stderr, "Error: out_bytes is null pointer\n");
        return 0;
    }
    memset(out_bytes, 0, 1514);

    uint8_t current_byte = 0; // 临时存储当前打包的字节
    size_t bit_idx = 0;       // 当前bit在字节中的位置（0~7）
    size_t byte_idx = 0;      // 当前写入的字节索引

    for (bool bit : bool_deque) {
        if (bit) { 
            if (big_endian) {
                current_byte |= (1 << (7 - bit_idx));
            }
            else {
                current_byte |= (1 << bit_idx);
            }
        }
        bit_idx++;
        if (bit_idx == 8) {
            out_bytes[byte_idx++] = current_byte;
            current_byte = 0;
            bit_idx = 0;
        }
    }
    if (bit_idx > 0) {
        out_bytes[byte_idx++] = current_byte;
    }
    return byte_idx;
}

size_t calc_uint8_to_deque_bool_len(const uint8_t in_bytes[],
    size_t in_len,
    size_t total_bits = 0) {
    if (in_bytes == nullptr || in_len == 0) return 0;
    size_t max_bits = in_len * 8;
    return (total_bits == 0 || total_bits > max_bits) ? max_bits : total_bits;
}

size_t uint8_to_deque_bool(
    const uint8_t in_bytes[],
    size_t in_len,
    std::deque<bool>& out_deque,
    size_t total_bits = 0,
    bool big_endian = false
) {
    // 1. 参数合法性校验
    if (in_bytes == nullptr || in_len == 0) {
        fprintf(stderr, "Error: Invalid input (null pointer or empty array)\n");
        out_deque.clear();
        return 0;
    }

    // 2. 清空输出队列，计算实际要拆分的 bit 数
    out_deque.clear();
    size_t actual_bits = calc_uint8_to_deque_bool_len(in_bytes, in_len, total_bits);
    if (actual_bits == 0) {
        return 0;
    }

    size_t bit_count = 0; // 已拆分的 bit 数
    // 3. 逐字节拆分 bit
    for (size_t byte_idx = 0; byte_idx < in_len && bit_count < actual_bits; byte_idx++) {
        uint8_t current_byte = in_bytes[byte_idx];
        // 逐 bit 拆分当前字节（0~7位）
        for (size_t bit_idx = 0; bit_idx < 8 && bit_count < actual_bits; bit_idx++) {
            bool bit_val = false;
            if (big_endian) {
                // 大端：bit_idx=0 → 取第7位，bit_idx=1 → 取第6位...
                bit_val = (current_byte >> (7 - bit_idx)) & 0x01;
            }
            else {
                // 小端：bit_idx=0 → 取第0位，bit_idx=1 → 取第1位...（默认）
                bit_val = (current_byte >> bit_idx) & 0x01;
            }
            out_deque.push_back(bit_val);
            bit_count++;
        }
    }
    return bit_count;
}

struct ADDRESS {
    int customized_mac;
    uint32_t ipv4;
    uint8_t mac[6];
};

#pragma pack(push, 1) // Force 1-byte alignment

struct EthHeader {
    uint8_t dst_mac[6];   // Destination MAC address
    uint8_t src_mac[6];   // Source MAC address
    uint16_t eth_type;    // EtherType (0x0800 = IPv4)
};

// IPv4 header (20 bytes, no options) - Network Layer (RFC 791 compliant)
struct Ipv4Header {
    uint8_t version_ihl;  // Version (4) + IHL (5 = 20 bytes)
    uint8_t tos;          // Type of Service
    uint16_t total_len;   // Total length (IPv4 header + payload)
    uint16_t id;          // Identification
    uint16_t frag_off;    // Fragment offset (0 = no fragmentation)
    uint8_t ttl;          // Time to Live (e.g., 64)
    uint8_t protocol;     // Upper layer protocol (6 = TCP, 17 = UDP, etc.)
    uint16_t checksum;    // IPv4 checksum
    uint32_t src_ip;      // Source IP (network byte order)
    uint32_t dst_ip;      // Destination IP (network byte order)
};

struct IcmpEchoHeader {
    uint8_t type;
    uint64_t time_stamp;
};
#pragma pack(pop) // Restore default alignment

// Callback parameter encapsulation
struct CallbackParam {
    class IpV4PacketHandler* handler;
    void* user_data;
};

#define ETH_HDR_LEN sizeof(EthHeader)
#define IP_HDR_LEN sizeof(Ipv4Header)
#define ICMP_ECHO_HDR_LEN sizeof(IcmpEchoHeader)

// --------------------------
// Core IPv4 Packet Handler Class
// (Ethernet + IPv4 layers only - no ICMP)
// --------------------------
class IpV4PacketHandler {
private:

    Mutex_FIFO<std::pair<int, std::deque<bool>>>& inter_fifo;
    MAC& mac;
    pcap_t* dev_handle_;          // Network adapter handle
    int dev_index_;               // Selected adapter index
    int promisc_mode_;            // Promiscuous mode (1 = enabled)
    bool is_capturing_;           // Capture state

    std::thread capture_thread_;  // Background capture thread
    std::thread audio_capture_thread;
    std::atomic<bool> audio_thread_stop{ false };

    std::mutex mtx_;              // Thread safety mutex

    std::condition_variable cv_;

    std::atomic<bool> icmp_echo_received = false;

    ADDRESS local_address;

    // Disable copy/assignment (prevent handle leaks)
    IpV4PacketHandler(const IpV4PacketHandler&) = delete;
    IpV4PacketHandler& operator=(const IpV4PacketHandler&) = delete;

    
    void print_mac(const uint8_t* mac, const char* prefix) const {
        
        printf("%s%02x:%02x:%02x:%02x:%02x:%02x\n", prefix,
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }

    // Print IP address (thread-safe)
    void print_ip(uint32_t ip, const char* prefix) const {
        
        printf("%s%s", prefix, inet_ntoa(*(in_addr*)&ip));
    }

    // Print hex + ASCII payload (thread-safe)
    void print_hex_ascii(const uint8_t* data, int len) {
        
        int i, j;
        for (i = 0; i < len; i += 16) {
            printf("0x%04x: ", i);
            // Hexadecimal
            for (j = 0; j < 16; j++) {
                if (i + j < len) printf("%02x ", data[i + j]);
                else printf("   ");
            }
            // ASCII
            printf(" | ");
            for (j = 0; j < 16; j++) {
                if (i + j < len) {
                    uint8_t c = data[i + j];
                    printf("%c", (c >= 0x20 && c <= 0x7e) ? c : '.');
                }
                else printf(" ");
            }
            printf("\n");
        }
    }

    // Calculate IPv4 checksum (RFC 1071 compliant)
    uint16_t calculate_checksum(const uint8_t* data, int len) const {
        uint32_t sum = 0;
        int i = 0;
        while (len > 1) {
            sum += *((uint16_t*)&data[i]);
            i += 2;
            len -= 2;
        }
        if (len == 1) {
            sum += (uint16_t)data[i] << 8;
        }
        sum = (sum >> 16) + (sum & 0xFFFF);
        sum += (sum >> 16);
        return (uint16_t)(~sum);
    }


    int build_ipv4_frame(
        uint8_t* frame,               // Output frame buffer (max 1514 bytes)
        const uint8_t* dst_mac,       // Destination MAC
        const uint32_t dst_ip,              // Destination IP (host byte order)
        uint8_t protocol,             // Upper layer protocol (TCP/UDP/etc.)
        const uint8_t* payload,       // IPv4 payload
        int payload_len               // Payload length (max 1472 for MTU 1500)
    ) {
        // Frame size calculations
        const int IP_TOTAL_LEN = IP_HDR_LEN + payload_len;
        const int FRAME_TOTAL_LEN = ETH_HDR_LEN + IP_TOTAL_LEN;

        // Validate maximum frame size (Ethernet MTU = 1500 + 14 byte header)
        if (FRAME_TOTAL_LEN > 1514) {
            fprintf(stderr, "IPv4 frame too large (max 1514 bytes)\n");
            return -1;
        }

        // 1. Fill Ethernet header
        EthHeader* eth = (EthHeader*)frame;
        memcpy(eth->dst_mac, dst_mac, 6);
        memcpy(eth->src_mac, local_address.mac, 6);
        eth->eth_type = htons(0x0800); // EtherType for IPv4

        // 2. Fill IPv4 header
        Ipv4Header* ip = (Ipv4Header*)(frame + ETH_HDR_LEN);
        ip->version_ihl = 0x45;        // 4 = IPv4, 5 = 20-byte header
        ip->tos = 0;                   // Default TOS
        ip->total_len = htons(IP_TOTAL_LEN);
        ip->id = htons(rand() % 65535); // Random identification
        ip->frag_off = 0;              // No fragmentation
        ip->ttl = 64;                  // Standard TTL value
        ip->protocol = protocol;       // Upper layer protocol
        ip->src_ip = local_address.ipv4;    // Host → network byte order
        ip->dst_ip = dst_ip;
        ip->checksum = 0;              // Zero before checksum calculation
        ip->checksum = calculate_checksum((uint8_t*)ip, IP_HDR_LEN);

        // 3. Fill IPv4 payload
        if (payload_len > 0 && payload != nullptr) {
            memcpy(frame + ETH_HDR_LEN + IP_HDR_LEN, payload, payload_len);
        }

        return FRAME_TOTAL_LEN;
    }


    void parse_ipv4_packet(bpf_u_int32 caplen, const uint8_t* pkt_data, int customized_mac = -1, bool audio_way = false) {
       
        if (caplen < ETH_HDR_LEN + IP_HDR_LEN) {
			printf("Captured packet too short for IPv4\n");
            return;
        }
        EthHeader* eth = (EthHeader*)pkt_data;
        Ipv4Header* ip = (Ipv4Header*)(pkt_data + ETH_HDR_LEN);

    
        if (ntohs(eth->eth_type) != 0x0800) {
            return;
        }

        // Calculate payload length
        int ip_payload_len = ntohs(ip->total_len) - IP_HDR_LEN;
        int frame_payload_len = caplen - ETH_HDR_LEN - IP_HDR_LEN;
        int actual_payload_len = (ip_payload_len < frame_payload_len) ? ip_payload_len : frame_payload_len;
        u_char* payload_ptr = (u_char*)(pkt_data + ETH_HDR_LEN + IP_HDR_LEN);

        /*
        // Print packet details (thread-safe)
        printf("\n=====================================\n");
        printf("IPv4 Packet Captured\n");
        printf("-------------------------------------\n");

        // Capture timestamp
        time_t ts = header->ts.tv_sec;
        struct tm* tm = localtime(&ts);
        printf("Capture Time:    %04d-%02d-%02d %02d:%02d:%02d.%06d\n",
            tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
            tm->tm_hour, tm->tm_min, tm->tm_sec, (int)header->ts.tv_usec);

        // Data Link Layer (Ethernet)
        printf("--- Data Link Layer (Ethernet) ---\n");
        print_mac(eth->dst_mac, "Destination MAC: ");
        print_mac(eth->src_mac, "Source MAC:      ");
        printf("EtherType:       0x%04x (IPv4)\n", ntohs(eth->eth_type));

        // Network Layer (IPv4)
        printf("--- Network Layer (IPv4) ---\n");
        print_ip(ip->src_ip, "Source IP:       ");
        print_ip(ip->dst_ip, "Destination IP:  ");
        printf("IP Version:      %d\n", (ip->version_ihl >> 4) & 0x0F);
        printf("IP Header Len:   %d bytes\n", (ip->version_ihl & 0x0F) * 4);
        printf("IP TTL:          %d\n", ip->ttl);
        printf("Protocol:        %d\n", ip->protocol);
        printf("IP Total Len:    %d bytes\n", ntohs(ip->total_len));
        printf("IP Checksum:     0x%04x\n", ntohs(ip->checksum));

        // Payload
		
        if (actual_payload_len > 0) {
            printf("--- IPv4 Payload (%d bytes) ---\n", actual_payload_len);
            print_hex_ascii(payload_ptr, actual_payload_len);
        }*/

		ADDRESS src_addr;
		src_addr.ipv4 = ip->src_ip;
		memcpy(src_addr.mac, eth->src_mac, 6);
		src_addr.customized_mac = customized_mac;
        
        if (ip->protocol == ICMP_ECHO) {
            handle_icmp_echo(payload_ptr, actual_payload_len, src_addr, audio_way);
		}
    }

    bool handle_icmp_echo(
        uint8_t* payload, 
        int len, 
		const ADDRESS& src_addr,
        bool audio_way
    ) {
		IcmpEchoHeader* icmp_header = (IcmpEchoHeader*)payload;
        if (icmp_header->type == ICMP_REQUEST) { // ICMP Echo Request
            return send_icmp_echo(src_addr, ICMP_REPLY, icmp_header->time_stamp, audio_way);
        }
        else if (icmp_header->type == ICMP_REPLY) { // ICMP Echo Reply
			uint64_t rtt = get_timestamp_milliseconds() - icmp_header->time_stamp;
            print_ip(src_addr.ipv4, "Reply from ");
            printf(": bytes=%d time=%lums\n", len, rtt);
			icmp_echo_received = true;
            return true;
		}
    }

    // Capture callback (forward to class method)
    void on_packet_captured(const struct pcap_pkthdr* header, const uint8_t* pkt_data) {
        parse_ipv4_packet(header->caplen, pkt_data);
    }

    // Static capture callback (libpcap requirement)
    static void static_packet_handler(u_char* param, const struct pcap_pkthdr* header, const uint8_t* pkt_data) {
        CallbackParam* cb_param = (CallbackParam*)param;
        if (cb_param && cb_param->handler) {
            cb_param->handler->on_packet_captured(header, pkt_data);
        }
    }

    // Background capture loop
    void capture_loop() {
        if (!dev_handle_) {
            fprintf(stderr, "Adapter not opened! Capture thread exiting.\n");
            return;
        }

        CallbackParam cb_param;
        cb_param.handler = this;
        cb_param.user_data = nullptr;

        printf("\nCapture thread started (IPv4 packets)\n");
        // Run capture (block until pcap_breakloop)
        pcap_loop(dev_handle_, 0, static_packet_handler, (u_char*)&cb_param);

        // Mark capture as stopped
        is_capturing_ = false;
        printf("Capture thread stopped\n");
    }

    void audio_capture() {
        std::pair<int, std::deque<bool>> receiving_buffer;
        while (!audio_thread_stop) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            if (inter_fifo.pop(receiving_buffer)) {
				int src = receiving_buffer.first;
				std::deque<bool>& data = receiving_buffer.second;
                uint8_t frame[1514];
                size_t bytes = deque_bool_to_uint8(data, frame);
				parse_ipv4_packet(bytes, frame, src, true);
            }
        }
    }

public:
    // Constructor/Destructor
	IpV4PacketHandler(MAC& MAC, Mutex_FIFO<std::pair<int, std::deque<bool>>>& INTER_FIFO, const ADDRESS& Local_addr)
        : dev_handle_(nullptr), dev_index_(0), promisc_mode_(0), is_capturing_(false), mac(MAC), inter_fifo(INTER_FIFO), local_address(Local_addr) {

        // Initialize Winsock (required for network byte order functions)
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);

        srand(time(nullptr)); // Seed for random IPv4 ID
    }

    ~IpV4PacketHandler() {
        stop_capture();
        if (dev_handle_) {
            pcap_close(dev_handle_);
            dev_handle_ = nullptr;
        }
        if (capture_thread_.joinable() && audio_capture_thread.joinable()) {
            capture_thread_.join();
			audio_capture_thread.join();
        }
        WSACleanup();

    }

    
    void list_devices() const {
        pcap_if_t* alldevs = nullptr;
        char errbuf[PCAP_ERRBUF_SIZE] = { 0 };

        if (pcap_findalldevs(&alldevs, errbuf) == -1) {
            fprintf(stderr, "Failed to get adapter list: %s\n", errbuf);
            return;
        }

        printf("===== Available Network Adapters =====\n");
        int i = 0;
        for (pcap_if_t* d = alldevs; d; d = d->next) {
            printf("%d. Name: %s\n", ++i, d->name);
         
            if (d->description) {
                printf("   Description: %s\n", d->description);
            }
            else {
                printf("   Description: None\n");
            }
        }

        if (i == 0) {
            fprintf(stderr, "No adapters found! Install Npcap/libpcap.\n");
        }
        pcap_freealldevs(alldevs);
    }

    // Open network adapter
    bool open_device(int dev_index, int promisc_mode = 1, const char* filter = "ip") {

        // Cleanup existing handle
        if (dev_handle_) {
            pcap_close(dev_handle_);
            dev_handle_ = nullptr;
        }

        dev_index_ = dev_index;
        promisc_mode_ = promisc_mode;

        // Get adapter list
        pcap_if_t* alldevs = nullptr;
        pcap_if_t* dev = nullptr;
        char errbuf[PCAP_ERRBUF_SIZE] = { 0 };

        if (pcap_findalldevs(&alldevs, errbuf) == -1) {
            fprintf(stderr, "Failed to get adapter list: %s\n", errbuf);
            return false;
        }

        // Locate selected adapter
        dev = alldevs;
        for (int i = 0; i < dev_index - 1 && dev; i++) {
            dev = dev->next;
        }
        if (!dev) {
            fprintf(stderr, "Invalid adapter index!\n");
            pcap_freealldevs(alldevs);
            return false;
        }

        // Open adapter (capture all frames, 1s timeout)
        dev_handle_ = pcap_open_live(
            dev->name,        // Adapter name
            65536,            // Capture buffer size
            promisc_mode_,    // Promiscuous mode
            1000,             // Timeout (ms)
            errbuf            // Error buffer
        );
        if (!dev_handle_) {
            fprintf(stderr, "Failed to open adapter: %s\n", errbuf);
            pcap_freealldevs(alldevs);
            return false;
        }

        // Set BPF filter (default: capture only IPv4 packets)
        if (filter != nullptr && strlen(filter) > 0) {
            struct bpf_program fp;
            if (pcap_compile(dev_handle_, &fp, filter, 0, PCAP_NETMASK_UNKNOWN) == -1) {
                fprintf(stderr, "Filter compile failed: %s\n", pcap_geterr(dev_handle_));
            }
            else {
                pcap_setfilter(dev_handle_, &fp);
                pcap_freecode(&fp);
                printf("Filter enabled: %s\n", filter);
            }
        }

        printf("Successfully opened adapter: %s (Promiscuous Mode: %s)\n",
            dev->description ? dev->description : dev->name,
            promisc_mode_ ? "Enabled" : "Disabled");

        pcap_freealldevs(alldevs);
        return true;
    }

    void pinging(
		const ADDRESS& dst,          // Destination address info
        int audio_way = false,
        int times = 0
    ) {
        print_ip(dst.ipv4, "Pinging "); printf("\n");
        for (int i = 0; i < times; ++i) {
            send_icmp_echo(dst, ICMP_REQUEST, get_timestamp_milliseconds(), audio_way);
            icmp_echo_received = false;
            std::unique_lock<std::mutex> ulock(mtx_);
            bool timeout = cv_.wait_for(ulock, std::chrono::milliseconds(PING_TIMEOUT_MS),
                [this]() { return icmp_echo_received.load(); });
        }
    }

    bool send_icmp_echo(
        const ADDRESS dst_addr,
        uint8_t type,
        uint64_t time_stamp,
        int audio_way = false
    ) {
        IcmpEchoHeader icmp_header;
		icmp_header.type = type; 
		icmp_header.time_stamp = time_stamp;
        return send_ipv4_packet(
            dst_addr,
            ICMP_ECHO, // Protocol = ICMP
            (uint8_t*)&icmp_header,
            ICMP_ECHO_HDR_LEN,
            audio_way
		);
    }

    // Send IPv4 packet (Ethernet+IPv4 layers)
    bool send_ipv4_packet(
        const ADDRESS dst_addr,
        uint8_t protocol,             // Upper layer protocol (TCP=6, UDP=17, etc.)
        const uint8_t* payload = nullptr,  // IPv4 payload
        int payload_len = 0,           // Payload length
        bool audio_way = false
    ) {
        
        if (!dev_handle_) {
            fprintf(stderr, "Adapter not opened! Cannot send IPv4 packet.\n");
            return false;
        }

        // Allocate frame buffer (max Ethernet frame size = 1514 bytes)
        uint8_t frame[1514] = { 0 };
        int frame_len = build_ipv4_frame(frame, dst_addr.mac, dst_addr.ipv4, protocol, payload, payload_len);

        if (frame_len <= 0) {
            return false;
        }

        // Send frame via libpcap
        if (audio_way) {
            std::deque<bool> data;
            uint8_to_deque_bool(frame, frame_len, data);
            mac.send_data(data, dst_addr.customized_mac);
        }
        else if (pcap_sendpacket(dev_handle_, frame, frame_len) != 0) {
            fprintf(stderr, "IPv4 send failed: %s\n", pcap_geterr(dev_handle_));
            return false;
        }

        // Print success info
        /*printf("\n===== IPv4 Packet Sent Successfully =====\n");
        printf("--- Data Link Layer (Ethernet) ---\n");
        print_mac(dst_mac, "Destination MAC: ");
        print_mac(src_mac, "Source MAC:      ");
        printf("--- Network Layer (IPv4) ---\n");
        print_ip(src_ip, "Source IP:       ");
        print_ip(dst_ip, "Destination IP:  ");
        printf("Protocol:        %d\n", protocol);
        printf("Payload Length:  %d bytes\n", payload_len);
        printf("Total Frame Len: %d bytes\n", frame_len);
        printf("=========================================\n");*/

        return true;
    }


    bool start_capture() {
        std::lock_guard<std::mutex> lock(mtx_);
        if (is_capturing_ || !dev_handle_) {
            fprintf(stderr, "Capture already running or adapter closed!\n");
            return false;
        }

        is_capturing_ = true;
        capture_thread_ = std::thread(&IpV4PacketHandler::capture_loop, this);
        audio_capture_thread = std::thread(&IpV4PacketHandler::audio_capture, this);
        return true;
    }

    void stop_capture() {
        std::lock_guard<std::mutex> lock(mtx_);
        if (is_capturing_ && dev_handle_) {
            pcap_breakloop(dev_handle_);
            is_capturing_ = false;
            audio_thread_stop = true;
            printf("\nStopping IPv4 capture...\n");
        }
    }
};