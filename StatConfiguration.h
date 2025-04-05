#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include "StreamToActionTranslator.h"
#include "Win32Overlay.h"
#include <atomic>

/**
* \brief Key Repeat Delay is the time delay a button has in-between activations.
*/
static constexpr sds::Nanos_t KeyRepeatDelay{ std::chrono::microseconds{100'000} };

// Mouse Movement
static constexpr int32_t MouseMoveUp{ 1 };
static constexpr int32_t MouseMoveDown{ 2 };
static constexpr int32_t MouseMoveRight{ 3 };
static constexpr int32_t MouseMoveLeft{ 4 };

static constexpr int32_t MouseMoveUpLeft{ 5 };
static constexpr int32_t MouseMoveUpRight{ 6 };
static constexpr int32_t MouseMoveDownRight{ 7 };
static constexpr int32_t MouseMoveDownLeft{ 8 };

// Mouse Clicks
static constexpr int32_t MouseLeftClick{ 9 };
static constexpr int32_t MouseRightClick{ 10 };
static constexpr int32_t MouseMiddleClick{ 11 };

// Mouse Scroll
static constexpr int32_t MouseScrollUp{ 12 };
static constexpr int32_t MouseScrollDown{ 13 };

// Additional Mouse Functions
static constexpr int32_t MouseDragStart{ 14 };
static constexpr int32_t MouseDragEnd{ 15 };

// On-Screen Keyboard
static constexpr int32_t ToggleOnScreenKeyboard{ 16 };

// Multimedia Controls
static constexpr int32_t MediaPlayPause{ 17 };
static constexpr int32_t MediaNextTrack{ 18 };
static constexpr int32_t MediaPrevTrack{ 19 };
static constexpr int32_t VolumeUp{ 20 };
static constexpr int32_t VolumeDown{ 21 };
static constexpr int32_t VolumeMute{ 22 };
static constexpr int32_t MediaStop{ 23 };

static constexpr int32_t LaunchAmazonPrime{ 24 };
static constexpr int32_t LaunchTubi{ 25 };
static constexpr int32_t LaunchNetflix{ 26 };
static constexpr int32_t EscapeKey{ 27 };
static constexpr int32_t SensitivityToggle{ 28 };
static constexpr int32_t ToggleMonitorOverlay{ 29 };

struct SensitivityToggler
{
	std::atomic<int> CurrentSensitivity{ 1 };

	int Get() { return CurrentSensitivity.load(); }
	auto Toggle() {
		if (CurrentSensitivity.load() == 1)
			CurrentSensitivity.store(2);
		else
			CurrentSensitivity.store(1);
	}
};

// get global sensitivity instance
SensitivityToggler& GetSensitivityTogglerInstance() 
{
	static SensitivityToggler instance;
	return instance;
}

inline auto CallSendInput(INPUT* inp, std::uint32_t numSent) noexcept -> UINT
{
	return SendInput(static_cast<UINT>(numSent), inp, sizeof(INPUT));
}

inline void SendMouseMove(const int x, const int y) noexcept
{
	INPUT m_mouseMoveInput{};
	m_mouseMoveInput.type = INPUT_MOUSE;
	m_mouseMoveInput.mi.dwFlags = MOUSEEVENTF_MOVE;

	using dx_t = decltype(m_mouseMoveInput.mi.dx);
	using dy_t = decltype(m_mouseMoveInput.mi.dy);
	m_mouseMoveInput.mi.dx = static_cast<dx_t>(x);
	m_mouseMoveInput.mi.dy = -static_cast<dy_t>(y);
	m_mouseMoveInput.mi.dwExtraInfo = GetMessageExtraInfo();
	//Finally, send the input
	CallSendInput(&m_mouseMoveInput, 1);
}

// Simulate Mouse Clicks
inline void SendMouseClick(int button)
{
	INPUT input = {};
	input.type = INPUT_MOUSE;

	if (button == MouseLeftClick) {
		input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
		SendInput(1, &input, sizeof(INPUT));
		input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
		SendInput(1, &input, sizeof(INPUT));
	}
	else if (button == MouseRightClick) {
		input.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
		SendInput(1, &input, sizeof(INPUT));
		input.mi.dwFlags = MOUSEEVENTF_RIGHTUP;
		SendInput(1, &input, sizeof(INPUT));
	}
	else if (button == MouseMiddleClick) {
		input.mi.dwFlags = MOUSEEVENTF_MIDDLEDOWN;
		SendInput(1, &input, sizeof(INPUT));
		input.mi.dwFlags = MOUSEEVENTF_MIDDLEUP;
		SendInput(1, &input, sizeof(INPUT));
	}
}

// Toggle On-Screen Keyboard
inline void ToggleOnScreenKeyboardFn()
{
	static bool keyboardOpen = false;
	if (keyboardOpen) {
		system("taskkill /IM osk.exe /F"); // Close OSK
	}
	else {
		system("osk"); // Open OSK
	}
	keyboardOpen = !keyboardOpen;
}

inline void SendMultimediaKey(const WORD vk, const bool doDown) noexcept
{
	INPUT keyInput{};
	keyInput.type = INPUT_KEYBOARD;
	keyInput.ki.wVk = vk;
	keyInput.ki.dwExtraInfo = GetMessageExtraInfo();
	if (!doDown)
	{
		keyInput.ki.dwFlags = KEYEVENTF_KEYUP;
	}
	//Finally, send the input
	CallSendInput(&keyInput, 1);
}

inline auto GetClickMappings()
{
	using std::vector, sds::MappingContainer, std::cout;
	using namespace std::chrono_literals;
	using namespace sds;

	SmallVector_t<MappingContainer> clickMappings =
	{
		// Left Click
		MappingContainer
		{
			.OnDown = []() { SendMouseClick(MouseLeftClick); },
			.ButtonVirtualKeycode = MouseLeftClick,
			.RepeatingKeyBehavior = RepeatType::None
		},

		// Right Click
		MappingContainer
		{
			.OnDown = []() { SendMouseClick(MouseRightClick); },
			.ButtonVirtualKeycode = MouseRightClick,
			.RepeatingKeyBehavior = RepeatType::None
		},

		// Middle Click
		MappingContainer
		{
			.OnDown = []() { SendMouseClick(MouseMiddleClick); },
			.ButtonVirtualKeycode = MouseMiddleClick,
			.RepeatingKeyBehavior = RepeatType::None
		},

		//// Toggle On-Screen Keyboard
		//MappingContainer
		//{
		//	.OnDown = []() { ToggleOnScreenKeyboardFn(); },
		//	.ButtonVirtualKeycode = ToggleOnScreenKeyboard,
		//	.RepeatingKeyBehavior = RepeatType::None
		//}
	};
	return clickMappings;
}

inline auto GetDriverMouseMappings()
{
	using std::vector, std::cout;
	using namespace std::chrono_literals;
	using namespace sds;

	constexpr auto FirstDelay = std::chrono::nanoseconds(0);   // No initial delay
	constexpr auto RepeatDelay = std::chrono::microseconds(1200);  // 1.2ms repeat delay

	vector<MappingContainer> mapBuffer =
	{
		// Move Up
		MappingContainer
		{
			.OnDown = [&]() { SendMouseMove(0, GetSensitivityTogglerInstance().Get()); },
			.OnRepeat = [&]() { SendMouseMove(0, GetSensitivityTogglerInstance().Get()); },
			.ButtonVirtualKeycode = MouseMoveUp,
			.RepeatingKeyBehavior = RepeatType::Infinite,
			.DelayBeforeFirstRepeat = FirstDelay,
			.BetweenRepeatDelay = RepeatDelay
		},

		// Move Down
		MappingContainer
		{
			.OnDown = [&]() { SendMouseMove(0, -GetSensitivityTogglerInstance().Get()); },
			.OnRepeat = [&]() { SendMouseMove(0, -GetSensitivityTogglerInstance().Get()); },
			.ButtonVirtualKeycode = MouseMoveDown,
			.RepeatingKeyBehavior = RepeatType::Infinite,
			.DelayBeforeFirstRepeat = FirstDelay,
			.BetweenRepeatDelay = RepeatDelay
		},

		// Move Right
		MappingContainer
		{
			.OnDown = [&]() { SendMouseMove(GetSensitivityTogglerInstance().Get(), 0); },
			.OnRepeat = [&]() { SendMouseMove(GetSensitivityTogglerInstance().Get(), 0); },
			.ButtonVirtualKeycode = MouseMoveRight,
			.RepeatingKeyBehavior = RepeatType::Infinite,
			.DelayBeforeFirstRepeat = FirstDelay,
			.BetweenRepeatDelay = RepeatDelay
		},

		// Move Left
		MappingContainer
		{
			.OnDown = [&]() { SendMouseMove(-GetSensitivityTogglerInstance().Get(), 0); },
			.OnRepeat = [&]() { SendMouseMove(-GetSensitivityTogglerInstance().Get(), 0); },
			.ButtonVirtualKeycode = MouseMoveLeft,
			.RepeatingKeyBehavior = RepeatType::Infinite,
			.DelayBeforeFirstRepeat = FirstDelay,
			.BetweenRepeatDelay = RepeatDelay
		},

		// Move Up-Left
		MappingContainer
		{
			.OnDown = [&]() { SendMouseMove(-GetSensitivityTogglerInstance().Get(), GetSensitivityTogglerInstance().Get()); },
			.OnRepeat = [&]() { SendMouseMove(-GetSensitivityTogglerInstance().Get(), GetSensitivityTogglerInstance().Get()); },
			.ButtonVirtualKeycode = MouseMoveUpLeft,
			.RepeatingKeyBehavior = RepeatType::Infinite,
			.DelayBeforeFirstRepeat = FirstDelay,
			.BetweenRepeatDelay = RepeatDelay
		},

		// Move Up-Right
		MappingContainer
		{
			.OnDown = [&]() { SendMouseMove(GetSensitivityTogglerInstance().Get(), GetSensitivityTogglerInstance().Get()); },
			.OnRepeat = [&]() { SendMouseMove(GetSensitivityTogglerInstance().Get(), GetSensitivityTogglerInstance().Get()); },
			.ButtonVirtualKeycode = MouseMoveUpRight,
			.RepeatingKeyBehavior = RepeatType::Infinite,
			.DelayBeforeFirstRepeat = FirstDelay,
			.BetweenRepeatDelay = RepeatDelay
		},

		// Move Down-Right
		MappingContainer
		{
			.OnDown = [&]() { SendMouseMove(GetSensitivityTogglerInstance().Get(), -GetSensitivityTogglerInstance().Get()); },
			.OnRepeat = [&]() { SendMouseMove(GetSensitivityTogglerInstance().Get(), -GetSensitivityTogglerInstance().Get()); },
			.ButtonVirtualKeycode = MouseMoveDownRight,
			.RepeatingKeyBehavior = RepeatType::Infinite,
			.DelayBeforeFirstRepeat = FirstDelay,
			.BetweenRepeatDelay = RepeatDelay
		},

		// Move Down-Left
		MappingContainer
		{
			.OnDown = [&]() { SendMouseMove(-GetSensitivityTogglerInstance().Get(), -GetSensitivityTogglerInstance().Get()); },
			.OnRepeat = [&]() { SendMouseMove(-GetSensitivityTogglerInstance().Get(), -GetSensitivityTogglerInstance().Get()); },
			.ButtonVirtualKeycode = MouseMoveDownLeft,
			.RepeatingKeyBehavior = RepeatType::Infinite,
			.DelayBeforeFirstRepeat = FirstDelay,
			.BetweenRepeatDelay = RepeatDelay
		}
	};

	return mapBuffer;
}

inline auto GetDriverKeyboardMappings()
{
	using std::vector;
	using sds::MappingContainer;
	using sds::RepeatType;

	vector<MappingContainer> mapBuffer =
	{
		// Multimedia Controls
		MappingContainer
		{
			.OnDown = []() { SendMultimediaKey(VK_MEDIA_PLAY_PAUSE, true); },
			.OnUp = []() { SendMultimediaKey(VK_MEDIA_PLAY_PAUSE, false); },
			.ButtonVirtualKeycode = MediaPlayPause,
			.RepeatingKeyBehavior = RepeatType::None
		},
		MappingContainer
		{
			.OnDown = []() { SendMultimediaKey(VK_MEDIA_NEXT_TRACK, true); },
			.OnUp = []() { SendMultimediaKey(VK_MEDIA_NEXT_TRACK, false); },
			.ButtonVirtualKeycode = MediaNextTrack,
			.RepeatingKeyBehavior = RepeatType::None
		},
		MappingContainer
		{
			.OnDown = []() { SendMultimediaKey(VK_MEDIA_PREV_TRACK, true); },
			.OnUp = []() { SendMultimediaKey(VK_MEDIA_PREV_TRACK, false); },
			.ButtonVirtualKeycode = MediaPrevTrack,
			.RepeatingKeyBehavior = RepeatType::None
		},
		MappingContainer
		{
			.OnDown = []() { SendMultimediaKey(VK_VOLUME_UP, true); },
			.OnUp = []() { SendMultimediaKey(VK_VOLUME_UP, false); },
			.ButtonVirtualKeycode = VolumeUp,
			.RepeatingKeyBehavior = RepeatType::None
		},
		MappingContainer
		{
			.OnDown = []() { SendMultimediaKey(VK_VOLUME_DOWN, true); },
			.OnUp = []() { SendMultimediaKey(VK_VOLUME_DOWN, false); },
			.ButtonVirtualKeycode = VolumeDown,
			.RepeatingKeyBehavior = RepeatType::None
		},
		MappingContainer
		{
			.OnDown = []() { SendMultimediaKey(VK_VOLUME_MUTE, true); },
			.OnUp = []() { SendMultimediaKey(VK_VOLUME_MUTE, false); },
			.ButtonVirtualKeycode = VolumeMute,
			.RepeatingKeyBehavior = RepeatType::None
		},
		MappingContainer
		{
			.OnDown = []() { SendMultimediaKey(VK_MEDIA_STOP, true); },
			.OnUp = []() { SendMultimediaKey(VK_MEDIA_STOP, false); },
			.ButtonVirtualKeycode = MediaStop,
			.RepeatingKeyBehavior = RepeatType::None
		},
		// Launch Prime Video
		MappingContainer{
			.OnDown = []() { system("start https://www.amazon.com/gp/video/storefront"); },
			.ButtonVirtualKeycode = LaunchAmazonPrime,
			.RepeatingKeyBehavior = RepeatType::None
		},

		// Launch Tubi
		MappingContainer{
			.OnDown = []() { system("start https://tubitv.com"); },
			.ButtonVirtualKeycode = LaunchTubi,
			.RepeatingKeyBehavior = RepeatType::None
		},

		// Launch Netflix
		MappingContainer{
			.OnDown = []() { system("start https://www.netflix.com"); },
			.ButtonVirtualKeycode = LaunchNetflix,
			.RepeatingKeyBehavior = RepeatType::None
		},
		MappingContainer
		{
			.OnDown = []() { SendMultimediaKey(VK_ESCAPE, true); },
			.OnUp = []() { SendMultimediaKey(VK_ESCAPE, false); },
			.ButtonVirtualKeycode = EscapeKey,
			.RepeatingKeyBehavior = RepeatType::None
		},
		MappingContainer
		{
			.OnDown = [&]() { GetSensitivityTogglerInstance().Toggle(); },
			.ButtonVirtualKeycode = SensitivityToggle,
			.RepeatingKeyBehavior = RepeatType::None
		},
		MappingContainer
		{
			.OnDown = []() { ToggleMultiMonitorOverlay(); },
			.ButtonVirtualKeycode = ToggleMonitorOverlay,
			.RepeatingKeyBehavior = RepeatType::None
		}
	};

	return mapBuffer;
}

inline auto GetAllMappings()
{
	auto clickMappings = GetClickMappings();
	auto keyboardMappings = GetDriverKeyboardMappings();
	auto allMappings = GetDriverMouseMappings();
	allMappings.insert(allMappings.end(), clickMappings.begin(), clickMappings.end());
	allMappings.insert(allMappings.end(), keyboardMappings.begin(), keyboardMappings.end());

	return allMappings;
}