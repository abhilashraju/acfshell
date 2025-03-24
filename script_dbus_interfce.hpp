#pragma once
#include "script_runner.hpp"
#include "sdbus_calls_runner.hpp"
#include <memory>
namespace scrrunner
{
    struct ScriptInterface
    {
        net::io_context &io_context;
        ScriptRunner &scriptRunner;
        std::shared_ptr<sdbusplus::asio::connection> conn;
        sdbusplus::asio::object_server dbusServer;
        std::shared_ptr<sdbusplus::asio::dbus_interface> iface;
        static constexpr std::string_view busName = "xyz.openbmc_project.acfshell";
        static constexpr std::string_view objPath = "/xyz/openbmc_project/acfshell";
        static constexpr std::string_view interface =
            "xyz.openbmc_project.TacfShell";
        ScriptInterface(net::io_context &ioc, ScriptRunner &runner) : io_context(ioc), scriptRunner(runner),
                                                                      conn(std::make_shared<sdbusplus::asio::connection>(io_context)),
                                                                      dbusServer(conn)
        {
            conn->request_name(busName.data());
            iface =
                dbusServer.add_interface(objPath.data(), interface.data());
            // test generic properties

            iface->register_property_rw<std::vector<std::string>>(
                "active", sdbusplus::vtable::property_::emits_change,
                [](const auto &newPropertyValue, auto &prop)
                {
                    prop = newPropertyValue;
                    return true;
                },
                [](const auto &prop)
                { return prop; });

            iface->register_method("start",
                                   [this](const std::string &script)
                                   {
                                       auto id = scriptRunner.run_script(script, std::bind_front(&ScriptInterface::removeFromActive, this));
                                       if (!id)
                                       {
                                           LOG_ERROR("Failed to start script");
                                           return std::string();
                                       }
                                       net::co_spawn(io_context, std::bind_front(&ScriptInterface::addToActive, this, *id), net::detached);
                                       return *id;
                                   });

            iface->initialize();
        }
        net::awaitable<void> addToActive(const std::string &scriptId)
        {
            auto [ec, active] = co_await getProperty<std::vector<std::string>>(*conn, busName.data(), objPath.data(), interface.data(), "active");
            if (!ec)
            {
                active.push_back(scriptId);
                std::tie(ec) = co_await setProperty(*conn, busName.data(), objPath.data(), interface.data(), "active", active);
                if (ec)
                {
                    LOG_ERROR("Failed to set active property");
                }
                co_return;
            }

            LOG_ERROR("Failed to get active property");
        }
        void removeFromActive(boost::system::error_code ec, std::string scriptId)
        {
            if (ec)
            {
                LOG_ERROR("Failed to start script");
                return;
            }
            auto remover = [this, scriptId]() -> net::awaitable<void>
            {
                auto [ec, active] = co_await getProperty<std::vector<std::string>>(*conn, busName.data(), objPath.data(), interface.data(), "active");
                if (ec)
                {
                    LOG_ERROR("Failed to get active property");
                    co_return;
                }
                active.erase(std::remove(active.begin(), active.end(), scriptId), active.end());
                std::tie(ec) = co_await setProperty(*conn, busName.data(), objPath.data(), interface.data(), "active", active);
                if (ec)
                {
                    LOG_ERROR("Failed to set active property");
                }
            };
            net::co_spawn(io_context, std::move(remover), net::detached);
        }
        net::awaitable<void> runScript(const std::string &script)
        {
            auto [ec, value] =
                co_await awaitable_dbus_method_call<std::string>(
                    *conn, busName.data(), objPath.data(), interface.data(), "start", script);

            if (ec)
            {
                LOG_ERROR("Error starting script: {}", ec.message());
            }
        }
    };
} // namespace scrrunner