#pragma once
#include <boost/process.hpp>
#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/awaitable.hpp>
#include <string>
#include <iostream>
#include <vector>
#include <fstream>
#include <openssl/evp.h>
#include <map>
#include <functional>
#include <optional>
#include <filesystem>
#include "logger.hpp"
namespace net = boost::asio;
namespace bp = boost::process;
namespace scrrunner
{
    struct ScriptRunner
    {
        using Callback = std::function<void(boost::system::error_code, std::string)>;
        static std::optional<std::string> makeHash(const std::string &script)
        {
            // Create a SHA256 hash of the script string using EVP API
            unsigned char hash[EVP_MAX_MD_SIZE];
            unsigned int hash_len;
            EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
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
        net::awaitable<void> execute(const std::string &filename, const std::string &hash, Callback callback)
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
            script_cache.emplace(filename, std::ref(c));
            while (!ec)
            {
                auto size = co_await net::async_read(ap, net::buffer(buf), net::redirect_error(net::use_awaitable, ec));
                if (ec && ec != net::error::eof)
                {
                    LOG_ERROR("Error: {}", ec.message());
                    callback(ec, hash);
                    co_return;
                }
                std::cout.write(buf.data(), size);
            }
            script_cache.erase(filename);
            std::filesystem::remove(filename);
            callback(boost::system::error_code{}, hash);
        }
        bool run_script(const std::string &id, const std::string &script, Callback callback)
        {

            std::string filename = "/tmp/script_" + id + ".sh";

            // Write the script to a file
            std::ofstream script_file(filename);
            if (!script_file)
            {
                LOG_ERROR("Failed to create script file: {}", filename);
                return false;
            }
            script_file << script;
            script_file.close();

            net::co_spawn(io_context, [this, filename, id = id, callback = std::move(callback)]() mutable -> net::awaitable<void>
                          { co_await execute(filename, id, std::move(callback)); }, net::detached);
            return true;
        }
        ScriptRunner(net::io_context &io_context) : io_context(io_context) {}
        ~ScriptRunner()
        {
            for (auto &[filename, child] : script_cache)
            {
                child.get().terminate();
                std::filesystem::remove(filename);
            }
        }
        net::io_context &io_context;
        std::map<std::string, std::reference_wrapper<bp::child>> script_cache;
    };
} // namespace scrrunner