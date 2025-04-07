#pragma once
#include <iostream>
#include <fstream>
#include <string>
#include <nlohmann/json.hpp>
#include "ClientFunctionality.h"

using json = nlohmann::json;

const std::string CONFIG_FILE = "session_config.json";

// Function to read session token from JSON config file
std::string ReadSessionToken() {
    std::ifstream file(CONFIG_FILE);
    if (!file) {
        std::cerr << "Error: Could not open " << CONFIG_FILE << "\n";
        return "";
    }

    json config;
    try {
        file >> config;
        if (config.contains("session_token")) {
            return config["session_token"].get<std::string>();
        }
        else {
            std::cerr << "Error: 'session_token' missing in " << CONFIG_FILE << "\n";
            return "";
        }
    }
    catch (const std::exception& e) {
        std::cerr << "JSON parsing error: " << e.what() << "\n";
        return "";
    }
}

void SaveSessionToken(const std::string& newSessionToken)
{
    nlohmann::json j;
    j["session_token"] = newSessionToken;
    std::ofstream ofs(CONFIG_FILE);
    ofs << j.dump(4);
}

void StartArcClient(
    const std::string& portString, 
    const std::string& serverAddress, 
    const std::string& sessionToken, 
    std::atomic<bool>& should_stop,
    const ClientCallbacks& callbacks)
{
    if (sessionToken.empty()) {
        std::cerr << "Error: No valid session token found. Exiting.\n";
        return;
    }

    std::cout << "[Session] Connecting to " << serverAddress << ":" << portString << '\n';
    WebSocketClient(serverAddress, portString, sessionToken, "desktop", should_stop, callbacks);
    return;
}

// Probably global access to various client functions.
struct WebSocketClientGlobal
{
    const std::string PortString{ "443" };
    std::string ServerAddress{ "arcserver.cloud" };

    // The thread upon which all Asio, Beast, SSL, Websockets work is initiated.
    std::thread ClientThread;
    std::string CurrentSessionToken;
    std::atomic<bool> IsStopRequested{};

    ClientCallbacks Callbacks;

    void Init(std::string sessionToken)
    {
        CurrentSessionToken = std::move(sessionToken);
        IsStopRequested.store(false);
        ClientThread = std::thread([&]() { StartArcClient(PortString, ServerAddress, CurrentSessionToken, IsStopRequested, Callbacks); });
    }

    ~WebSocketClientGlobal() noexcept
    {
        StopClientThread();
    }

    void UpdateSessionToken(std::string sessionTokenUpdate)
    {
        StopClientThread();

        // Reset stop request, start new thread with new session token.
        CurrentSessionToken = std::move(sessionTokenUpdate);
        IsStopRequested.store(false);
        ClientThread = std::thread([&]() { StartArcClient(PortString, ServerAddress, CurrentSessionToken, IsStopRequested, Callbacks); });
    }

    void StopClientThread()
    {
        // If there is a joinable running thread...
        if (ClientThread.joinable())
        {
            // Request stop, join and wait.
            IsStopRequested.store(true);
            ClientThread.join();
        }
    }
};