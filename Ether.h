#include "Utils.h"
#include "MAC.h"


#define PING_TIMEOUT_MS 2000

#define ICMP 1
#define ICMP_REPLY 0
#define ICMP_REQUEST 8
#define DST_ADDRESS 1

struct ADDRESS {
    uint32_t ipv4;
    uint8_t mac[6];

    ADDRESS() = default;
    ADDRESS(uint32_t ip, const uint8_t* phy_mac) {
        ipv4 = ip;
        memcpy(mac, phy_mac, 6);
    }
    ADDRESS(char* ip_str, char* mac_str) {
        ipv4 = inet_addr(ip_str);
        mac_str_to_uint8(mac_str, mac);
    }
};


#pragma pack(push, 1) // Force 1-byte alignment

struct EthHeader {
    uint8_t dst_mac[6];   // Destination MAC address
    uint8_t src_mac[6];   // Source MAC address
    uint16_t eth_type;    // EtherType (0x0800 = IPv4)
};

struct Ipv4Header {
    uint8_t version_ihl;    // 0x45
    uint8_t tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t frag_off;
    uint8_t ttl;
    uint8_t protocol;       // ICMP = 1
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dst_ip;
};

struct IcmpHeader {
    uint8_t type;           // Request=8, Reply=0
    uint8_t code;           // 0
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
};

#pragma pack(pop) // Restore default alignment

// Callback parameter encapsulation
struct CallbackParam {
    class IpV4PacketHandler* handler;
    void* user_data;
};


#define ETH_HDR_LEN sizeof(EthHeader)
#define IP_HDR_LEN sizeof(Ipv4Header)
#define ICMP_HDR_LEN sizeof(IcmpHeader)
#define VLAN_HDR_LEN 4

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
    std::mutex audio_mtx;                  // 互斥锁，保护条件变量和共享标志
    std::condition_variable audio_cv;      // 条件变量，用于唤醒线程
    bool audio_wakeup = false;          // 唤醒标志（核心：等待的“条件”）

    std::mutex mtx_;              // Thread safety mutex
    std::condition_variable cv_;

    std::atomic<bool> icmp_echo_received = false;

    uint8_t audio_scam_buffer[1514];
    size_t audio_scam_len = 0;

    ADDRESS local_address;

    struct ROUTE_ENTRY {
        std::unique_ptr<uint8_t[]> mac;
        int audio_addr;
        bool audio_way;
    };

    std::unordered_map<uint32_t, ROUTE_ENTRY> routing_table;

    // Disable copy/assignment (prevent handle leaks)
    IpV4PacketHandler(const IpV4PacketHandler&) = delete;
    IpV4PacketHandler& operator=(const IpV4PacketHandler&) = delete;

    
    void print_ip(uint32_t ip, const char* prefix) {
        printf("%s%s", prefix, inet_ntoa(*(in_addr*)&ip));
    }

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

    void print_packet_info(
        const uint32_t src_ip, const uint32_t dst_ip,
        const uint8_t* src_mac, const uint8_t* dst_mac,
        const uint8_t* payload_ptr,
        size_t payload_len, bool audio_way, bool send_packet) {

        printf("\n=====================================\n");
        printf("IPv4 Packet %s\n", send_packet ? "Sent" : "Captured");
        printf("-------------------------------------\n");

        // Data Link Layer (Ethernet)
        //printf("--- Data Link Layer (Ethernet) ---\n");
        print_mac(dst_mac, "Destination MAC: ");
        print_mac(src_mac, "Source MAC:      ");
        //printf("EtherType:       0x%04x (IPv4)\n", ntohs(eth->eth_type));

        // Network Layer (IPv4)
        printf("--- Network Layer (IPv4) ---\n");
        print_ip(src_ip, "Source IP:       "); printf("\n");
        print_ip(dst_ip, "Destination IP:  "); printf("\n");
        //printf("IP Version:      %d\n", (ip->version_ihl >> 4) & 0x0F);
        //printf("IP Header Len:   %d bytes\n", (ip->version_ihl & 0x0F) * 4);
        //printf("IP TTL:          %d\n", ip->ttl);
        //printf("Protocol:        %d\n", ip->protocol);
        //printf("IP Total Len:    %d bytes\n", ntohs(ip->total_len));
        //printf("IP Checksum:     0x%04x\n", ntohs(ip->checksum));
        printf("Interface: %s\n", audio_way ? "Audio" : "Internet");
        if (payload_len > 0) {
            printf("--- IPv4 Payload (%d bytes) ---\n", payload_len);
            print_hex_ascii(payload_ptr, payload_len);
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
        const ADDRESS& src_addr,
        const ADDRESS& dst_addr,
        uint8_t protocol,             // Upper layer protocol (TCP/UDP/etc.)
        const uint8_t* payload,       // IPv4 payload
        int payload_len
    ) {
        // Frame size calculations
        const int IP_TOTAL_LEN = IP_HDR_LEN + payload_len;
        const int FRAME_TOTAL_LEN = ETH_HDR_LEN + IP_TOTAL_LEN;

        // Validate maximum frame size (Ethernet MTU = 1500 + 14 byte header)
        if (FRAME_TOTAL_LEN > 1514) {
            fprintf(stderr, "IPv4 frame too large (max 1514 bytes)\n");
            return -1;
        }

        
        EthHeader* eth = (EthHeader*)frame;
        memcpy(eth->dst_mac, dst_addr.mac, 6);
        memcpy(eth->src_mac, src_addr.mac, 6);
        eth->eth_type = htons(0x0800); 
        frame += ETH_HDR_LEN;
        
        // 2. Fill IPv4 header
        Ipv4Header* ip = (Ipv4Header*)frame;
        ip->version_ihl = 0x45;        // 4 = IPv4, 5 = 20-byte header
        ip->tos = 0;                   // Default TOS
        ip->total_len = htons(IP_TOTAL_LEN);
        ip->id = htons(rand() % 65535); // Random identification
        ip->frag_off = 0;              // No fragmentation
        ip->ttl = 64;                  // Standard TTL value
        ip->protocol = protocol;       // Upper layer protocol
        ip->src_ip = src_addr.ipv4;           // Host → network byte order
        ip->dst_ip = dst_addr.ipv4;
        ip->checksum = 0;              // Zero before checksum calculation
        ip->checksum = calculate_checksum((uint8_t*)ip, IP_HDR_LEN);
        frame += IP_HDR_LEN;

        if (payload_len > 0 && payload != nullptr) {
            memcpy(frame, payload, payload_len);
        }

        return FRAME_TOTAL_LEN;
    }


    void parse_ipv4_packet(bpf_u_int32 caplen, const uint8_t* pkt_data) {

        if (caplen < ETH_HDR_LEN + IP_HDR_LEN) {
			printf("Captured packet too short for IPv4\n");
            return;
        }
        EthHeader* eth = (EthHeader*)pkt_data;

        int ip_offset = ETH_HDR_LEN;
        if (ntohs(eth->eth_type) == 0x800) ip_offset = ETH_HDR_LEN;
        else if (ntohs(eth->eth_type) == 0x8100) ip_offset = ETH_HDR_LEN + VLAN_HDR_LEN;

        Ipv4Header* ip = (Ipv4Header*)(pkt_data + ip_offset);

        // Calculate payload length
        int ip_payload_len = ntohs(ip->total_len) - IP_HDR_LEN;
        int frame_payload_len = caplen - ip_offset - IP_HDR_LEN;
        int actual_payload_len = (ip_payload_len < frame_payload_len) ? ip_payload_len : frame_payload_len;
        u_char* payload_ptr = (u_char*)ip + IP_HDR_LEN;

        if (!compare_mac(eth->dst_mac, local_address.mac)) return;
        
        print_packet_info(ip->src_ip, ip->dst_ip, eth->src_mac, eth->dst_mac, payload_ptr, actual_payload_len, false, false);

        add_to_routing_table(ip->src_ip, eth->src_mac);

        if (ip->dst_ip != local_address.ipv4) {
            
            ROUTE_ENTRY& entry = routing_table[ip->dst_ip];

            if (entry.audio_way) {

                EthHeader* r_eth = (EthHeader*)audio_scam_buffer;
                Ipv4Header* r_ip = (Ipv4Header*)(audio_scam_buffer + ETH_HDR_LEN);
                IcmpHeader* r_icmp = (IcmpHeader*)(audio_scam_buffer + ETH_HDR_LEN + IP_HDR_LEN);

                memcpy(r_eth->dst_mac, eth->src_mac, 6);
                memcpy(r_eth->src_mac, eth->dst_mac, 6);
                r_eth->eth_type = htons(0x800);

                memcpy(r_ip, ip, IP_HDR_LEN);
                r_ip->dst_ip = ip->src_ip;
                r_ip->src_ip = ip->dst_ip;
                r_ip->checksum = 0;
                r_ip->checksum = calculate_checksum((uint8_t*)r_ip, IP_HDR_LEN);

                memcpy(r_icmp, payload_ptr, actual_payload_len);
                r_icmp->type = ICMP_REPLY; 
                r_icmp->checksum = 0;
                r_icmp->checksum = calculate_checksum((uint8_t*)r_icmp, actual_payload_len);

                audio_scam_len = caplen;

                mac.send_icmp_request(entry.audio_addr, ip->src_ip);
                return;
            }
            ADDRESS src_addr(ip->src_ip, local_address.mac);
            ADDRESS dst_addr(ip->dst_ip, entry.mac.get());
            send_ipv4_packet(src_addr, dst_addr, ip->protocol, payload_ptr, actual_payload_len);
            return;
        }
        
        if (ip->protocol == ICMP) {
            ADDRESS src_addr(ip->src_ip, eth->src_mac);
            ADDRESS dst_addr(ip->dst_ip, eth->dst_mac);
            handle_icmp_echo(payload_ptr, actual_payload_len, src_addr, dst_addr);
		}
    }

    bool handle_icmp_echo(
        uint8_t* payload, 
        int payload_len, 
		const ADDRESS& src_addr,
        const ADDRESS& dst_addr
    ) {
		IcmpHeader* icmp_header = (IcmpHeader*)payload;
        
        if (icmp_header->type == ICMP_REQUEST) { // ICMP Echo Request
            icmp_header->type = ICMP_REPLY;
            icmp_header->checksum = 0;
            icmp_header->checksum = calculate_checksum(payload, payload_len);
            return send_ipv4_packet(dst_addr, src_addr, ICMP, payload, payload_len);
        }
        else if (icmp_header->type == ICMP_REPLY) { // ICMP Echo Reply
            if (ntohs(icmp_header->id) == 1) {
                mac.send_icmp_reply(DST_ADDRESS);
                return true;
            }
			icmp_echo_received.store(true);
            cv_.notify_all();
            return true;
		}
    }

    // Capture callback (forward to class method)
    void on_packet_captured(const struct pcap_pkthdr* header, const uint8_t* pkt_data) {
        // Capture timestamp
        /*time_t ts = header->ts.tv_sec;
        struct tm* tm = localtime(&ts);
        printf("Capture Time:    %04d-%02d-%02d %02d:%02d:%02d.%06d\n",
            tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
            tm->tm_hour, tm->tm_min, tm->tm_sec, (int)header->ts.tv_usec);*/
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
        while (!audio_thread_stop) {
            std::unique_lock<std::mutex> lock(audio_mtx);
            audio_cv.wait(lock, [this]() { return audio_wakeup; });

            printf("Waking up, start process\n");
            std::pair<int, std::deque<bool>> receiving_buffer;
            while (inter_fifo.pop(receiving_buffer)) {
                int type = receiving_buffer.first;
                if (type == ICMP_REPLY_TYPE) {
                    if (audio_scam_len != 0) {
                        printf("Sending fake icmp\n");
                        pcap_sendpacket(dev_handle_, audio_scam_buffer, audio_scam_len);
                    }
                    else {
                        printf("Reply by audio interface\n");
                        icmp_echo_received.store(true);
                        cv_.notify_all();
                    }
                }
                if (type == ICMP_REQUEST_TYPE) {
                    
                    uint32_t ip = decode_header(256, receiving_buffer.second);
                    if (ip == local_address.ipv4) {
                        mac.send_icmp_reply(DST_ADDRESS);
                    }
                    else {
                        ADDRESS dst(ip, routing_table[ip].mac.get());
                        print_ip(ip, "Forwarding to "); printf("\n");
                        send_icmp_echo(local_address, dst, ICMP_REQUEST, 1);
                    }
                }
            }
            audio_wakeup = false;
        }
    }

public:
    
	IpV4PacketHandler(MAC& MAC, Mutex_FIFO<std::pair<int, std::deque<bool>>>& INTER_FIFO, const ADDRESS& Local_addr)
        : dev_handle_(nullptr), dev_index_(0), promisc_mode_(0), is_capturing_(false), mac(MAC), inter_fifo(INTER_FIFO), local_address(Local_addr) {

        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);

        srand(time(nullptr)); // Seed for random IPv4 ID

        mac.set_ipv4_lock(&audio_mtx, &audio_cv, &audio_wakeup);
        list_devices();
        int dev_index = 0;
        std::cout << "Select the device index to capture IPv4 packets: ";
        std::cin >> dev_index;
        open_device(dev_index);
    }

    ~IpV4PacketHandler() {
        stop_capture();
        if (dev_handle_) {
            pcap_close(dev_handle_);
            dev_handle_ = nullptr;
        }
        if (capture_thread_.joinable()) {
            capture_thread_.join();
        }
        if (audio_capture_thread.joinable()) {
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

    int sockaddr_to_str(const struct sockaddr* sa, char* addr_str, size_t str_len) {
        if (sa == nullptr || addr_str == nullptr) {
            return -1;
        }
        memset(addr_str, 0, str_len);

        switch (sa->sa_family) {
        case AF_INET: { // IPv4 地址
            struct sockaddr_in* sin = (struct sockaddr_in*)sa;
            inet_ntop(AF_INET, &sin->sin_addr, addr_str, str_len);
            break;
        }
        case AF_INET6: { // IPv6 地址
            struct sockaddr_in6* sin6 = (struct sockaddr_in6*)sa;
            inet_ntop(AF_INET6, &sin6->sin6_addr, addr_str, str_len);
            break;
        }
        default: { // 未知地址类型（如AF_PACKET）
            snprintf(addr_str, str_len, "Unknown family (%d)", sa->sa_family);
            return -1;
        }
        }
        return 0;
    }

    void print_pcap_addr(const struct pcap_addr* pcap_addr_list, const char* prefix) {
        if (pcap_addr_list == nullptr) {
            printf("%sNo address information available\n", prefix);
            return;
        }
        const int MAX_ADDR_STR_LEN = 64;
        const struct pcap_addr* addr_item = pcap_addr_list;
        int addr_idx = 0;

        // 遍历所有地址项（一个网卡可能绑定多个IP）
        while (addr_item != nullptr) {
            char addr_str[MAX_ADDR_STR_LEN] = { 0 };

            printf("%sAddress #%d:\n", prefix, ++addr_idx);

            // 1. 输出IP地址
            if (sockaddr_to_str(addr_item->addr, addr_str, sizeof(addr_str)) == 0) {
                printf("%s  IP Address: %s\n", prefix, addr_str);
            }
            else {
                printf("%s  IP Address: (none)\n", prefix);
            }
            // 下一个地址项
            addr_item = addr_item->next;
        }
    }

    // Open network adapter
    bool open_device(int dev_index, int promisc_mode = 1, const char* filter = "icmp") {

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
            1,             // Timeout (ms)
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
        print_pcap_addr(dev->addresses, "");
        pcap_freealldevs(alldevs);
        return true;
    }

    void add_to_routing_table(const uint32_t ip, const uint8_t* mac, bool audio_way = false, int audio_addr = -1) {
        if (mac == nullptr) {
            return; 
        }
        ROUTE_ENTRY entry;
        entry.audio_way = audio_way;
        if (audio_way && audio_addr == -1) {
            printf("Missing audio address\n");
            return;
        }
        entry.audio_addr = audio_addr;
        entry.mac = std::make_unique<uint8_t[]>(6);
        std::memcpy(entry.mac.get(), mac, 6);

        routing_table[ip] = std::move(entry); 
        return;
    }
    void add_to_routing_table(const char* ip_str, const char* mac_str, bool audio_way = false, int audio_addr = -1) {
        uint8_t mac[6];
        mac_str_to_uint8(mac_str, mac);
        add_to_routing_table(inet_addr(ip_str), mac, audio_way, audio_addr);
    }
    void add_to_routing_table(const ADDRESS& addr, bool audio_way = false, int audio_addr = -1) {
        add_to_routing_table(addr.ipv4, addr.mac, audio_way, audio_addr);
    }

    void pinging(
        const ADDRESS& src_addr,
		const ADDRESS& dst_addr,          // Destination address info
        int audio_addr = -1,
        int audio_way = false,
        int times = 0
    ) {
        print_ip(dst_addr.ipv4, "Pinging "); printf("\n");
        for (int i = 0; i < times; ++i) {
            if (audio_way) {
                mac.send_icmp_request(audio_addr, dst_addr.ipv4);
            }
            else {
                send_icmp_echo(src_addr, dst_addr, ICMP_REQUEST);
            }
            uint64_t send_time = get_timestamp_milliseconds();
            icmp_echo_received = false;
            std::unique_lock<std::mutex> ulock(mtx_);
            bool timeout = cv_.wait_for(ulock, std::chrono::milliseconds(PING_TIMEOUT_MS),
                [this]() { return icmp_echo_received.load(); });
            uint64_t rtt = get_timestamp_milliseconds() - send_time;
            print_ip(src_addr.ipv4, "Reply from ");
            printf(": time=%lums\n", rtt);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    bool send_icmp_echo(
        const ADDRESS& src_addr,
        const ADDRESS& dst_addr,
        uint8_t type,
        int id = 0
    ) {
        IcmpHeader icmp_header;
		icmp_header.type = type; 
        icmp_header.code = 0;
        icmp_header.id = htons(id);
        icmp_header.seq = htons(1);
        icmp_header.checksum = 0;
        icmp_header.checksum = calculate_checksum((uint8_t*)& icmp_header, ICMP_HDR_LEN);
        return send_ipv4_packet(src_addr, dst_addr, ICMP, (uint8_t*)&icmp_header, ICMP_HDR_LEN);
    }

    // Send IPv4 packet (Ethernet+IPv4 layers)
    bool send_ipv4_packet(
        const ADDRESS& src_addr,
        const ADDRESS& dst_addr,
        uint8_t protocol,             // Upper layer protocol (TCP=6, UDP=17, etc.)
        const uint8_t* payload = nullptr,  // IPv4 payload
        int payload_len = 0
    ) {
        
        if (!dev_handle_) {
            fprintf(stderr, "Adapter not opened! Cannot send IPv4 packet.\n");
            return false;
        }

        // Allocate frame buffer (max Ethernet frame size = 1514 bytes)
        uint8_t frame[1514] = { 0 };
        int frame_len = build_ipv4_frame(frame, src_addr, dst_addr, protocol, payload, payload_len);

        if (frame_len <= 0) {
            return false;
        }

        if (pcap_sendpacket(dev_handle_, frame, frame_len) != 0) {
            fprintf(stderr, "IPv4 send failed: %s\n", pcap_geterr(dev_handle_));
            return false;
        }

        //print_packet_info(src_addr.ipv4, dst_addr.ipv4, src_addr.mac, dst_addr.mac, payload, payload_len, audio_way, true);

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