#pragma once
#include "logger.hpp"

#include <openssl/evp.h>

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/use_awaitable.hpp>
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
namespace net = boost::asio;
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
    net::awaitable<void> execute(const std::string& filename,
                                 const std::string& hash, Callback callback)
    {
        std::vector<char> buf(4096);

        bp::async_pipe ap(io_context);

        boost::system::error_code ec;
        bp::child c("/usr/bin/bash", filename, bp::std_out > ap);
        if (ec)
        {
            LOG_ERROR("Failed to start child process: {}", ec.message());
            callback(ec, hash);
            co_return;
        }
        script_cache.emplace(hash,
                             ScriptEntry{std::ref(c), std::move(callback)});

        std::ofstream ofs(scriptOutputFileName(hash));
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
            ofs.write(buf.data(), size);
        }
        ofs.close();
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
    ScriptRunner(net::io_context& io_context) : io_context(io_context) {}
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
    struct ScriptEntry
    {
        std::reference_wrapper<bp::child> child;
        std::function<void(boost::system::error_code, std::string)> callback;
    };
    std::map<std::string, ScriptEntry> script_cache;
};
} // namespace scrrunner
