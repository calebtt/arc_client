#pragma once
#include <iostream>
#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/asio/ssl.hpp>
#include <nlohmann/json.hpp>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <array>
#include <mutex>
#include "StatConfiguration.h"
#include "StreamToActionTranslator.h"


namespace asio = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;
namespace ssl = boost::asio::ssl;
namespace http = boost::beast::http;
using namespace std::literals;


// Tracks "keydown" states for command keycodes
static const std::unordered_map<std::string, int32_t> commandLookup
{
	{"move_up", MouseMoveUp},
	{"move_down", MouseMoveDown},
	{"move_right", MouseMoveRight},
	{"move_left", MouseMoveLeft},
	{"move_up_left", MouseMoveUpLeft}, {"move_up_right", MouseMoveUpRight}, {"move_down_right", MouseMoveDownRight}, {"move_down_left", MouseMoveDownLeft},

	{"click_left", MouseLeftClick}, {"click_right", MouseRightClick}, {"click_middle", MouseMiddleClick},
	{"scroll_up", MouseScrollUp}, {"scroll_down", MouseScrollDown},
	{"drag_start", MouseDragStart}, {"drag_end", MouseDragEnd},
	//{"osk_toggle", 16},

	// Multimedia controls
	{"play_pause", MediaPlayPause},
	{"next_track", MediaNextTrack},
	{"previous_track", MediaPrevTrack},
	{"volume_up", VolumeUp},
	{"volume_down", VolumeDown},
	{"mute_toggle", VolumeMute},
	{"stop", MediaStop},
	{"open_prime_video", LaunchAmazonPrime},
	{"open_tubi", LaunchTubi},
	{"open_netflix", LaunchNetflix},
	{"press_escape", EscapeKey},
	{"toggle_mouse_sensitivity", SensitivityToggle},
	{"toggle_blue_light_filter", ToggleMonitorOverlay}
};

struct ClientCallbacks
{
	std::function<void()> OnConnect;
	std::function<void(const std::string&)> OnError;
	std::function<void(std::set<std::string>)> OnClientListChanged;
	std::function<void()> OnFailure;

	template<typename Fn, typename... Args>
	void CallOnNewThread(Fn&& fn, Args&&... args) const
	{
		std::thread([f = std::forward<Fn>(fn), ...capturedArgs = std::forward<Args>(args)]() mutable {
			f(std::forward<Args>(capturedArgs)...);
			}).detach();
	}

	void CallOnNewThread(const std::function<void()>& fn) const
	{
		std::thread([f = fn]() {
			f();
			}).detach();
	}

};

// Array to track 0 = up, 1 = down
static std::array<bool, 32> keyStateBuffer{};
static std::mutex keyStateMutex{};
static sds::Translator translator(GetAllMappings());

std::set<std::string> connectedClientUUIDs;
std::set<std::string> trustedClientUUIDs;

void HandleWebClientListUpdate(const nlohmann::json& json, const ClientCallbacks& callbacks) {
	connectedClientUUIDs.clear();
	if (json.contains("clients") && json["clients"].is_array()) {
		for (const auto& entry : json["clients"]) {
			if (entry.contains("client_id")) {
				connectedClientUUIDs.insert(entry["client_id"].get<std::string>());
			}
		}
	}

	if (callbacks.OnClientListChanged) {
		callbacks.OnClientListChanged(connectedClientUUIDs);
	}
}



void UpdateStateBuffer(const std::string state, const std::string command)
{
	std::lock_guard lock(keyStateMutex);

	const auto result = commandLookup.find(command);
	if (result != commandLookup.cend())
	{
		keyStateBuffer[result->second] = state == "keydown";
	}
}

void ReadMessage(auto& ws, beast::flat_buffer& buffer, std::atomic<bool>& stop_signal, const ClientCallbacks& callbacks)
{
	ws.async_read(buffer, [&](boost::system::error_code ec, std::size_t bytes_transferred) {
		if (ec == websocket::error::closed) {
			std::cout << "[Desktop Client] Disconnected normally.\n";
			stop_signal.store(true);
			return;
		}

		if (ec) {
			const std::string errMsg = "[ERROR] Desktop Client Read Error: "s + ec.message() + "\n"s;
			std::cerr << errMsg;
			if (callbacks.OnError)
				callbacks.OnError(errMsg);

			stop_signal.store(true);
			return;
		}

		const std::string payload = beast::buffers_to_string(buffer.data());
		buffer.consume(bytes_transferred);

		try {
			const auto json = nlohmann::json::parse(payload);

			bool handled = false;

			if (json.contains("type") && json["type"] == "web_client_list") {
				HandleWebClientListUpdate(json, callbacks);
				handled = true;
			}

			if (json.contains("command") && json.contains("state")) {
				const std::string command = json["command"];
				const std::string state = json["state"];

				if (json.contains("client_id")) {
					const std::string& uuid = json["client_id"];
					if (!trustedClientUUIDs.contains(uuid)) {
						if (callbacks.OnError) {
							callbacks.OnError("[Security] Ignoring command from untrusted client: "s + uuid + "\n"s);
						}
						handled = true;
					}
				}

				if (!handled) {
					std::cout << "[Desktop Client] Received Command: " << command
						<< " | State: " << state << "\n";

					UpdateStateBuffer(state, command);
				}
			}
		}
		catch (const nlohmann::json::parse_error& e) {
			const std::string errMsg = "[ERROR] JSON parse error: "s + e.what() + "\n"s;
			std::cerr << errMsg;
			if (callbacks.OnError)
				callbacks.OnError(errMsg);
		}

		// The next read
		ReadMessage(ws, buffer, stop_signal, callbacks);
		});
}

void WebSocketClient(
	const std::string& host,
	const std::string& port,
	const std::string& session_token,
	const std::string& client_type,
	std::atomic<bool>& should_stop,
	const ClientCallbacks& callbacks
)
{
	constexpr int max_retries = 5;
	constexpr int reconnect_delay_ms = 1000; // Delay between reconnects
	constexpr std::chrono::seconds ping_interval{ 50 };

	int retry_count = 0;

	while (max_retries < 0 || retry_count < max_retries) 
	{
		try {
			asio::io_context ioc;
			auto work_guard = asio::make_work_guard(ioc);

			ssl::context ctx(ssl::context::tlsv12_client);
			ctx.set_verify_mode(ssl::verify_none);  // Accept self-signed certs

			tcp::resolver resolver(ioc);
			websocket::stream<beast::ssl_stream<tcp::socket>> ws(ioc, ctx);
			ws.set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
			ws.set_option(websocket::stream_base::decorator(
				[](websocket::request_type& req) {
					req.set(http::field::user_agent, std::string("ARC Desktop Client"));
				}
			));

			//std::atomic<bool> stop_signal{ false };

			const auto results = resolver.resolve(host, port);
			asio::connect(ws.next_layer().next_layer(), results.begin(), results.end());
			ws.next_layer().handshake(ssl::stream_base::client);
			ws.handshake(host, "/ws/");

			nlohmann::json register_msg = {
				{"session_token", session_token},
				{"client_type", client_type}
			};
			ws.write(asio::buffer(register_msg.dump()));

			std::cout << "[" << client_type << " Client] Connected with session: " << session_token << "\n";
			if (callbacks.OnConnect)
			{
				callbacks.OnConnect();
			}

			beast::flat_buffer buffer;
			ReadMessage(ws, buffer, should_stop, callbacks);

			std::thread translator_thread([&]() {
				while (!should_stop.load()) {
					sds::SmallVector_t<int32_t> heldDownKeys;
					{
						std::lock_guard lock(keyStateMutex);
						for (size_t i = 0; i < keyStateBuffer.size(); ++i) {
							if (keyStateBuffer[i])
								heldDownKeys.push_back(static_cast<int32_t>(i));
						}
					}
					translator(heldDownKeys)();
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
				});

			std::thread asio_thread([&]() { ioc.run(); });

			auto last_ping_time = std::chrono::steady_clock::now();
			while (!should_stop.load()) {
				if (std::chrono::steady_clock::now() - last_ping_time > ping_interval) {
					boost::system::error_code ec;
					ws.ping({}, ec);
					if (ec) {
						std::cerr << "[WARN] WebSocket ping failed: " << ec.message() << "\n";
					}
					else {
						std::cout << "[KeepAlive] Sent ping\n";
					}
					last_ping_time = std::chrono::steady_clock::now();
				}

				std::this_thread::sleep_for(std::chrono::milliseconds(500));
			}
			should_stop.store(true);
			work_guard.reset();  // Allow io_context to stop
			ioc.stop();          // Actually cause .run() to exit
			asio_thread.join();
			translator_thread.join();

			auto cleanup_actions = translator.GetCleanupActions();
			for (auto& ca : cleanup_actions)
				ca();


			ws.close(websocket::close_code::normal);
			std::cout << "[" << client_type << " Client] Connection closed gracefully.\n";

			retry_count = 0; // Reset retry count on clean exit
			break; // Exit loop normally
		}
		catch (const std::exception& e) {
			if (should_stop.load())
			{
				return;
			}

			std::string errMsg = e.what();
			std::cerr << "[ERROR] " << client_type << " Client Error: " << errMsg << "\n";
			if (callbacks.OnError) {
				callbacks.OnError(errMsg);
			}

			++retry_count;
			std::cerr << "[INFO] Attempting to reconnect in " << reconnect_delay_ms << "ms...\n";
			std::this_thread::sleep_for(std::chrono::milliseconds(reconnect_delay_ms));
		}
	}
	if (callbacks.OnFailure)
	{
		callbacks.CallOnNewThread(callbacks.OnFailure);
	}
}
