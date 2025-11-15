#include <iostream>
#include <fstream>
#include <string>
#include <ctime>

class Log_Handlr {
private:
    std::ofstream log_file;

public:

    Log_Handlr(){}

    void init(const std::string& filename) {
        log_file.open(filename, std::ios::out | std::ios::trunc);
    }
    ~Log_Handlr() {
        log_file.close();
    }

    void log_message(const std::string& message) {
        if (!log_file.is_open()) {
            std::cerr << "日志文件未初始化" << std::endl;
            return;
        }

        std::time_t now = std::time(nullptr);
        std::string timestamp = std::ctime(&now);
        if (!timestamp.empty() && timestamp.back() == '\n') {
            timestamp.pop_back();
        }

        log_file << "[" << timestamp << "] " << message << std::endl;
        log_file.flush();
    }
};




