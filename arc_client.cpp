#include <iostream>
#include <fstream>
#include <string>
#include <nlohmann/json.hpp>
#include "ClientFunctionality.h"

using json = nlohmann::json;

const std::string CONFIG_FILE = "session_config.json";

// Function to read session token from JSON config file
std::string get_session_token() {
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

inline void EnterForExit()
{
    std::string exitString;
    std::cerr << "[Enter] to exit...\n";
    std::getline(std::cin, exitString);
}

void PrintIntroBanner()
{
    std::cout << R"(

        _____________________
       |                     |
       |  .----.   .----.    |
       |  | O  |   | O  |    |
       |  '----'   '----'    |
       |    .----. .----.    |
       |    | O  | | O  |    |
       |    '----' '----'    |
       |    .----. .----.    |
       |    | O  | | O  |    |
       |    '----' '----'    |
       |      .------.       |
       |      | MENU |       |
       |      '------'       |
       |   .--.      .--.    |
       |   |<<|      |>>|    |
       |   '--'      '--'    |
       |_____________________|

         A . p p   R . e m o t e   C . o n t r o l

)" << std::endl;

}

bool IsArgTypeLocal(const std::string& arg)
{
    const std::string typeLocalArg = "type=local";
    return arg == typeLocalArg;
}

unsigned short GetArgPort(const std::string& arg)
{
    return std::stoi(arg);
}

// Launch with "type=local" for local debugging.
int main(int argc, char* argv[])
{
    const bool hasArg = argc > 1;
    const bool isTestSession = hasArg && IsArgTypeLocal(argv[1]);
    const std::string session_token = get_session_token();
    const std::string serverAddress = isTestSession ? "localhost" : "slowcasting.com";

    PrintIntroBanner();

    if (isTestSession)
    {
        std::cout << "[Test Session] connecting to " << serverAddress << '\n';
    }

    if (session_token.empty()) {
        std::cerr << "Error: No valid session token found. Exiting.\n";
        EnterForExit();
        return 1;
    }
    // wss://slowcasting.com:8443
    WebSocketClient(serverAddress, "443", session_token, "desktop");
    EnterForExit();
    return 0;
}
