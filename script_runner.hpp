#pragma once
#include "logger.hpp"
#include "sdbus_calls_runner.hpp"

#include <openssl/evp.h>

#include <boost/process.hpp>

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>
static constexpr auto acfdirectory = "/tmp/acf";
namespace bp = boost::process;
namespace scrrunner
{
struct ScriptRunner
{
    using Callback =
        std::function<void(boost::system::error_code, std::string)>;
    static std::optional<std::string> makeHash(const std::string& script)
    {
        // Create a SHA256 hash of the script string using EVP API
        unsigned char hash[EVP_MAX_MD_SIZE];
        unsigned int hash_len;
        EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
        if (mdctx == nullptr)
        {
            LOG_ERROR("Failed to create EVP_MD_CTX");
            return std::nullopt;
        }
        if (EVP_DigestInit_ex(mdctx, EVP_sha256(), nullptr) != 1 ||
            EVP_DigestUpdate(mdctx, script.c_str(), script.size()) != 1 ||
            EVP_DigestFinal_ex(mdctx, hash, &hash_len) != 1)
        {
            LOG_ERROR("Failed to compute SHA256 hash");
            EVP_MD_CTX_free(mdctx);
            return std::nullopt;
        }
        EVP_MD_CTX_free(mdctx);

        // Convert the hash to a hexadecimal string
        std::string hash_str;
        for (unsigned int i = 0; i < hash_len; ++i)
        {
            char buf[3];
            snprintf(buf, sizeof(buf), "%02x", hash[i]);
            hash_str += buf;
        }
        // Restrict the hash length to 16 characters
        constexpr size_t maxHashLen = 16;
        if (hash_str.length() > maxHashLen)
        {
            hash_str.resize(maxHashLen);
        }
        return hash_str;
    }
    std::string scriptDir(std::string id)
    {
        std::string dir = std::format("{}/{}", acfdirectory, id);
        if (!std::filesystem::exists(dir))
        {
            std::filesystem::create_directories(dir);
        }
        return dir;
    }
    std::string scriptFileName(std::string id)
    {
        return std::format("{}/{}.sh", scriptDir(id), id);
    }
    std::string scriptOutputFileName(std::string id)
    {
        return std::format("{}/{}.out", scriptDir(id), id);
    }
    net::awaitable<boost::system::error_code> writeResult(bp::async_pipe& ap,
                                                          std::ofstream& os)
    {
        std::vector<char> buf(4096);
        boost::system::error_code ec{};
        while (!ec)
        {
            auto size = co_await net::async_read(
                ap, net::buffer(buf),
                net::redirect_error(net::use_awaitable, ec));
            if (ec && ec != net::error::eof)
            {
                LOG_INFO("Error: {}", ec.message());
                break;
            }
            os.write(buf.data(), size);
        }
        co_return (ec == net::error::eof ? boost::system::error_code{} : ec);
    }
    net::awaitable<void> execute(const std::string& filename,
                                 const std::string& hash, Callback callback)
    {
        bp::async_pipe ap(io_context);
        bp::async_pipe ep(io_context);

        boost::system::error_code ec;
        bp::child c("/usr/bin/bash", filename, bp::std_out > ap,
                    bp::std_err > ep);
        // if (ec)
        // {
        //     LOG_ERROR("Failed to start child process: {}", ec.message());
        //     callback(ec, hash);
        //     co_return;
        // }
        script_cache.emplace(hash,
                             ScriptEntry{std::ref(c), std::move(callback)});

        std::ofstream ofs(scriptOutputFileName(hash));
        ec = co_await writeResult(ap, ofs);
        if (ec)
        {
            LOG_ERROR("{}", ec.message());
            co_return;
        }
        ec = co_await writeResult(ep, ofs);
        if (ec)
        {
            LOG_ERROR("{}", ec.message());
            co_return;
        }
        ofs.close();
        uint64_t timeout = 30;
        using paramtype = std::vector<
            std::pair<std::string, std::variant<std::string, uint64_t>>>;
        sdbusplus::message_t msg;
        std::tie(ec, msg) =
            co_await awaitable_dbus_method_call<sdbusplus::message_t>(
                *conn, "xyz.openbmc_project.Dump.Manager",
                "/xyz/openbmc_project/dump/bmc",
                "xyz.openbmc_project.Dump.Create", "CreateDump", paramtype());

        if (ec)
        {
            LOG_ERROR("Error creating dump: {}", ec.message());
        }
        invokeCallback(boost::system::error_code{}, hash);
        remove(hash);
    }
    void invokeCallback(boost::system::error_code ec, const std::string& id)
    {
        auto it = script_cache.find(id);
        if (it == script_cache.end())
        {
            return;
        }
        it->second.callback(ec, id);
    }
    void remove(const std::string& id)
    {
        script_cache.erase(id);
    }
    bool run_script(const std::string& id, const std::string& script,
                    Callback callback)
    {
        auto filename = scriptFileName(id);
        // Write the script to a file
        std::ofstream script_file(filename);
        if (!script_file)
        {
            LOG_ERROR("Failed to create script file: {}", filename);
            return false;
        }
        script_file << script;
        script_file.close();

        net::co_spawn(
            io_context,
            [this, filename, id = id,
             callback = std::move(callback)]() mutable -> net::awaitable<void> {
                co_await execute(filename, id, std::move(callback));
            },
            net::detached);
        return true;
    }
    bool cancel_script(const std::string& id)
    {
        auto it = script_cache.find(id);
        if (it == script_cache.end())
        {
            return false;
        }
        it->second.child.get().terminate();
        it->second.callback(boost::system::error_code{}, id);
        remove(id);
        return true;
    }
    ScriptRunner(net::io_context& io_context,
                 std::shared_ptr<sdbusplus::asio::connection> conn) :
        io_context(io_context), conn(conn)
    {}
    ~ScriptRunner()
    {
        while (!script_cache.empty())
        {
            auto p = *script_cache.begin();
            p.second.child.get().terminate();
            remove(p.first);
        }
    }
    net::io_context& io_context;
    std::shared_ptr<sdbusplus::asio::connection> conn;
    struct ScriptEntry
    {
        std::reference_wrapper<bp::child> child;
        std::function<void(boost::system::error_code, std::string)> callback;
    };
    std::map<std::string, ScriptEntry> script_cache;
};
} // namespace scrrunner
