#include "logger.h"
#include <iostream>
#include <fstream>
#include <mutex>

namespace Logger {

std::mutex logMutex;
std::string currentProvider = "console";

void setProvider(const std::string& provider) {
    std::lock_guard<std::mutex> lock(logMutex);
    currentProvider = provider;
}

void log(const std::string& level, const std::string& message) {
    std::lock_guard<std::mutex> lock(logMutex);
    std::string formattedMessage = "[" + level + "] " + message;

    if (currentProvider == "console") {
        std::cout << formattedMessage << std::endl;
    } else if (currentProvider == "file") {
        std::ofstream logFile("log.txt", std::ios::app);
        if (logFile.is_open()) {
            logFile << formattedMessage << std::endl;
            logFile.close();
        }
    } else {
        std::cerr << "Unknown provider: " << currentProvider << std::endl;
    }
}

} // namespace Logger