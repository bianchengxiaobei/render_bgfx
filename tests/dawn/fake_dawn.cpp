#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

namespace {

std::string environmentValue(const char* name) {
#ifdef _MSC_VER
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) {
        return {};
    }
    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value != nullptr ? std::string(value) : std::string();
#endif
}

} // namespace

int main() {
    const std::string resultPath = environmentValue("HBG_FAKE_DAWN_RESULT");
    if (!resultPath.empty()) {
        std::ofstream result(resultPath, std::ios::binary | std::ios::trunc);
        result << "fake dawn lmgz";
    }

    const std::string sleepText = environmentValue("HBG_FAKE_DAWN_SLEEP_MS");
    if (!sleepText.empty()) {
        const int milliseconds = std::stoi(sleepText);
        if (milliseconds > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
        }
    }

    std::cout << "fake dawn complete\n";
    return 0;
}
