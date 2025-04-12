#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef FI_IMPLEMENTATION
#define FI_IMPLEMENTATION
#endif
#include <Windows.h>
#include "StreamToActionTranslator.h"
#include "Win32Overlay.h"
#include <atomic>
#include <fakeinput/fakeinput.hpp>

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

struct MappingsBuilder
{
	static FakeInput::Mouse MouseObj;
	static FakeInput::Keyboard KeyboardObj;


	static void SendMouseMove(const int x, const int y) noexcept
	{
		MouseObj.move(x, y);
	}

	// Simulate Mouse Clicks
	static void SendMouseClick(FakeInput::MouseButton button) noexcept
	{
		// TODO this might need a delay.
		MouseObj.pressButton(button);
		MouseObj.releaseButton(button); 
	}

	static void SendMultimediaKey(FakeInput::KeyType vk, const bool doDown) noexcept
	{
		if(doDown)
			KeyboardObj.pressKey(FakeInput::CreateKeyFromKeyType(vk));
		else
			KeyboardObj.releaseKey(FakeInput::CreateKeyFromKeyType(vk));
	}
};
MappingsBuilder inputSimulator;


// get global sensitivity instance
SensitivityToggler& GetSensitivityTogglerInstance() 
{
	static SensitivityToggler instance;
	return instance;
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
			.OnDown = []() { inputSimulator.SendMouseClick(FakeInput::Mouse_Left); },
			.ButtonVirtualKeycode = MouseLeftClick,
			.RepeatingKeyBehavior = RepeatType::None
		},

		// Right Click
		MappingContainer
		{
			.OnDown = []() { inputSimulator.SendMouseClick(FakeInput::Mouse_Right); },
			.ButtonVirtualKeycode = MouseRightClick,
			.RepeatingKeyBehavior = RepeatType::None
		},

		// Middle Click
		MappingContainer
		{
			.OnDown = []() { inputSimulator.SendMouseClick(FakeInput::Mouse_Middle); },
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
	using FakeInput::Mouse;

	vector<MappingContainer> mapBuffer =
	{
		// Move Up
		MappingContainer
		{
			.OnDown = [&]() { Mouse::move(0, -GetSensitivityTogglerInstance().Get()); },
			.OnRepeat = [&]() { Mouse::move(0, -GetSensitivityTogglerInstance().Get()); },
			.ButtonVirtualKeycode = MouseMoveUp,
			.RepeatingKeyBehavior = RepeatType::Infinite,
			.DelayBeforeFirstRepeat = FirstDelay,
			.BetweenRepeatDelay = RepeatDelay
		},

		// Move Down
		MappingContainer
		{
			.OnDown = [&]() { Mouse::move(0, GetSensitivityTogglerInstance().Get()); },
			.OnRepeat = [&]() { Mouse::move(0, GetSensitivityTogglerInstance().Get()); },
			.ButtonVirtualKeycode = MouseMoveDown,
			.RepeatingKeyBehavior = RepeatType::Infinite,
			.DelayBeforeFirstRepeat = FirstDelay,
			.BetweenRepeatDelay = RepeatDelay
		},

		// Move Right
		MappingContainer
		{
			.OnDown = [&]() { Mouse::move(GetSensitivityTogglerInstance().Get(), 0); },
			.OnRepeat = [&]() { Mouse::move(GetSensitivityTogglerInstance().Get(), 0); },
			.ButtonVirtualKeycode = MouseMoveRight,
			.RepeatingKeyBehavior = RepeatType::Infinite,
			.DelayBeforeFirstRepeat = FirstDelay,
			.BetweenRepeatDelay = RepeatDelay
		},

		// Move Left
		MappingContainer
		{
			.OnDown = [&]() { Mouse::move(-GetSensitivityTogglerInstance().Get(), 0); },
			.OnRepeat = [&]() { Mouse::move(-GetSensitivityTogglerInstance().Get(), 0); },
			.ButtonVirtualKeycode = MouseMoveLeft,
			.RepeatingKeyBehavior = RepeatType::Infinite,
			.DelayBeforeFirstRepeat = FirstDelay,
			.BetweenRepeatDelay = RepeatDelay
		},

		// Move Up-Left
		MappingContainer
		{
			.OnDown = [&]() {
				auto s = GetSensitivityTogglerInstance().Get();
				Mouse::move(-s, -s);
			},
			.OnRepeat = [&]() {
				auto s = GetSensitivityTogglerInstance().Get();
				Mouse::move(-s, -s);
			},
			.ButtonVirtualKeycode = MouseMoveUpLeft,
			.RepeatingKeyBehavior = RepeatType::Infinite,
			.DelayBeforeFirstRepeat = FirstDelay,
			.BetweenRepeatDelay = RepeatDelay
		},

		// Move Up-Right
		MappingContainer
		{
			.OnDown = [&]() {
				auto s = GetSensitivityTogglerInstance().Get();
				Mouse::move(s, -s);
			},
			.OnRepeat = [&]() {
				auto s = GetSensitivityTogglerInstance().Get();
				Mouse::move(s, -s);
			},
			.ButtonVirtualKeycode = MouseMoveUpRight,
			.RepeatingKeyBehavior = RepeatType::Infinite,
			.DelayBeforeFirstRepeat = FirstDelay,
			.BetweenRepeatDelay = RepeatDelay
		},

		// Move Down-Right
		MappingContainer
		{
			.OnDown = [&]() {
				auto s = GetSensitivityTogglerInstance().Get();
				Mouse::move(s, s);
			},
			.OnRepeat = [&]() {
				auto s = GetSensitivityTogglerInstance().Get();
				Mouse::move(s, s);
			},
			.ButtonVirtualKeycode = MouseMoveDownRight,
			.RepeatingKeyBehavior = RepeatType::Infinite,
			.DelayBeforeFirstRepeat = FirstDelay,
			.BetweenRepeatDelay = RepeatDelay
		},

		// Move Down-Left
		MappingContainer
		{
			.OnDown = [&]() {
				auto s = GetSensitivityTogglerInstance().Get();
				Mouse::move(-s, s);
			},
			.OnRepeat = [&]() {
				auto s = GetSensitivityTogglerInstance().Get();
				Mouse::move(-s, s);
			},
			.ButtonVirtualKeycode = MouseMoveDownLeft,
			.RepeatingKeyBehavior = RepeatType::Infinite,
			.DelayBeforeFirstRepeat = FirstDelay,
			.BetweenRepeatDelay = RepeatDelay
		}
	};

	return mapBuffer;
}

inline auto GetDriverKeyboardMappings(HWND uiHwnd)
{
	using std::vector;
	using sds::MappingContainer;
	using sds::RepeatType;

	vector<MappingContainer> mapBuffer =
	{
		// Multimedia Controls
		MappingContainer
		{
			.OnDown = []() { inputSimulator.SendMultimediaKey(FakeInput::Key_MediaPlayPause, true); },
			.OnUp = []() { inputSimulator.SendMultimediaKey(FakeInput::Key_MediaPlayPause, false); },
			.ButtonVirtualKeycode = MediaPlayPause,
			.RepeatingKeyBehavior = RepeatType::None
		},
		MappingContainer
		{
			.OnDown = []() { inputSimulator.SendMultimediaKey(FakeInput::Key_MediaNext, true); },
			.OnUp = []() { inputSimulator.SendMultimediaKey(FakeInput::Key_MediaNext, false); },
			.ButtonVirtualKeycode = MediaNextTrack,
			.RepeatingKeyBehavior = RepeatType::None
		},
		MappingContainer
		{
			.OnDown = []() { inputSimulator.SendMultimediaKey(FakeInput::Key_MediaPrev, true); },
			.OnUp = []() { inputSimulator.SendMultimediaKey(FakeInput::Key_MediaPrev, false); },
			.ButtonVirtualKeycode = MediaPrevTrack,
			.RepeatingKeyBehavior = RepeatType::None
		},
		MappingContainer
		{
			.OnDown = []() { inputSimulator.SendMultimediaKey(FakeInput::Key_VolumeUp, true); },
			.OnUp = []() { inputSimulator.SendMultimediaKey(FakeInput::Key_VolumeUp, false); },
			.ButtonVirtualKeycode = VolumeUp,
			.RepeatingKeyBehavior = RepeatType::None
		},
		MappingContainer
		{
			.OnDown = []() { inputSimulator.SendMultimediaKey(FakeInput::Key_VolumeDown, true); },
			.OnUp = []() { inputSimulator.SendMultimediaKey(FakeInput::Key_VolumeDown, false); },
			.ButtonVirtualKeycode = VolumeDown,
			.RepeatingKeyBehavior = RepeatType::None
		},
		MappingContainer
		{
			.OnDown = []() { inputSimulator.SendMultimediaKey(FakeInput::Key_VolumeMute, true); },
			.OnUp = []() { inputSimulator.SendMultimediaKey(FakeInput::Key_VolumeMute, false); },
			.ButtonVirtualKeycode = VolumeMute,
			.RepeatingKeyBehavior = RepeatType::None
		},
		MappingContainer
		{
			.OnDown = []() { inputSimulator.SendMultimediaKey(FakeInput::Key_MediaStop, true); },
			.OnUp = []() { inputSimulator.SendMultimediaKey(FakeInput::Key_MediaStop, false); },
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
			.OnDown = []() { inputSimulator.SendMultimediaKey(FakeInput::Key_Escape, true); },
			.OnUp = []() { inputSimulator.SendMultimediaKey(FakeInput::Key_Escape, false); },
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
			.OnDown = [uiHwnd]() { PostMessage(uiHwnd, WM_COMMAND, 1002, 0); },
			.ButtonVirtualKeycode = ToggleMonitorOverlay,
			.RepeatingKeyBehavior = RepeatType::None
		}
	};

	return mapBuffer;
}

inline auto GetAllMappings(HWND uiHwnd)
{
	auto clickMappings = GetClickMappings();
	auto keyboardMappings = GetDriverKeyboardMappings(uiHwnd);
	auto allMappings = GetDriverMouseMappings();
	allMappings.insert(allMappings.end(), clickMappings.begin(), clickMappings.end());
	allMappings.insert(allMappings.end(), keyboardMappings.begin(), keyboardMappings.end());

	return allMappings;
}