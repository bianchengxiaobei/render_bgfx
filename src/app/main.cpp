#include "app/application.h"
#include "app/backend.h"
#include "core/log.h"

#include <cstdio>

int main(int argc, char** argv) {
    hbg::log::init();

    const hbg::CommandLineResult commandLine = hbg::parseCommandLine(argc, argv);
    if (commandLine.help) {
        std::fputs(hbg::usage(), stdout);
        return 0;
    }
    if (!commandLine.ok) {
        std::fputs(hbg::usage(), stderr);
        return 2;
    }

    hbg::Application application;
    return application.run(commandLine.options);
}
