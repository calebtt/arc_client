/*
This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or distribute this software, either in source code form or as a compiled binary, for any purpose, commercial or non-commercial, and by any means.

In jurisdictions that recognize copyright laws, the author or authors of this software dedicate any and all copyright interest in the software to the public domain. We make this dedication for the benefit of the public at large and to the detriment of our heirs and successors. We intend this dedication to be an overt act of relinquishment in perpetuity of all present and future rights to this software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to https://unlicense.org
*/
#pragma once
#include <iostream>
#include <span>
#include <ranges>
#include <algorithm>
#include <functional>
#include <optional>
#include <numeric>
#include <vector>
#include <deque>
#include <map>
#include <chrono>
#include <type_traits>
#include <concepts>
#include <source_location>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <cassert>

namespace sds
{
	namespace chron = std::chrono;
	using Index_t = uint16_t;
	using Nanos_t = chron::nanoseconds;
	using Clock_t = chron::steady_clock;
	using TimePoint_t = chron::time_point <Clock_t, Nanos_t>;
	using Fn_t = std::function<void()>;
	using GrpVal_t = int16_t;

	template<typename T>
	using SmallVector_t = std::vector<T>;

	template<typename Key_t, typename Val_t>
	using SmallFlatMap_t = std::map<Key_t, Val_t>;

	enum class RepeatType
	{
		// Upon the button being held down, will translate to the key-repeat function activating repeatedly using a delay in between repeats.
		Infinite,
		// Upon the button being held down, will send a single repeat, will not continue translating to repeat after the single repeat.
		FirstOnly,
		// No key-repeats sent.
		None
	};

	enum class ActionState
	{
		Init, // State indicating ready for new cycle
		KeyDown,
		KeyRepeat,
		KeyUp,
	};

	struct MappingContainer;
	struct TranslationPack;
	class Translator;

	// Concept for range of ButtonDescription type that must be contiguous.
	template<typename T>
	concept MappingRange_c = requires (T & t)
	{
		{ std::same_as<typename T::value_type, MappingContainer> == true };
		{ std::ranges::contiguous_range<T> == true };
	};

	// A translator type, wherein you can call GetUpdatedState with a range of virtual keycode integral values, and get a TranslationPack as a result.
	template<typename Poller_t>
	concept InputTranslator_c = requires(Poller_t & t)
	{
		{ t.GetUpdatedState({ 1, 2, 3 }) } -> std::convertible_to<TranslationPack>;
		{ t.GetMappingsRange() } -> std::convertible_to<std::shared_ptr<const SmallVector_t<MappingContainer>>>;
	};

	template<typename Int_t>
	concept NotBoolIntegral_c = requires(Int_t & t)
	{
		{ std::same_as<Int_t, bool> == false };
		{ std::integral<Int_t> == true };
	};

	template<typename GroupInfo_t>
	concept FilterGroupInfo_c = requires(GroupInfo_t & t)
	{
		{ t.IsMappingActivated(1) } -> std::convertible_to<bool>;
		{ t.IsMappingOvertaken(1) } -> std::convertible_to<bool>;
		{ t.IsAnyMappingActivated()	} -> std::convertible_to<bool>;
		{ t.IsMappingActivatedOrOvertaken(1) } -> std::convertible_to<bool>;
		{ t.GetActivatedValue() } -> std::convertible_to<int32_t>;
		{ t.UpdateForNewMatchingGroupingDown(1) } -> std::convertible_to<std::pair<bool, std::optional<int32_t>>>;
		{ t.UpdateForNewMatchingGroupingUp(1) } -> std::convertible_to<std::optional<int32_t>>;
	};

	/**
	* \brief	DelayTimer manages a non-blocking time delay, it provides functions such as IsElapsed() and Reset(...)
	*/
	class DelayTimer
	{
		TimePoint_t m_start_time{ Clock_t::now() };
		Nanos_t m_delayTime{}; // this should remain nanoseconds to ensure maximum granularity when Reset() with a different type.
	public:
		static constexpr Nanos_t DefaultKeyRepeatDelay{ std::chrono::milliseconds{1} };
	public:
		DelayTimer() = delete;
		explicit DelayTimer(Nanos_t duration) noexcept : m_delayTime(duration) { }
		DelayTimer(const DelayTimer& other) = default;
		DelayTimer(DelayTimer&& other) = default;
		DelayTimer& operator=(const DelayTimer& other) = default;
		DelayTimer& operator=(DelayTimer&& other) = default;
		~DelayTimer() noexcept = default;
		/**
		 * \brief	Check for elapsed.
		 * \return	true if timer has elapsed, false otherwise
		 */
		[[nodiscard]]
		bool IsElapsed() const noexcept
		{
			if (Clock_t::now() > (m_start_time + m_delayTime))
			{
				return true;
			}
			return false;
		}
		/**
		 * \brief	Reset timer with chrono duration type.
		 * \param delay		Delay in nanoseconds (or any std::chrono duration type)
		 */
		void Reset(const Nanos_t delay) noexcept
		{
			m_start_time = Clock_t::now();
			m_delayTime = { delay };
		}
		/**
		 * \brief	Reset timer to last used duration value for a new start point.
		 */
		void Reset() noexcept
		{
			m_start_time = Clock_t::now();
		}
		/**
		 * \brief	Gets the current timer period/duration for elapsing.
		 */
		auto GetTimerPeriod() const
		{
			return m_delayTime;
		}
	};

	/**
	 * \brief	Wrapper for button to action mapping state enum, the least I can do is make sure state modifications occur through a managing class,
	 *		and that there exists only one 'current' state, and that it can only be a finite set of possibilities.
	 *		Also contains last sent time (for key-repeat), and delay before first key-repeat timer.
	 * \remarks	This class enforces an invariant that it's state cannot be altered out of sequence.
	 */
	class 
		//alignas(std::hardware_constructive_interference_size) 
		MappingStateTracker
	{
		/**
		 * \brief Key Repeat Delay is the time delay a button has in-between activations.
		 */
		static constexpr std::chrono::nanoseconds DefaultKeyRepeatDelay{ std::chrono::microseconds{100'000} };
		ActionState m_currentValue{ ActionState::Init };
	public:
		/**
		 * \brief	This delay is mostly used for in-between key-repeats, but could also be in between other state transitions.
		 */
		DelayTimer LastSentTime{ DefaultKeyRepeatDelay };
		/**
		 * \brief	This is the delay before the first repeat is sent whilst holding the button down.
		 */
		DelayTimer DelayBeforeFirstRepeat{ LastSentTime.GetTimerPeriod() };
	public:
		[[nodiscard]] constexpr bool IsRepeating() const noexcept {
			return m_currentValue == ActionState::KeyRepeat;
		}
		[[nodiscard]] constexpr bool IsDown() const noexcept {
			return m_currentValue == ActionState::KeyDown;
		}
		[[nodiscard]] constexpr bool IsUp() const noexcept {
			return m_currentValue == ActionState::KeyUp;
		}
		[[nodiscard]] constexpr bool IsInitialState() const noexcept {
			return m_currentValue == ActionState::Init;
		}
		constexpr auto SetDown() noexcept
		{
			if (m_currentValue != ActionState::Init)
				return;

			m_currentValue = ActionState::KeyDown;
		}
		constexpr auto SetUp() noexcept
		{
			if (m_currentValue != ActionState::KeyDown && m_currentValue != ActionState::KeyRepeat)
				return;

			m_currentValue = ActionState::KeyUp;
		}
		constexpr auto SetRepeat() noexcept
		{
			if (m_currentValue != ActionState::KeyDown)
				return;

			m_currentValue = ActionState::KeyRepeat;
		}
		constexpr auto SetInitial() noexcept
		{
			if (m_currentValue != ActionState::KeyUp)
				return;

			m_currentValue = ActionState::Init;
		}
	};
	static_assert(std::copyable<MappingStateTracker>);
	static_assert(std::movable<MappingStateTracker>);

	struct MappingContainer
	{
		Fn_t OnDown; // Key-down
		Fn_t OnUp; // Key-up
		Fn_t OnRepeat; // Key-repeat
		Fn_t OnReset; // Reset after key-up and prior to another key-down can be performed
		int32_t ButtonVirtualKeycode{};
		RepeatType RepeatingKeyBehavior{};
		/**
		 * \brief	The exclusivity grouping member is intended to allow the user to add different groups of mappings
		 *	that require another mapping from the same group to be "overtaken" or key-up sent before the "overtaking" new mapping
		 *	can perform the key-down.
		 * \remarks		optional, if not in use set to default constructed value or '{}'
		 */
		std::optional<GrpVal_t> ExclusivityGrouping; // TODO one variation of ex. group behavior is to have a priority value associated with the mapping.
		std::optional<Nanos_t> DelayBeforeFirstRepeat;
		std::optional<Nanos_t> BetweenRepeatDelay;
	};
	static_assert(std::copyable<MappingContainer>);
	static_assert(std::movable<MappingContainer>);

	/**
	 * \brief	TranslationResult holds info from a translated state change, typically the operation to perform (if any) and
	 *		a function to call to advance the state to the next state to continue to receive proper translation results.
	 *	\remarks	The advance state function can be used to ensure the operation to perform occurs BEFORE the mapping advances it's state.
	 *		This does mean that it may be possible to induce some error related to setting the state inappropriately. Without this
	 *		design, it would be possible to, for instance, withhold calling the operation to perform, yet still have the mapping's state updating internally, erroneously.
	 *		I believe this will make calling order-dependent functionality less error-prone.
	 */
	struct
		//alignas(std::hardware_constructive_interference_size)
	TranslationResult
	{
		// Operation being requested to be performed, callable
		Fn_t OperationToPerform;
		// Function to advance the button mapping to the next state (after operation has been performed)
		Fn_t AdvanceStateFn;
		// Vk of the mapping it refers to
		int32_t MappingVk{};
		// Exclusivity grouping value, if any
		std::optional<GrpVal_t> ExclusivityGrouping;
		// Call operator, calls op fn then advances the state
		void operator()() const
		{
			OperationToPerform();
			AdvanceStateFn();
		}
	};
	static_assert(std::copyable<TranslationResult>);
	static_assert(std::movable<TranslationResult>);

	/**
	 * \brief	TranslationPack is a pack of ranges containing individual TranslationResult structs for processing state changes.
	 * \remarks		If using the provided call operator, it will prioritize key-up requests, then key-down requests, then repeat requests, then updates.
	 *	I figure it should process key-ups and new key-downs with the highest priority, after that keys doing a repeat, and lastly updates.
	 */
	struct
		//alignas(std::hardware_constructive_interference_size)
	TranslationPack
	{
		void operator()() const
		{
			// Note that there will be a function called if there is a state change,
			// it just may not have any custom behavior attached to it.
			for (const auto& elem : UpRequests)
				elem();
			for (const auto& elem : DownRequests)
				elem();
			for (const auto& elem : RepeatRequests)
				elem();
			for (const auto& elem : UpdateRequests)
				elem();
		}

		SmallVector_t<TranslationResult> UpRequests{}; // key-ups
		SmallVector_t<TranslationResult> DownRequests{}; // key-downs
		SmallVector_t<TranslationResult> RepeatRequests{}; // repeats
		SmallVector_t<TranslationResult> UpdateRequests{}; // resets
		// TODO might wrap the vectors in a struct with a call operator to have individual call operators for range of TranslationResult.
	};
	//static_assert(sizeof(TranslationPack) > 0);
	static_assert(std::copyable<TranslationPack>);
	static_assert(std::movable<TranslationPack>);

#pragma region Factory_Functions_For_Translator

	// These are a few 'factory' functions, to create the appropriate TranslationResult for the next mapping state--they are tremendously useful.
	[[nodiscard]] inline auto GetResetTranslationResult(const MappingContainer& currentMapping, MappingStateTracker& stateTracker) noexcept -> TranslationResult
	{
		return TranslationResult
		{
			.OperationToPerform = [&]()
			{
				if (currentMapping.OnReset)
					currentMapping.OnReset();
			},
			.AdvanceStateFn = [&]()
			{
				stateTracker.SetInitial();
				stateTracker.LastSentTime.Reset();
			},
			.MappingVk = currentMapping.ButtonVirtualKeycode,
			.ExclusivityGrouping = currentMapping.ExclusivityGrouping
		};
	}

	[[nodiscard]] inline auto GetRepeatTranslationResult(const MappingContainer& currentMapping, MappingStateTracker& stateTracker) noexcept -> TranslationResult
	{
		return TranslationResult
		{
			.OperationToPerform = [&]()
			{
				if (currentMapping.OnRepeat)
					currentMapping.OnRepeat();
				stateTracker.LastSentTime.Reset();
			},
			.AdvanceStateFn = [&]()
			{
				stateTracker.SetRepeat();
			},
			.MappingVk = currentMapping.ButtonVirtualKeycode,
			.ExclusivityGrouping = currentMapping.ExclusivityGrouping
		};
	}

	[[nodiscard]] inline auto GetOvertakenTranslationResult(const MappingContainer& overtakenMapping, MappingStateTracker& stateTracker) noexcept -> TranslationResult
	{
		return TranslationResult
		{
			.OperationToPerform = [&]()
			{
				if (overtakenMapping.OnUp)
					overtakenMapping.OnUp();
			},
			.AdvanceStateFn = [&]()
			{
				stateTracker.SetUp();
			},
			.MappingVk = overtakenMapping.ButtonVirtualKeycode,
			.ExclusivityGrouping = overtakenMapping.ExclusivityGrouping
		};
	}

	[[nodiscard]] inline auto GetKeyUpTranslationResult(const MappingContainer& currentMapping, MappingStateTracker& stateTracker) noexcept -> TranslationResult
	{
		return TranslationResult
		{
			.OperationToPerform = [&]()
			{
				if (currentMapping.OnUp)
					currentMapping.OnUp();
			},
			.AdvanceStateFn = [&]()
			{
				stateTracker.SetUp();
			},
			.MappingVk = currentMapping.ButtonVirtualKeycode,
			.ExclusivityGrouping = currentMapping.ExclusivityGrouping
		};
	}

	[[nodiscard]] inline auto GetInitialKeyDownTranslationResult(const MappingContainer& currentMapping, MappingStateTracker& stateTracker) noexcept -> TranslationResult
	{
		return TranslationResult
		{
			.OperationToPerform = [&]()
			{
				if (currentMapping.OnDown)
					currentMapping.OnDown();
				// Reset timer after activation, to wait for elapsed before another next state translation is returned.
				stateTracker.LastSentTime.Reset();
				stateTracker.DelayBeforeFirstRepeat.Reset();
			},
			.AdvanceStateFn = [&]()
			{
				stateTracker.SetDown();
			},
			.MappingVk = currentMapping.ButtonVirtualKeycode,
			.ExclusivityGrouping = currentMapping.ExclusivityGrouping
		};
	}

#pragma endregion

#pragma region Algos_For_Translator
	// Algorithm functions used by the translator.

	//[[nodiscard]] constexpr bool IsNotEnd(const std::ranges::range auto& theRange, const std::ranges::iterator_t<decltype(theRange)>& theIterator) noexcept
	//{
	//	return theIterator != std::ranges::cend(theRange);
	//}

	//[[nodiscard]] constexpr bool IsEnd(const std::ranges::range auto& theRange, const std::ranges::iterator_t<decltype(theRange)>& theIterator) noexcept
	//{
	//	return theIterator == std::ranges::cend(theRange);
	//}

	/**
	 * \brief For a single mapping, search the controller state update buffer and produce a TranslationResult appropriate to the current mapping state and controller state.
	 * \param downKeys Wrapper class containing the results of a controller state update polling.
	 * \param singleButton The mapping type for a single virtual key of the controller.
	 * \returns Optional, <c>TranslationResult</c>
	 */
	template<typename Val_t>
	[[nodiscard]] auto GetButtonTranslationForInitialToDown(const SmallVector_t<Val_t>& downKeys, const MappingContainer& singleButton, MappingStateTracker& stateTracker) noexcept -> std::optional<TranslationResult>
	{
		using std::ranges::find;

		if (stateTracker.IsInitialState())
		{
			const auto findResult = find(downKeys, singleButton.ButtonVirtualKeycode);
			// If VK *is* found in the down list, create the down translation.
			if (std::end(downKeys) != findResult)
				return std::make_optional<TranslationResult>(GetInitialKeyDownTranslationResult(singleButton, stateTracker));
		}
		return {};
	}

	template<typename Val_t>
	[[nodiscard]] auto GetButtonTranslationForDownToRepeat(const SmallVector_t<Val_t>& downKeys, const MappingContainer& singleButton, MappingStateTracker& stateTracker) noexcept -> std::optional<TranslationResult>
	{
		using std::ranges::find;

		const bool isDownAndUsesRepeat =
			stateTracker.IsDown()
			&& (singleButton.RepeatingKeyBehavior == RepeatType::Infinite
				|| singleButton.RepeatingKeyBehavior == RepeatType::FirstOnly);

		const bool isDelayElapsed = stateTracker.DelayBeforeFirstRepeat.IsElapsed();

		if (isDownAndUsesRepeat && isDelayElapsed)
		{
			const auto findResult = find(downKeys, singleButton.ButtonVirtualKeycode);
			// If VK *is* found in the down list, create the repeat translation.
			if (std::cend(downKeys) != findResult)
				return std::make_optional<TranslationResult>(GetRepeatTranslationResult(singleButton, stateTracker));
		}
		return {};
	}

	template<typename Val_t>
	[[nodiscard]] auto GetButtonTranslationForRepeatToRepeat(const SmallVector_t<Val_t>& downKeys, const MappingContainer& singleButton, MappingStateTracker& stateTracker) noexcept -> std::optional<TranslationResult>
	{
		using std::ranges::find;

		const bool isRepeatAndUsesInfinite = stateTracker.IsRepeating() && singleButton.RepeatingKeyBehavior == RepeatType::Infinite;
		if (isRepeatAndUsesInfinite && stateTracker.LastSentTime.IsElapsed())
		{
			const auto findResult = find(downKeys, singleButton.ButtonVirtualKeycode);
			// If VK *is* found in the down list, create the repeat translation.
			if (std::cend(downKeys) != findResult)
				return std::make_optional<TranslationResult>(GetRepeatTranslationResult(singleButton, stateTracker));
		}
		return {};
	}

	template<typename Val_t>
	[[nodiscard]] auto GetButtonTranslationForDownOrRepeatToUp(const SmallVector_t<Val_t>& downKeys, const MappingContainer& singleButton, MappingStateTracker& stateTracker) noexcept -> std::optional<TranslationResult>
	{
		using std::ranges::find;

		if (stateTracker.IsDown() || stateTracker.IsRepeating())
		{
			const auto findResult = find(downKeys, singleButton.ButtonVirtualKeycode);
			// If VK is not found in the down list, create the up translation.
			if (std::end(downKeys) == findResult)
				return std::make_optional<TranslationResult>(GetKeyUpTranslationResult(singleButton, stateTracker));
		}
		return {};
	}

	// This is the reset translation
	[[nodiscard]] inline auto GetButtonTranslationForUpToInitial(const MappingContainer& singleButton, MappingStateTracker& stateTracker) noexcept -> std::optional<TranslationResult>
	{
		// if the timer has elapsed, update back to the initial state.
		if (stateTracker.IsUp() && stateTracker.LastSentTime.IsElapsed())
		{
			return std::make_optional<TranslationResult>(GetResetTranslationResult(singleButton, stateTracker));
		}
		return {};
	}

	/**
	* \brief  Optionally returns the indices at which a mapping that matches the 'vk' was found.
	* \param vk Virtual keycode of the presumably 'down' key with which to match MappingContainer mappings.
	* \param mappingsRange The range of MappingContainer mappings for which to return the index of a matching mapping.
	*/
	[[nodiscard]] inline auto GetMappingIndexForVk(const NotBoolIntegral_c auto vk, const std::span<const MappingContainer> mappingsRange) noexcept -> std::optional<Index_t>
	{
		using std::ranges::find_if;

		const auto findResult = find_if(mappingsRange, [vk](const auto& e) { return e.ButtonVirtualKeycode == static_cast<decltype(e.ButtonVirtualKeycode)>(vk); });
		const bool didFindResult = std::cend(mappingsRange) != findResult;

		[[unlikely]]
		if (!didFindResult)
		{
			return {};
		}

		return std::make_optional<Index_t>(static_cast<Index_t>(std::distance(mappingsRange.cbegin(), findResult)));
	}

	[[nodiscard]] constexpr auto IsMappingInRange(const NotBoolIntegral_c auto vkToFind, const std::ranges::range auto& downVirtualKeys) noexcept -> bool
	{
		return std::ranges::any_of(downVirtualKeys, [vkToFind](const auto vk) { return vk == vkToFind; });
	}

	constexpr auto GetErasedRange(const std::ranges::range auto& theRange, const std::ranges::range auto& theValues) noexcept -> std::vector<int32_t>
	{
		auto copied = theRange | std::views::filter([&](const auto& elem) { return !IsMappingInRange(elem, theValues); });
		return { std::ranges::begin(copied), std::ranges::end(copied) };
	}

	/**
	 * \brief	Checks a list of mappings for having multiple mappings mapped to a single controller button.
	 * \param	mappingsList Span of controller button to action mappings.
	 * \return	true if good (or empty) mapping list, false if there is a problem.
	 */
	[[nodiscard]] inline bool AreMappingsUniquePerVk(const std::span<const MappingContainer> mappingsList) noexcept
	{
		SmallFlatMap_t<int32_t, bool> mappingTable;
		for (const auto& e : mappingsList)
		{
			if (mappingTable[e.ButtonVirtualKeycode])
			{
				return false;
			}
			mappingTable[e.ButtonVirtualKeycode] = true;
		}
		return true;
	}

	/**
	 * \brief	Checks a list of mappings for having multiple mappings mapped to a single controller button.
	 * \param	mappingsList Span of controller button to action mappings.
	 * \return	true if good (or empty) mapping list, false if there is a problem.
	 */
	[[nodiscard]] inline bool AreMappingVksNonZero(const std::span<const MappingContainer> mappingsList) noexcept
	{
		return !std::ranges::any_of(mappingsList, [](const auto vk) { return vk == 0; }, &MappingContainer::ButtonVirtualKeycode);
	}

	/**
	 * \brief Used to determine if the MappingStateManager is in a state that would require some cleanup before destruction.
	 * \remarks If you add another state for the mapping, make sure to update this.
	 * \return True if mapping needs cleanup, false otherwise.
	 */
	[[nodiscard]] constexpr bool DoesMappingNeedCleanup(const MappingStateTracker& mapping) noexcept
	{
		return mapping.IsDown() || mapping.IsRepeating();
	}

#pragma endregion Algos_For_Translator

	/**
	 * \brief Encapsulates the mapping buffer, processes controller state updates, returns translation packs.
	 * \remarks If, before destruction, the mappings are in a state other than initial or awaiting reset, then you may wish to
	 *	make use of the <c>GetCleanupActions()</c> function. Not copyable. Is movable.
	 *	<p></p>
	 *	<p>An invariant exists such that: <b>There must be only one mapping per virtual keycode.</b></p>
	 */
	class Translator
	{
		using MappingVector_t = SmallVector_t<MappingContainer>;
		using MappingStateVector_t = SmallVector_t<MappingStateTracker>;
		static_assert(MappingRange_c<MappingVector_t>);
		MappingStateVector_t m_mappingStates;
		std::shared_ptr<MappingVector_t> m_mappings;
	public:
		Translator() = delete; // no default
		Translator(const Translator& other) = delete; // no copy
		auto operator=(const Translator& other)->Translator & = delete; // no copy-assign

		Translator(Translator&& other) = default; // move-construct
		auto operator=(Translator&& other)->Translator & = default; // move-assign
		~Translator() = default;

		/**
		 * \brief Mapping Vector Ctor, may throw on exclusivity group error, OR more than one mapping per VK.
		 * \param keyMappings mapping vector type
		 * \exception std::runtime_error on exclusivity group error during construction, OR more than one mapping per VK.
		 */
		explicit Translator(MappingRange_c auto&& keyMappings)
			: m_mappings(std::make_shared<MappingVector_t>(std::forward<decltype(keyMappings)>(keyMappings)))
		{
			if (!AreMappingsUniquePerVk(*m_mappings) || !AreMappingVksNonZero(*m_mappings))
				throw std::runtime_error("Exception: More than 1 mapping per VK!");
			
			m_mappingStates.resize(m_mappings->size());
			// Zip returns a tuple of refs to the types.
			for (auto zipped : std::views::zip(*m_mappings, m_mappingStates))
			{
				const auto& mapping = std::get<0>(zipped);
				auto& mappingState = std::get<1>(zipped);
				mappingState.DelayBeforeFirstRepeat.Reset(mapping.DelayBeforeFirstRepeat.value_or(DelayTimer::DefaultKeyRepeatDelay));
				mappingState.LastSentTime.Reset(mapping.BetweenRepeatDelay.value_or(DelayTimer::DefaultKeyRepeatDelay));
			}
		}
	public:
		[[nodiscard]] auto operator()(const SmallVector_t<int32_t>& stateUpdate) noexcept -> TranslationPack
		{
			return GetUpdatedState(stateUpdate);
		}

		[[nodiscard]] auto GetUpdatedState(const SmallVector_t<int32_t>& stateUpdate) noexcept -> TranslationPack
		{
			TranslationPack translations;
			for (auto elem : std::views::zip(*m_mappings, m_mappingStates))
			{
				auto& [mapping, mappingState] = elem;
				if (const auto upToInitial = GetButtonTranslationForUpToInitial(mapping, mappingState))
				{
					translations.UpdateRequests.push_back(*upToInitial);
				}
				else if (const auto initialToDown = GetButtonTranslationForInitialToDown(stateUpdate, mapping, mappingState))
				{
					// Advance to next state.
					translations.DownRequests.push_back(*initialToDown);
				}
				else if (const auto downToFirstRepeat = GetButtonTranslationForDownToRepeat(stateUpdate, mapping, mappingState))
				{
					translations.RepeatRequests.push_back(*downToFirstRepeat);
				}
				else if (const auto repeatToRepeat = GetButtonTranslationForRepeatToRepeat(stateUpdate, mapping, mappingState))
				{
					translations.RepeatRequests.push_back(*repeatToRepeat);
				}
				else if (const auto repeatToUp = GetButtonTranslationForDownOrRepeatToUp(stateUpdate, mapping, mappingState))
				{
					translations.UpRequests.push_back(*repeatToUp);
				}
			}
			return translations;
		}

		[[nodiscard]] auto GetCleanupActions() noexcept -> SmallVector_t<TranslationResult>
		{
			SmallVector_t<TranslationResult> translations;
			for (auto elem : std::views::zip(*m_mappings, m_mappingStates))
			{
				auto& [mapping, mappingState] = elem;
				if (DoesMappingNeedCleanup(mappingState))
				{
					translations.push_back(GetKeyUpTranslationResult(mapping, mappingState));
				}
			}
			return translations;
		}

		[[nodiscard]] auto GetMappingsRange() const noexcept -> std::shared_ptr<const MappingVector_t>
		{
			return m_mappings;
		}
	};
	static_assert(InputTranslator_c<Translator> == true);
	static_assert(std::movable<Translator> == true);
	static_assert(std::copyable<Translator> == false);

	/**
	 * \brief	<para>A logical representation of a mapping's exclusivity group activation status, for this setup a single key in the exclusivity group can be 'activated'
	 *	or have a key-down state at a time. It is exclusively the only key in the group forwarded to the translator for processing of key-down events.</para>
	 * <para>Essentially this is used to ensure only a single key per exclusivity grouping is down at a time, and keys can overtake the current down key. </para>
	 * \remarks This abstraction manages the currently activated key being "overtaken" by another key from the same group and causing a key-up/down to be sent for the currently activated,
	 *	as well as moving the key in line behind the newly activated key. A much needed abstraction.
	 */
	class GroupActivationInfo
	{
		using Elem_t = int32_t;

		// First element of the queue is the activated mapping.
		std::deque<Elem_t> ActivatedValuesQueue;
	public:
		/**
		 * \brief Boolean of the returned pair is whether or not the keydown should be filtered/removed.
		 *	The optional value is (optionally) referring to the mapping to send a new key-up for.
		 * \remarks An <b>precondition</b> is that the mapping's value passed into this has a matching exclusivity grouping!
		 */
		[[nodiscard]] auto UpdateForNewMatchingGroupingDown(const Elem_t newDownVk) noexcept -> std::pair<bool, std::optional<Elem_t>>
		{
			// Filter all of the hashes already activated/overtaken.
			const bool isActivated = IsMappingActivated(newDownVk);
			const bool isOvertaken = IsMappingOvertaken(newDownVk);
			const bool doFilterTheDown = isOvertaken;
			if (isActivated || isOvertaken)
				return std::make_pair(doFilterTheDown, std::optional<Elem_t>{});

			// If any mapping hash is already activated, this new hash will be overtaking it and thus require a key-up for current activated.
			if (IsAnyMappingActivated())
			{
				const auto currentDownValue = ActivatedValuesQueue.front();
				ActivatedValuesQueue.push_front(newDownVk);
				return std::make_pair(false, std::make_optional<Elem_t>(currentDownValue));
			}

			// New activated mapping case, add to queue in first position and don't filter. No key-up required.
			ActivatedValuesQueue.push_front(newDownVk);
			return std::make_pair(false, std::optional<Elem_t>{});
		}

		/**
		 * \brief The optional value is (optionally) referring to the mapping to send a new key-down for,
		 *	in the event that the currently activated key is key-up'd and there is an overtaken key waiting behind it in the queue.
		 * \remarks An <b>precondition</b> is that the mapping passed into this has a matching exclusivity grouping!
		 */
		auto UpdateForNewMatchingGroupingUp(const Elem_t newUpVk) noexcept -> std::optional<Elem_t>
		{
			// Handle no hashes in queue to update case, and specific new up hash not in queue either.
			if (!IsAnyMappingActivated())
				return {};

			const auto findResult = std::ranges::find(ActivatedValuesQueue, newUpVk);
			const bool isFound = findResult != std::ranges::cend(ActivatedValuesQueue);

			if (isFound)
			{
				const bool isInFirstPosition = findResult == ActivatedValuesQueue.cbegin();

				// Case wherein the currently activated mapping is the one getting a key-up.
				if (isInFirstPosition)
				{
					if (!ActivatedValuesQueue.empty())
					{
						// If there is an overtaken queue, key-down the next key in line.
						ActivatedValuesQueue.pop_front();
						// Return the new front hash to be sent a key-down.
						return !ActivatedValuesQueue.empty() ? std::make_optional<Elem_t>(ActivatedValuesQueue.front()) : std::optional<Elem_t>{};
					}
				}

				// otherwise, just remove it from the queue because it hasn't been key-down'd (it's one of the overtaken, or size is 1).
				ActivatedValuesQueue.erase(findResult);
			}

			return {};
		}

	public:
		[[nodiscard]] bool IsMappingActivated(const Elem_t vk) const noexcept
		{
			if (ActivatedValuesQueue.empty())
				return false;
			return vk == ActivatedValuesQueue.front();
		}
		[[nodiscard]] bool IsMappingOvertaken(const Elem_t vk) const noexcept
		{
			if (ActivatedValuesQueue.empty())
				return false;

			const bool isCurrentActivation = ActivatedValuesQueue.front() == vk;
			const auto findResult = std::ranges::find(ActivatedValuesQueue, vk);
			const bool isFound = findResult != std::ranges::cend(ActivatedValuesQueue);
			return !isCurrentActivation && isFound;
		}
		[[nodiscard]] bool IsAnyMappingActivated() const noexcept 
		{
			return !ActivatedValuesQueue.empty();
		}
		[[nodiscard]] bool IsMappingActivatedOrOvertaken(const Elem_t vk) const noexcept
		{
			const auto findResult = std::ranges::find(ActivatedValuesQueue, vk);
			return findResult != std::ranges::cend(ActivatedValuesQueue);
		}
		[[nodiscard]] auto GetActivatedValue() const noexcept -> Elem_t
		{
			assert(!ActivatedValuesQueue.empty());
			return ActivatedValuesQueue.front();
		}
	};
	static_assert(std::movable<GroupActivationInfo>);
	static_assert(std::copyable<GroupActivationInfo>);

	/**
	 * \brief	May be used to internally filter the poller's translations in order to apply the overtaking behavior.
	 * \remarks This behavior is deviously complex, and modifications are best done to "GroupActivationInfo" only, if at all possible.
	 *	In the event that a single state update contains presently un-handled key-downs for mappings with the same exclusivity grouping,
	 *	it will only process a single overtaking key-down at a time, and will suppress the rest in the state update to be handled on the next iteration.
	 */
	template<typename GroupInfo_t = GroupActivationInfo>
	class OvertakingFilter
	{
		using VirtualCode_t = int32_t; 

		std::unordered_set<VirtualCode_t> m_allVirtualKeycodes; // Order does not match mappings
		// const ptr to mappings
		std::shared_ptr<const SmallVector_t<MappingContainer>> m_mappings;

		// map of grouping value to GroupActivationInfo container.
		std::unordered_map<GrpVal_t, GroupInfo_t> m_groupMap;
		// Mapping of grouping value to mapping indices.
		std::unordered_map<GrpVal_t, std::set<VirtualCode_t>> m_groupToVkMap;
		
		std::unordered_map<int32_t, Index_t> m_vkToIndexMap;
	public:
		OvertakingFilter() noexcept = delete;

		explicit OvertakingFilter(const InputTranslator_c auto& translator) noexcept
		{
			auto pMappings = translator.GetMappingsRange();

			SetMappingRange(pMappings);
		}

		// This function is used to filter the controller state updates before they are sent to the translator.
		// It will have an effect on overtaking behavior by modifying the state update buffer, which just contains the virtual keycodes that are reported as down.
		[[nodiscard]] auto GetFilteredButtonState(const SmallVector_t<VirtualCode_t>& stateUpdate) noexcept -> SmallVector_t<VirtualCode_t>
		{
			using std::ranges::sort;
			using std::views::filter;

			// Sorting provides an ordering to which down states with an already handled exclusivity grouping get filtered out for this iteration.
			//sort(stateUpdate, std::ranges::less{}); // TODO <-- problem for the (current) unit testing, optional anyway

			// Filters out VKs that don't have any corresponding mapping.
			auto filteredUpdateView = stateUpdate | filter([this](const auto vk) { return m_allVirtualKeycodes.contains(vk); });
			const std::vector<int32_t> filteredStateUpdate = { filteredUpdateView.cbegin(), filteredUpdateView.cend() };

			const auto uniqueGrouped = GetNonUniqueGroupElements(filteredStateUpdate);

			const auto filteredForDown = FilterDownTranslation(uniqueGrouped);

			// There appears to be no reason to report additional VKs that will become 'down' after a key is moved to up,
			// because for the key to still be in the overtaken queue, it would need to still be 'down' as well, and thus handled
			// by the down filter.
			FilterUpTranslation(stateUpdate);

			return filteredForDown;
		}

		auto operator()(const SmallVector_t<VirtualCode_t>& stateUpdate) noexcept -> SmallVector_t<VirtualCode_t>
		{
			return GetFilteredButtonState(stateUpdate);
		}
	private:

		void SetMappingRange(const std::shared_ptr<const SmallVector_t<MappingContainer>>& mappingsList) noexcept
		{
			m_mappings = mappingsList;
			m_allVirtualKeycodes = {};
			m_groupMap = {};
			m_groupToVkMap = {};
			m_vkToIndexMap = {};

			BuildAllMemos(m_mappings);
		}

		// A somewhat important bit of memoization/pre-processing.
		void BuildAllMemos(const std::shared_ptr<const SmallVector_t<MappingContainer>>& mappingsList) noexcept
		{
			using std::views::enumerate;

			for (const auto& [index, elem] : enumerate(*mappingsList))
			{
				// all vk set
				m_allVirtualKeycodes.insert(elem.ButtonVirtualKeycode);
				m_vkToIndexMap[elem.ButtonVirtualKeycode] = static_cast<Index_t>(index);

				if (elem.ExclusivityGrouping)
				{
					// group to group info map
					m_groupMap[elem.ExclusivityGrouping.value()] = {};
					// group to vk map
					m_groupToVkMap[elem.ExclusivityGrouping.value()].insert(elem.ButtonVirtualKeycode);
				}
			}
		}

		[[nodiscard]] auto FilterDownTranslation(const SmallVector_t<VirtualCode_t>& stateUpdate) noexcept -> SmallVector_t<VirtualCode_t>
		{
			using std::views::filter;
			using std::views::transform;

			const auto vkToMappingIndex = [&](const auto vk) -> std::optional<Index_t>
				{
					using std::ranges::find_if;
					if (m_allVirtualKeycodes.contains(vk))
					{
						return std::make_optional<Index_t>(m_vkToIndexMap[vk]);
					}
					return {};
				};
			const auto optWithValueAndGroup = [&](const auto opt) -> bool
				{
					return opt.has_value() && GetMappingAt(*opt).ExclusivityGrouping.has_value();
				};
			const auto removeOpt = [&](const auto opt)
				{
					return opt.value();
				};

			auto stateUpdateCopy = stateUpdate;
			SmallVector_t<VirtualCode_t> vksToRemoveRange;
			// This appeared (at this time) to be the best option: 
			// input vk List -> xform to mapping index list -> filter results to only include non-empty optional and ex. group -> xform to remove the optional = index list of mappings in the state update that have an ex. group.
			for (const auto& mappingIndex : stateUpdateCopy | transform(vkToMappingIndex) | filter(optWithValueAndGroup) | transform(removeOpt))
			{
				auto& currentMapping = GetMappingAt(mappingIndex);
				auto& currentGroup = m_groupMap[*currentMapping.ExclusivityGrouping];

				const auto& [shouldFilter, upOpt] = currentGroup.UpdateForNewMatchingGroupingDown(currentMapping.ButtonVirtualKeycode);
				if (shouldFilter)
				{
					vksToRemoveRange.push_back(currentMapping.ButtonVirtualKeycode);
				}
				if (upOpt)
				{
					vksToRemoveRange.push_back(*upOpt);
				}
			}

			return GetErasedRange(stateUpdateCopy, vksToRemoveRange);
		}

		// it will process only one key per ex. group per iteration. The others will be filtered out and handled on the next iteration.
		void FilterUpTranslation(const SmallVector_t<VirtualCode_t>& stateUpdate) noexcept
		{
			using std::views::filter;
			// filters for all mappings of interest per the current 'down' VK buffer (the UP mappings in this case).
			const auto exGroupAndNotInUpdatePred = [&](const auto& currentMapping)
				{
					const bool hasValue = currentMapping.ExclusivityGrouping.has_value();
					const bool notInUpdate = !IsMappingInRange(currentMapping.ButtonVirtualKeycode, stateUpdate);
					return hasValue && notInUpdate;
				};

			for (const auto& currentMapping : (*m_mappings) | filter(exGroupAndNotInUpdatePred))
			{
				auto& currentGroup = m_groupMap[*currentMapping.ExclusivityGrouping];
				currentGroup.UpdateForNewMatchingGroupingUp(currentMapping.ButtonVirtualKeycode);
			}
		}

	private:
		[[nodiscard]] constexpr auto GetMappingAt(const NotBoolIntegral_c auto index) noexcept -> const MappingContainer&
		{
			return m_mappings->at(static_cast<Index_t>(index));
		}

		[[nodiscard]] constexpr auto GetMappingForVk(const NotBoolIntegral_c auto vk) noexcept -> const MappingContainer&
		{
			auto ind = GetMappingIndexForVk(vk, *m_mappings);
			assert(ind.has_value());
			return GetMappingAt(*ind);
		}

		// Pre: VKs in state update do have a mapping.
		[[nodiscard]] auto GetNonUniqueGroupElements(const SmallVector_t<int32_t>& stateUpdate) noexcept -> SmallVector_t<int32_t>
		{
			using std::ranges::find, std::ranges::cend;
			using StateRange_t = std::remove_cvref_t<decltype(stateUpdate)>;

			SmallVector_t<GrpVal_t> groupingValueBuffer;
			StateRange_t virtualKeycodesToRemove;
			groupingValueBuffer.reserve(stateUpdate.size());
			virtualKeycodesToRemove.reserve(stateUpdate.size());

			for (const auto vk : stateUpdate)
			{
				const auto mappingIndex = m_vkToIndexMap[vk];
				const auto& foundMappingForVk = GetMappingAt(mappingIndex);

				if (foundMappingForVk.ExclusivityGrouping)
				{
					const auto grpVal = foundMappingForVk.ExclusivityGrouping.value();
					auto& currentGroup = m_groupMap[grpVal];
					if (!currentGroup.IsMappingActivatedOrOvertaken(vk))
					{
						const auto groupingFindResult = find(groupingValueBuffer, grpVal);

						// If already in located, being handled groupings, add to remove buffer.
						if (groupingFindResult != cend(groupingValueBuffer))
							virtualKeycodesToRemove.emplace_back(vk);
						// Otherwise, add this new grouping to the grouping value buffer.
						else
							groupingValueBuffer.emplace_back(grpVal);
					}
				}
			}
			
			return GetErasedRange(stateUpdate, virtualKeycodesToRemove);
		}
	};
	static_assert(std::copyable<OvertakingFilter<>>);
	static_assert(std::movable<OvertakingFilter<>>);
}
