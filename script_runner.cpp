#include "script_runner.hpp"

#include "script_dbus_interfce.hpp"
#include "sdbus_calls_runner.hpp"
int main(int argc, char* argv[])
{
    using namespace scrrunner;
    getLogger().setLogLevel(LogLevel::INFO);
    LOG_INFO("Starting script runner");
    net::io_context io_context;
    ScriptRunner scriptRunner(io_context);
    ScriptInterface scriptInterface(io_context, scriptRunner);
    if (argc > 1)
    {
        std::string script = argv[1];
        net::co_spawn(
            io_context,
            std::bind_front(&ScriptInterface::runScript, &scriptInterface,
                            std::string("newid"), script),
            net::detached);
    }
    io_context.run();
    return 0;
}
