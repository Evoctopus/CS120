#include <windows.h>
#include <iostream>
#include <string>
#include <cstring>
#include <vector>
#include <cstdint>
#include <thread>
#include <chrono>

class SharedMemoryComm {
private:
    HANDLE hMapFile;
    LPVOID pBuf;
    HANDLE hWriteEvent;
    HANDLE hReadEvent;
    std::string name;
    size_t size;
    bool is_creator;

public:
    SharedMemoryComm(const std::string& name, size_t size = 4096, bool is_creator = false)
        : name(name), size(size), is_creator(is_creator), hMapFile(nullptr), pBuf(nullptr),
        hWriteEvent(nullptr), hReadEvent(nullptr) {

        if (is_creator) {
            CreateSharedMemory();
            CreateEvents();
        }
        else {
            OpenSharedMemory();
            OpenEvents();
        }
    }

    ~SharedMemoryComm() {
        Close();
    }

private:
    void CreateSharedMemory() {
        try {
            // Create file mapping
            hMapFile = CreateFileMappingW(
                INVALID_HANDLE_VALUE,   // Use page file
                nullptr,
                PAGE_READWRITE,
                0,
                static_cast<DWORD>(size),
                std::wstring(name.begin(), name.end()).c_str()
            );

            if (hMapFile == nullptr) {
                throw std::runtime_error("Failed to create file mapping");
            }

            // Map view of file
            pBuf = MapViewOfFile(
                hMapFile,
                FILE_MAP_ALL_ACCESS,
                0,
                0,
                size
            );

            if (pBuf == nullptr) {
                throw std::runtime_error("Failed to map view of file");
            }
        }
        catch (const std::exception& e) {
            std::cerr << "[C++] Error creating shared memory: " << e.what() << std::endl;
            throw;
        }
    }

    void OpenSharedMemory() {
        try {
            // Open file mapping
            hMapFile = OpenFileMappingW(
                FILE_MAP_ALL_ACCESS,
                FALSE,
                std::wstring(name.begin(), name.end()).c_str()
            );

            if (hMapFile == nullptr) {
                throw std::runtime_error("Failed to open file mapping");
            }

            // Map view of file
            pBuf = MapViewOfFile(
                hMapFile,
                FILE_MAP_ALL_ACCESS,
                0,
                0,
                size
            );

            if (pBuf == nullptr) {
                throw std::runtime_error("Failed to map view of file");
            }
        }
        catch (const std::exception& e) {
            std::cerr << "[C++] Error opening shared memory: " << e.what() << std::endl;
            throw;
        }
    }

    void CreateEvents() {
        try {
            // Create write event (C++ -> Python)
            std::string write_event_name = name + "_WRITE_EVENT";
            hWriteEvent = CreateEventW(
                nullptr,
                TRUE,   // Manual reset
                FALSE,  // Initial state (not signaled)
                std::wstring(write_event_name.begin(), write_event_name.end()).c_str()
            );

            if (hWriteEvent == nullptr) {
                throw std::runtime_error("Failed to create write event");
            }

            // Create read event (Python -> C++)
            std::string read_event_name = name + "_READ_EVENT";
            hReadEvent = CreateEventW(
                nullptr,
                TRUE,   // Manual reset
                FALSE,  // Initial state (not signaled)
                std::wstring(read_event_name.begin(), read_event_name.end()).c_str()
            );

            if (hReadEvent == nullptr) {
                throw std::runtime_error("Failed to create read event");
            }

            std::cout << "[C++] Created signaling events" << std::endl;
        }
        catch (const std::exception& e) {
            std::cerr << "[C++] Error creating events: " << e.what() << std::endl;
            throw;
        }
    }

    void OpenEvents() {
        try {
            // Open write event
            std::string write_event_name = name + "_WRITE_EVENT";
            hWriteEvent = OpenEventW(
                SYNCHRONIZE | EVENT_MODIFY_STATE,
                FALSE,
                std::wstring(write_event_name.begin(), write_event_name.end()).c_str()
            );

            if (hWriteEvent == nullptr) {
                throw std::runtime_error("Failed to open write event");
            }

            // Open read event
            std::string read_event_name = name + "_READ_EVENT";
            hReadEvent = OpenEventW(
                SYNCHRONIZE | EVENT_MODIFY_STATE,
                FALSE,
                std::wstring(read_event_name.begin(), read_event_name.end()).c_str()
            );

            if (hReadEvent == nullptr) {
                throw std::runtime_error("Failed to open read event");
            }

            std::cout << "[C++] Opened signaling events" << std::endl;
        }
        catch (const std::exception& e) {
            std::cerr << "[C++] Error opening events: " << e.what() << std::endl;
            throw;
        }
    }

public:
    void WriteData(const std::vector<uint8_t>& data) {
        try {
            // Prepare packet: [4-byte length][data]
            uint32_t data_len = static_cast<uint32_t>(data.size());
            size_t packet_size = sizeof(uint32_t) + data_len;

            if (packet_size > size) {
                throw std::runtime_error("Data too large for shared memory");
            }

            // Write length
            memcpy(pBuf, &data_len, sizeof(uint32_t));

            // Write data
            if (data_len > 0) {
                memcpy(static_cast<char*>(pBuf) + sizeof(uint32_t), data.data(), data_len);
            }

            // Signal the write event
            if (!SetEvent(hWriteEvent)) {
                throw std::runtime_error("Failed to set write event");
            }

            //std::cout << "[C++] Wrote " << data_len << " bytes and signaled" << std::endl;
        }
        catch (const std::exception& e) {
            std::cerr << "[C++] Error writing data: " << e.what() << std::endl;
            throw;
        }
    }

    std::vector<uint8_t> ReadData(DWORD timeout = INFINITE) {
        try {
            // Wait for write event
            DWORD dwWait = WaitForSingleObject(hWriteEvent, timeout);

            if (dwWait == WAIT_OBJECT_0) {
                // Read length
                uint32_t data_len = 0;
                memcpy(&data_len, pBuf, sizeof(uint32_t));

                if (data_len > size - sizeof(uint32_t)) {
                    throw std::runtime_error("Invalid data length");
                }

                // Read data
                std::vector<uint8_t> result(data_len);
                if (data_len > 0) {
                    memcpy(result.data(), static_cast<char*>(pBuf) + sizeof(uint32_t), data_len);
                }

                // Reset the write event
                if (!ResetEvent(hWriteEvent)) {
                    throw std::runtime_error("Failed to reset write event");
                }

                // Signal that we've read the data
                if (!SetEvent(hReadEvent)) {
                    throw std::runtime_error("Failed to set read event");
                }

                //std::cout << "[C++] Read " << data_len << " bytes" << std::endl;
                return result;
            }
            else if (dwWait == WAIT_TIMEOUT) {
                std::cout << "[C++] Wait timeout" << std::endl;
                return std::vector<uint8_t>();
            }
            else {
                throw std::runtime_error("WaitForSingleObject failed");
            }
        }
        catch (const std::exception& e) {
            std::cerr << "[C++] Error reading data: " << e.what() << std::endl;
            throw;
        }
    }

    bool WaitForAck(DWORD timeout = INFINITE) {
        try {
            DWORD dwWait = WaitForSingleObject(hReadEvent, timeout);

            if (dwWait == WAIT_OBJECT_0) {
                if (!ResetEvent(hReadEvent)) {
                    throw std::runtime_error("Failed to reset read event");
                }
                std::cout << "[C++] Received acknowledgment" << std::endl;
                return true;
            }
            else if (dwWait == WAIT_TIMEOUT) {
                std::cout << "[C++] Acknowledgment timeout" << std::endl;
                return false;
            }
            else {
                throw std::runtime_error("WaitForSingleObject failed");
            }
        }
        catch (const std::exception& e) {
            std::cerr << "[C++] Error waiting for ACK: " << e.what() << std::endl;
            throw;
        }
    }

    void Close() {
        try {
            if (pBuf != nullptr) {
                UnmapViewOfFile(pBuf);
                pBuf = nullptr;
            }

            if (hMapFile != nullptr) {
                CloseHandle(hMapFile);
                hMapFile = nullptr;
            }

            if (hWriteEvent != nullptr) {
                CloseHandle(hWriteEvent);
                hWriteEvent = nullptr;
            }

            if (hReadEvent != nullptr) {
                CloseHandle(hReadEvent);
                hReadEvent = nullptr;
            }

            std::cout << "[C++] Closed shared memory and events" << std::endl;
        }
        catch (const std::exception& e) {
            std::cerr << "[C++] Error closing: " << e.what() << std::endl;
        }
    }
};


