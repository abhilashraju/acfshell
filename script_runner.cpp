#include "script_runner.hpp"

#include "acf_shell_iface.hpp"
#include "sdbus_calls_runner.hpp"
int main(int argc, char* argv[])
{
    using namespace scrrunner;
    getLogger().setLogLevel(LogLevel::DEBUG);
    LOG_INFO("Starting script runner");
    net::io_context io_context;
    auto conn = std::make_shared<sdbusplus::asio::connection>(io_context);
    ScriptRunner scriptRunner(io_context, conn);
    AcfShellIface shellIface(io_context, scriptRunner, conn);
    if (argc > 1)
    {
        std::string script = argv[1];
        std::ifstream file(script);
        if (!file)
        {
            LOG_ERROR("Failed to open script file: {}", script);
            return 1;
        }
        std::string scriptContent((std::istreambuf_iterator<char>(file)),
                                  std::istreambuf_iterator<char>());
        net::co_spawn(io_context,
                      std::bind_front(&AcfShellIface::execute, &shellIface,
                                      scriptContent),
                      net::detached);
    }
    io_context.run();
    return 0;
}
