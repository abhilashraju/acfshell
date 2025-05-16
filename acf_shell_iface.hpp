#pragma once
#include "script_iface.hpp"
#include "script_runner.hpp"
#include "sdbus_calls_runner.hpp"

#include <memory>
#include <vector>
namespace scrrunner
{
struct AcfShellIface
{
    net::io_context& io_context;
    ScriptRunner& scriptRunner;
    std::shared_ptr<sdbusplus::asio::connection> conn;
    sdbusplus::asio::object_server dbusServer;
    std::shared_ptr<sdbusplus::asio::dbus_interface> iface;
    static constexpr std::string_view busName = "xyz.openbmc_project.acfshell";
    static constexpr std::string_view objPath = "/xyz/openbmc_project/acfshell";
    static constexpr std::string_view interface =
        "xyz.openbmc_project.TacfShell";
    std::vector<std::unique_ptr<ScriptIface>> scriptIfaces;
    AcfShellIface(net::io_context& ioc, ScriptRunner& runner,
                  std::shared_ptr<sdbusplus::asio::connection> conn) :
        io_context(ioc), scriptRunner(runner), conn(conn), dbusServer(conn)
    {
        conn->request_name(busName.data());
        iface = dbusServer.add_interface(objPath.data(), interface.data());
        // test generic properties

        iface->register_method("active", [this]() {
            std::vector<std::string> activeScripts;
            for (const auto& iface : scriptIfaces)
            {
                activeScripts.push_back(iface->data.id);
            }
            return activeScripts;
        });

        iface->register_method(
            "start", [this](const std::string& script, uint64_t timeout,
                            bool dumpNeeded) {
                return addToActive(script, timeout, dumpNeeded);
            });
        iface->register_method("cancel", [this](const std::string& id) {
            auto iface = getScriptIface(id);
            if (iface)
            {
                return iface->cancel();
            }
            return false;
        });

        iface->initialize();
    }
    bool addToActive(const std::string& script, uint64_t timeout,
                     bool dumpNeeded)
    {
        auto scriptId = ScriptRunner::makeHash(script);

        LOG_DEBUG("Starting script: {}", scriptId.value());
        if (!scriptId)
        {
            LOG_ERROR("Failed to create script hash");
            return false;
        }
        try
        {
            auto iface = std::make_unique<ScriptIface>(
                io_context, scriptRunner,
                ScriptIface::Data{script, *scriptId, timeout, dumpNeeded},
                dbusServer);
            return runScript(std::move(iface));
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("Failed to create script interface: {}", e.what());
            return false;
        }
    }
    bool runScript(std::unique_ptr<ScriptIface> iface)
    {
        bool success = scriptRunner.run_script(
            iface->data.id, iface->data.script,
            std::bind_front(&AcfShellIface::onFinish, this));
        if (!success)
        {
            LOG_ERROR("Failed to start script");
            return false;
        }
        iface->startTimeout();
        scriptIfaces.push_back(std::move(iface));
        return success;
    }
    net::awaitable<void> execute(const std::string& script)
    {
        uint64_t timeout = 30;
        auto [ec, value] = co_await awaitable_dbus_method_call<bool>(
            *conn, busName.data(), objPath.data(), interface.data(), "start",
            script, timeout, true);

        if (ec)
        {
            LOG_ERROR("Error starting script: {}", ec.message());
        }
    }
    ScriptIface* getScriptIface(std::string scriptId)
    {
        auto it = std::find_if(scriptIfaces.begin(), scriptIfaces.end(),
                               [scriptId](const auto& iface) {
                                   return iface->data.id == scriptId;
                               });
        if (it != scriptIfaces.end())
        {
            return it->get();
        }
        return nullptr;
    }
    bool onFinish(boost::system::error_code ec, std::string scriptId)
    {
        if (!ec)
        {
            return removeFromActive(ec, scriptId);
        }
        return false;
    }
    bool removeFromActive(boost::system::error_code ec, std::string scriptId)
    {
        auto it = std::find_if(scriptIfaces.begin(), scriptIfaces.end(),
                               [scriptId](const auto& iface) {
                                   return iface->data.id == scriptId;
                               });
        if (it != scriptIfaces.end())
        {
            scriptIfaces.erase(it);
            return true;
        }
        return false;
    }
};
} // namespace scrrunner
