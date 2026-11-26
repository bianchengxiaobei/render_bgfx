#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

int main(int argc, char** argv) {
    std::string standardOutput;
    std::string standardError;
    std::string writePath;
    std::string writeText;
    int exitCode = 0;
    int sleepMs = 0;
    bool echoArguments = false;

    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--stdout" && i + 1 < argc) {
            standardOutput = argv[++i];
        } else if (argument == "--stderr" && i + 1 < argc) {
            standardError = argv[++i];
        } else if (argument == "--write" && i + 1 < argc) {
            writePath = argv[++i];
        } else if (argument == "--write-text" && i + 1 < argc) {
            writeText = argv[++i];
        } else if (argument == "--exit" && i + 1 < argc) {
            exitCode = std::atoi(argv[++i]);
        } else if (argument == "--sleep-ms" && i + 1 < argc) {
            sleepMs = std::atoi(argv[++i]);
        } else if (argument == "--echo-arguments") {
            echoArguments = true;
        }
    }

    if (!standardOutput.empty()) {
        std::cout << standardOutput << '\n';
    }
    if (!standardError.empty()) {
        std::cerr << standardError << '\n';
    }
    if (echoArguments) {
        for (int i = 0; i < argc; ++i) {
            std::cout << argv[i] << '\n';
        }
    }
    if (!writePath.empty()) {
        std::ofstream stream(writePath, std::ios::binary | std::ios::trunc);
        stream.write(writeText.data(), static_cast<std::streamsize>(writeText.size()));
    }
    std::cout.flush();
    std::cerr.flush();
    if (sleepMs > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }
    return exitCode;
}
