#pragma once
#include "script_runner.hpp"
#include "sdbus_calls_runner.hpp"

#include <format>
namespace scrrunner
{
struct ScriptIface
{
    static constexpr auto scriptPath = "/xyz/openbmc_project/acfshell/{}";
    static constexpr auto scriptInterface = "xyz.openbmc_project.TacfScript";
    static constexpr std::string_view busName = "xyz.openbmc_project.acfshell";
    ScriptIface(net::io_context& ioc, ScriptRunner& scriptRunner,
                const std::string& id, const std::string& script,
                sdbusplus::asio::object_server& objServer) :
        io_context(ioc), scriptRunner(scriptRunner), id(id), script(script),
        objServer(objServer)
    {
        LOG_DEBUG("Creating script interface: {}", id);
        std::string path = std::format(scriptPath, id);
        // Create the D-Bus object
        dbusIface = objServer.add_interface(path.data(), scriptInterface);

        // Register the cancel method
        dbusIface->register_method("cancel", [this]() { return cancel(); });
        dbusIface->initialize();
    }
    ~ScriptIface()
    {
        objServer.remove_interface(dbusIface);
    }
    bool cancel()
    {
        bool success = scriptRunner.cancel_script(id);
        if (!success)
        {
            LOG_ERROR("Failed to cancel script");
            return false;
        }
        return success;
    }
    net::io_context& io_context;
    ScriptRunner& scriptRunner;
    std::string id;
    std::string script;
    sdbusplus::asio::object_server& objServer;
    std::shared_ptr<sdbusplus::asio::dbus_interface> dbusIface;
};
} // namespace scrrunner
