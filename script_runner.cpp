#include "script_runner.hpp"

#include "acf_shell_iface.hpp"
#include "sdbus_calls_runner.hpp"
int main(int argc, char* argv[])
{
    using namespace scrrunner;
    getLogger().setLogLevel(LogLevel::DEBUG);
    LOG_INFO("Starting script runner");
    net::io_context io_context;
    ScriptRunner scriptRunner(io_context);
    AcfShellIface shellIface(io_context, scriptRunner);
    if (argc > 1)
    {
        std::string script = argv[1];
        net::co_spawn(
            io_context,
            std::bind_front(&AcfShellIface::execute, &shellIface, script),
            net::detached);
    }
    io_context.run();
    return 0;
}
