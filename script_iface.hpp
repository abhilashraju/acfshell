#pragma once
#include "script_runner.hpp"
#include "sdbus_calls_runner.hpp"

#include <format>
namespace scrrunner
{
struct ScriptIface
{
    struct Data
    {
        std::string script;
        std::string id;
        uint64_t timeout;
        bool dumpNeeded;
    };
    static constexpr auto scriptPath = "/xyz/openbmc_project/acfshell/{}";
    static constexpr auto scriptInterface = "xyz.openbmc_project.TacfScript";
    static constexpr std::string_view busName = "xyz.openbmc_project.acfshell";
    ScriptIface(net::io_context& ioc, ScriptRunner& scriptRunner,
                const Data& data, sdbusplus::asio::object_server& objServer) :
        io_context(ioc), scriptRunner(scriptRunner), data(data),
        objServer(objServer),
        timer(std::make_shared<boost::asio::steady_timer>(io_context))
    {
        std::string path = std::format(scriptPath, data.id);
        // Create the D-Bus object
        dbusIface = objServer.add_interface(path.data(), scriptInterface);

        // Register the cancel method
        dbusIface->register_method("cancel", [this]() { return cancel(); });
        dbusIface->initialize();
    }
    ~ScriptIface()
    {
        timer->cancel();
        objServer.remove_interface(dbusIface);
    }
    bool cancel()
    {
        bool success = scriptRunner.cancel_script(data.id);
        if (!success)
        {
            LOG_ERROR("Failed to cancel script");
            return false;
        }
        return success;
    }
    void startTimeout()
    {
        if (data.timeout == 0)
        {
            return;
        }
        timer->expires_after(std::chrono::seconds(data.timeout));
        timer->async_wait(
            [this, timer = timer](const boost::system::error_code& ec) {
                if (ec)
                {
                    return;
                }
                LOG_ERROR("Script {} timed out", data.id);
                cancel();
            });
    }
    net::io_context& io_context;
    ScriptRunner& scriptRunner;
    Data data;
    sdbusplus::asio::object_server& objServer;
    std::shared_ptr<sdbusplus::asio::dbus_interface> dbusIface;
    std::shared_ptr<boost::asio::steady_timer> timer;
};
} // namespace scrrunner
