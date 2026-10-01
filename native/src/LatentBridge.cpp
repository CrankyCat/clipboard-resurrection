#include "LatentBridge.h"
#include "LoggingPolicy.h"
#include "ConduitConnections.h"
#include "EngineAPI.h"
#include "ImportAnimation.h"
#include "ImportJobState.h"
#include "ImportCursorPayload.h"
#include "ImportRowResults.h"
#include "ImportProgress.h"
#include "PerformanceBatchState.h"
#include "EngineTaskDispatch.h"
#include "PowerTaskState.h"
#include "PapyrusBinding.h"
#include "WorkshopCallbackState.h"
#include "WorkshopWaitSnapshots.h"
#include "WorkshopCallbackSpacing.h"
#include "WorkshopProgress.h"
#include "WorkshopNoOp.h"
#include "LocalizationRuntime.h"
#include <RE/Bethesda/SendHUDMessage.h>
#include "SerializationNames.h"
#include "SerializationValues.h"
#include "LegacyCleanupGate.h"
#include <random>
#include <unordered_map>

extern std::atomic_bool& enableLogging;

namespace Clipboard::Latent
{
	namespace
	{
		constexpr std::string_view kPapyrusClass{ "ClipboardExtension" };
		constexpr std::uint32_t kFunctorVersion = 1;
		constexpr std::uint32_t kMaximumSerializedArrayElements = 262144;
		constexpr std::uint16_t kMaximumSerializedStringBytes = 32767;

		using namespace Clipboard::SerializationNames;

		static_assert(sizeof(RE::BSScript::Variable) == 0x10);
		static_assert(sizeof(bool) == 1);

		Callbacks g_callbacks;
		F4SE::DelayFunctorManager* g_manager = nullptr;
		std::mutex g_registrationLock;
		std::mutex g_bindingLock;
		RE::BSScript::IVirtualMachine* g_bindingAttemptedVM = nullptr;
		RE::BSScript::IVirtualMachine* g_boundVM = nullptr;

		enum class RegistrationState
		{
			kUninitialized,
			kFactoriesReady,
			kReady,
			kFailed
		};

		std::atomic<RegistrationState> g_registrationState{ RegistrationState::kUninitialized };
		LegacyCleanup::Gate g_cleanupGate{ [] {
			// Saved Papyrus stacks can outlive a process. Randomize the initial
			// token so an old stack cannot ordinarily own a new process's session.
			try { return static_cast<std::int32_t>(std::random_device{}() & 0x3FFFFFFF); }
			catch (...) { return static_cast<std::int32_t>(GetTickCount64() & 0x3FFFFFFF); }
		}() };

		template <class T>
		[[nodiscard]] bool WriteExact(const F4SE::SerializationInterface* a_intfc, const T& a_value)
		{
			return a_intfc && a_intfc->WriteRecordData(std::addressof(a_value), static_cast<std::uint32_t>(sizeof(T)));
		}

		template <class T>
		[[nodiscard]] bool ReadExact(const F4SE::SerializationInterface* a_intfc, T& a_value)
		{
			if constexpr (std::is_same_v<T, bool>) {
				return Clipboard::SerializationValues::ReadBool(
					[a_intfc](std::uint8_t& byte) {
						return a_intfc && a_intfc->ReadRecordData(std::addressof(byte), sizeof(byte)) == sizeof(byte);
					}, a_value);
			} else {
				return a_intfc &&
					a_intfc->ReadRecordData(std::addressof(a_value), static_cast<std::uint32_t>(sizeof(T))) == sizeof(T);
			}
		}

		[[nodiscard]] bool WriteString(const F4SE::SerializationInterface* a_intfc, std::string_view a_value)
		{
			if (a_value.size() > kMaximumSerializedStringBytes) {
				return false;
			}

			const auto length = static_cast<std::uint16_t>(a_value.size());
			return WriteExact(a_intfc, length) &&
				(length == 0 || a_intfc->WriteRecordData(a_value.data(), length));
		}

		[[nodiscard]] bool ReadString(const F4SE::SerializationInterface* a_intfc, RE::BSFixedString& a_value)
		{
			std::uint16_t length = 0;
			if (!ReadExact(a_intfc, length) || length > kMaximumSerializedStringBytes) {
				return false;
			}

			std::vector<char> buffer(static_cast<std::size_t>(length) + 1, '\0');
			if (length != 0 && a_intfc->ReadRecordData(buffer.data(), length) != length) {
				return false;
			}

			a_value = buffer.data();
			return true;
		}

		[[nodiscard]] RE::BSTSmartPointer<RE::BSScript::IVirtualMachine> GetVirtualMachine()
		{
			const auto gameVM = RE::GameVM::GetSingleton();
			return gameVM ? gameVM->GetVM() : nullptr;
		}

		[[nodiscard]] bool RestoreObjectVariable(
			const F4SE::SerializationInterface* a_intfc,
			const RE::BSFixedString& a_typeName,
			std::uint64_t a_savedHandle,
			RE::BSScript::Variable& a_value)
		{
			a_value = nullptr;
			if (a_savedHandle == 0) {
				return true;
			}

			const auto resolvedHandle = a_intfc->ResolveHandle(a_savedHandle);
			if (!resolvedHandle) {
				return true;
			}

			const auto vm = GetVirtualMachine();
			RE::BSTSmartPointer<RE::BSScript::ObjectTypeInfo> typeInfo;
			if (!vm || a_typeName.empty() || !vm->GetScriptObjectType(a_typeName, typeInfo) || !typeInfo) {
				return false;
			}

			RE::BSTSmartPointer<RE::BSScript::Object> object;
			if (!vm->FindBoundObject(*resolvedHandle, typeInfo->name.c_str(), false, object, false)) {
				if (vm->CreateObject(typeInfo->name, object) && object) {
					vm->GetObjectBindPolicy().BindObject(object, *resolvedHandle);
				}
			}

			if (object) {
				a_value = std::move(object);
			}
			return true;
		}

		[[nodiscard]] bool WriteVariable(
			const F4SE::SerializationInterface* a_intfc,
			const RE::BSScript::Variable& a_value)
		{
			using RawType = RE::BSScript::TypeInfo::RawType;
			const auto rawType = a_value.GetType().GetRawType();
			const auto typeByte = static_cast<std::uint8_t>(rawType);
			if (!WriteExact(a_intfc, typeByte)) {
				return false;
			}

			switch (rawType) {
			case RawType::kNone:
				return true;
			case RawType::kObject:
			{
				const auto object = RE::BSScript::get<RE::BSScript::Object>(a_value);
				if (!object || !object->GetTypeInfo()) {
					return WriteString(a_intfc, {}) && WriteExact(a_intfc, std::uint64_t{ 0 });
				}
				const auto& typeName = object->GetTypeInfo()->name;
				return WriteString(a_intfc, static_cast<std::string_view>(typeName)) &&
					WriteExact(a_intfc, static_cast<std::uint64_t>(object->GetHandle()));
			}
			case RawType::kString:
			{
				const auto value = RE::BSScript::get<RE::BSFixedString>(a_value);
				return WriteString(a_intfc, static_cast<std::string_view>(value));
			}
			case RawType::kInt:
				return WriteExact(a_intfc, RE::BSScript::get<std::uint32_t>(a_value));
			case RawType::kFloat:
				return WriteExact(a_intfc, RE::BSScript::get<float>(a_value));
			case RawType::kBool:
				return WriteExact(a_intfc, RE::BSScript::get<bool>(a_value));
			case RawType::kArrayObject:
			{
				const auto array = RE::BSScript::get<RE::BSScript::Array>(a_value);
				if (!array || array->size() > kMaximumSerializedArrayElements) {
					return false;
				}

				const auto* elementType = array->type_info().GetObjectTypeInfo();
				if (!elementType) {
					return false;
				}

				const auto count = static_cast<std::uint32_t>(array->size());
				if (!WriteExact(a_intfc, count) ||
					!WriteString(a_intfc, static_cast<std::string_view>(elementType->name))) {
					return false;
				}

				for (const auto& element : array->elements) {
					std::uint64_t handle = 0;
					if (element.is<RE::BSScript::Object>()) {
						const auto object = RE::BSScript::get<RE::BSScript::Object>(element);
						handle = object ? static_cast<std::uint64_t>(object->GetHandle()) : 0;
					} else if (!element.is<std::nullptr_t>()) {
						return false;
					}
					if (!WriteExact(a_intfc, handle)) {
						return false;
					}
				}
				return true;
			}
			default:
				return false;
			}
		}

		[[nodiscard]] bool ReadVariable(
			const F4SE::SerializationInterface* a_intfc,
			RE::BSScript::Variable& a_value)
		{
			using RawType = RE::BSScript::TypeInfo::RawType;
			std::uint8_t typeByte = 0;
			if (!ReadExact(a_intfc, typeByte)) {
				return false;
			}

			const auto rawType = static_cast<RawType>(typeByte);
			switch (rawType) {
			case RawType::kNone:
				a_value = nullptr;
				return true;
			case RawType::kObject:
			{
				RE::BSFixedString typeName;
				std::uint64_t handle = 0;
				return ReadString(a_intfc, typeName) && ReadExact(a_intfc, handle) &&
					RestoreObjectVariable(a_intfc, typeName, handle, a_value);
			}
			case RawType::kString:
			{
				RE::BSFixedString value;
				if (!ReadString(a_intfc, value)) {
					return false;
				}
				a_value = std::move(value);
				return true;
			}
			case RawType::kInt:
			{
				std::uint32_t value = 0;
				if (!ReadExact(a_intfc, value)) {
					return false;
				}
				a_value = value;
				return true;
			}
			case RawType::kFloat:
			{
				float value = 0.0F;
				if (!ReadExact(a_intfc, value)) {
					return false;
				}
				a_value = value;
				return true;
			}
			case RawType::kBool:
			{
				bool value = false;
				if (!ReadExact(a_intfc, value)) {
					return false;
				}
				a_value = value;
				return true;
			}
			case RawType::kArrayObject:
			{
				std::uint32_t count = 0;
				RE::BSFixedString typeName;
				if (!ReadExact(a_intfc, count) || count > kMaximumSerializedArrayElements ||
					!ReadString(a_intfc, typeName) || typeName.empty()) {
					return false;
				}

				std::vector<std::uint64_t> handles(count);
				for (auto& handle : handles) {
					if (!ReadExact(a_intfc, handle)) {
						return false;
					}
				}

				const auto vm = GetVirtualMachine();
				RE::BSTSmartPointer<RE::BSScript::Array> array;
				if (!vm || !vm->CreateArray(RawType::kObject, typeName, count, array) || !array) {
					return false;
				}

				for (std::uint32_t index = 0; index < count; ++index) {
					if (!RestoreObjectVariable(a_intfc, typeName, handles[index], array->elements[index])) {
						return false;
					}
				}

				a_value = std::move(array);
				return true;
			}
			default:
				return false;
			}
		}

		template <class T>
		struct IsVMArray : std::false_type
		{};

		template <class T>
		struct IsVMArray<VMArray<T>> : std::true_type
		{};

		template <class T>
		inline constexpr bool IsVMArrayV = IsVMArray<T>::value;

		template <class T>
		[[nodiscard]] std::optional<T> UnpackSafely(const RE::BSScript::Variable& a_value)
		{
			if constexpr (std::is_pointer_v<T> &&
				std::is_base_of_v<RE::TESForm, std::remove_pointer_t<T>>) {
				if (!a_value.is<RE::BSScript::Object>()) {
					return std::nullopt;
				}

				const auto object = RE::BSScript::get<RE::BSScript::Object>(a_value);
				const auto vm = GetVirtualMachine();
				if (!object || !vm ||
					!vm->GetObjectHandlePolicy().IsHandleLoaded(object->GetHandle())) {
					return std::nullopt;
				}
				auto* result = RE::BSScript::UnpackVariable<std::remove_pointer_t<T>>(a_value);
				return result ? std::optional<T>{ result } : std::nullopt;
			} else if constexpr (IsVMArrayV<T>) {
				using Element = typename T::value_type;
				if (!a_value.is<RE::BSScript::Array>()) {
					return std::nullopt;
				}

				const auto array = RE::BSScript::get<RE::BSScript::Array>(a_value);
				if (!array) {
					return std::nullopt;
				}

				T result;
				for (const auto& elementValue : array->elements) {
					if constexpr (std::is_pointer_v<Element> &&
						std::is_base_of_v<RE::TESForm, std::remove_pointer_t<Element>>) {
						if (elementValue.is<std::nullptr_t>()) {
							result.push_back(nullptr);
							continue;
						}
					}

					auto element = UnpackSafely<Element>(elementValue);
					if (!element) {
						return std::nullopt;
					}
					result.push_back(std::move(*element));
				}
				return result;
			} else if constexpr (std::is_same_v<T, RE::BSFixedString>) {
				return a_value.is<RE::BSFixedString>() ?
					std::optional<T>{ RE::BSScript::get<RE::BSFixedString>(a_value) } : std::nullopt;
			} else if constexpr (std::is_same_v<T, bool>) {
				return a_value.is<bool>() ? std::optional<T>{ RE::BSScript::get<bool>(a_value) } : std::nullopt;
			} else if constexpr (std::is_floating_point_v<T>) {
				return a_value.is<float>() ?
					std::optional<T>{ static_cast<T>(RE::BSScript::get<float>(a_value)) } : std::nullopt;
			} else if constexpr (std::is_integral_v<T>) {
				if (!a_value.is<std::uint32_t>()) {
					return std::nullopt;
				}
				return static_cast<T>(RE::BSScript::get<std::uint32_t>(a_value));
			} else {
				return std::nullopt;
			}
		}

		template <class... Args, std::size_t... Indices>
		[[nodiscard]] std::optional<std::tuple<Args...>> UnpackArguments(
			const std::array<RE::BSScript::Variable, sizeof...(Args)>& a_arguments,
			std::index_sequence<Indices...>)
		{
			auto unpacked = std::tuple<std::optional<Args>...>{ UnpackSafely<Args>(a_arguments[Indices])... };
			if (!(... && std::get<Indices>(unpacked).has_value())) {
				return std::nullopt;
			}

			return std::tuple<Args...>{ std::move(*std::get<Indices>(unpacked))... };
		}

		class LatentFunctorBase : public F4SE::DelayFunctor
		{
		public:
			explicit LatentFunctorBase(std::uint32_t a_stackID) :
				_stackID(a_stackID), _workTicket(g_cleanupGate)
			{}
			// F4SE also constructs a tag-only sample just to ask its class name.
			// Register restored work in LoadStack, not in these metadata samples.
			explicit LatentFunctorBase(F4SE::SerializationTag) noexcept {}

			bool ShouldReschedule(std::int32_t& a_delayMilliseconds) override
			{
				a_delayMilliseconds = 0;
				return false;
			}

			bool ShouldResumeStack(std::uint32_t& a_stackID) override
			{
				a_stackID = _stackID;
				return true;
			}

		protected:
			[[nodiscard]] bool SaveStack(const F4SE::SerializationInterface* a_intfc) const
			{
				return WriteExact(a_intfc, _stackID);
			}

			[[nodiscard]] bool LoadStack(const F4SE::SerializationInterface* a_intfc)
			{
				if (!ReadExact(a_intfc, _stackID)) { return false; }
				_workTicket.Start(g_cleanupGate);
				return true;
			}

			std::uint32_t _stackID{ 0 };
			LegacyCleanup::WorkTicket _workTicket;
		};

		// The added ticket is process-local; SaveStack and every payload retain
		// their previous byte layout and class version.
		static_assert(sizeof(LatentFunctorBase) == 0x18);

		template <const char* Name, auto CallbackMember, class Result, class... Args>
		class TypedFunctor final : public LatentFunctorBase
		{
		public:
			explicit TypedFunctor(F4SE::SerializationTag a_tag) :
				LatentFunctorBase(a_tag)
			{}

			explicit TypedFunctor(std::uint32_t a_stackID, Args... a_arguments) :
				LatentFunctorBase(a_stackID)
			{
				PackArguments(std::index_sequence_for<Args...>{}, std::move(a_arguments)...);
			}

			[[nodiscard]] const char* ClassName() const override { return Name; }
			[[nodiscard]] std::uint32_t ClassVersion() const override { return kFunctorVersion; }

			bool Save(const F4SE::SerializationInterface* a_intfc) override
			{
				try {
					if (!SaveStack(a_intfc)) {
						return false;
					}
					for (const auto& argument : _arguments) {
						if (!WriteVariable(a_intfc, argument)) {
							return false;
						}
					}
					return true;
				} catch (...) {
					return false;
				}
			}

			bool Load(const F4SE::SerializationInterface* a_intfc, std::uint32_t a_version) override
			{
				try {
					if (a_version != kFunctorVersion || !LoadStack(a_intfc)) {
						return false;
					}
					for (auto& argument : _arguments) {
						if (!ReadVariable(a_intfc, argument)) {
							return false;
						}
					}
					return true;
				} catch (...) {
					return false;
				}
			}

			bool Run(RE::BSScript::Variable& a_result) override
			{
				try {
					CLIPBOARD_DEBUG_LOG(F4SE::log::info("Running latent functor {} for Papyrus stack {}", Name, _stackID));
					SetEmptyResult(a_result);
					if (g_registrationState.load(std::memory_order_acquire) != RegistrationState::kReady) {
						F4SE::log::error("Latent functor {} ran before Clipboard registration completed", Name);
						return false;
					}

					auto arguments = UnpackArguments<Args...>(_arguments, std::index_sequence_for<Args...>{});
					if (!arguments) {
						F4SE::log::error("Could not unpack saved arguments for latent functor {}", Name);
						return false;
					}

					const auto callback = g_callbacks.*CallbackMember;
					if (!callback) {
						F4SE::log::error("Callback is unavailable for latent functor {}", Name);
						return false;
					}

					if constexpr (std::is_void_v<Result>) {
						std::apply(
							[&](auto&&... unpacked) {
								callback(_stackID, nullptr, std::forward<decltype(unpacked)>(unpacked)...);
							},
							std::move(*arguments));
						CLIPBOARD_DEBUG_LOG(F4SE::log::info("Completed latent functor {} for Papyrus stack {}", Name, _stackID));
					} else {
						auto value = std::apply(
							[&](auto&&... unpacked) {
								return callback(_stackID, nullptr, std::forward<decltype(unpacked)>(unpacked)...);
							},
							std::move(*arguments));
						if constexpr (IsVMArrayV<Result>) {
							CLIPBOARD_DEBUG_LOG(F4SE::log::info(
								"Completed latent functor {} for Papyrus stack {} with {} result rows",
								Name,
								_stackID,
								value.Length()));
						} else {
							CLIPBOARD_DEBUG_LOG(F4SE::log::info("Completed latent functor {} for Papyrus stack {}", Name, _stackID));
						}
						RE::BSScript::PackVariable(a_result, std::move(value));
					}
					return true;
				} catch (...) {
					// Never unwind through the F4SE object ABI. The manager will resume
					// the Papyrus stack with either the typed empty value initialized
					// above or None if even that allocation failed.
					try {
						F4SE::log::error("Latent functor {} failed", Name);
					} catch (...) {
					}
				}
				return false;
			}

		private:
			template <std::size_t... Indices>
			void PackArguments(std::index_sequence<Indices...>, Args... a_arguments)
			{
				(RE::BSScript::PackVariable(_arguments[Indices], std::move(a_arguments)), ...);
			}

			static void SetEmptyResult(RE::BSScript::Variable& a_result)
			{
				if constexpr (std::is_void_v<Result>) {
					a_result = nullptr;
				} else {
					RE::BSScript::PackVariable(a_result, Result{});
				}
			}

			std::array<RE::BSScript::Variable, sizeof...(Args)> _arguments;
		};

		using CreateSelectionBoxFunctor = TypedFunctor<
			kCreateSelectionBoxName,
			&Callbacks::createSelectionBox,
			ReferenceArray,
			RE::TESObjectREFR*,
			RE::TESForm*,
			std::uint32_t,
			std::uint32_t,
			std::uint32_t>;
		using ScrapSelectionFunctor = TypedFunctor<
			kScrapSelectionName, &Callbacks::scrapSelection, void, RE::TESObjectREFR*>;
		using ScrapObjectsFunctor = TypedFunctor<
			kScrapObjectsName, &Callbacks::scrapObjects, void, ReferenceArray>;
		using SendWorkshopEventFunctor = TypedFunctor<
			kSendWorkshopEventName,
			&Callbacks::sendWorkshopEvent,
			void,
			RE::TESObjectREFR*,
			RE::BSFixedString>;
		using EnableObjectsFunctor = TypedFunctor<
			kEnableObjectsName, &Callbacks::enableObjects, void, ReferenceArray>;
		using DisableObjectsFunctor = TypedFunctor<
			kDisableObjectsName, &Callbacks::disableObjects, void, ReferenceArray>;
		using ScaleSelectionFunctor = TypedFunctor<
			kScaleSelectionName,
			&Callbacks::scaleSelection,
			void,
			RE::TESObjectREFR*,
			float,
			bool>;
		using TryScaleSelectionFunctor = TypedFunctor<kTryScaleSelectionName,
			&Callbacks::tryScaleSelection, bool, RE::TESObjectREFR*, float, bool>;
		using PastePatternObjectsFunctor = TypedFunctor<
			kPastePatternObjectsName,
			&Callbacks::pastePatternObjects,
			ReferenceArray,
			RE::TESObjectREFR*,
			std::uint32_t>;
		using PastePatternObjectsWithReuseFunctor = TypedFunctor<
			kPastePatternObjectsWithReuseName, &Callbacks::pastePatternObjectsWithReuse,
			VMArray<ImportPlacementRow>, RE::TESObjectREFR*, std::uint32_t>;

		// The new jobs share the existing versioned VM-variable serializer and
		// public F4SE object ABI; legacy functor layouts above remain unchanged.
#include "EnginePowerFunctor.inl"
#include "PowerWireFunctor.inl"
#include "ImportFunctors.inl"
#include "WorkshopCallbackFunctors.inl"
#include "PerformanceBatchFunctors.inl"

		template <class Result, class... Args>
		class StaticLatentNativeFunction final : public RE::BSScript::NF_util::NativeFunctionBase
		{
		private:
			using Super = RE::BSScript::NF_util::NativeFunctionBase;
			using Function = bool(
				RE::BSScript::IVirtualMachine&,
				std::uint32_t,
				std::monostate,
				Args...);

		public:
			template <class F>
			StaticLatentNativeFunction(
				std::string_view a_name,
				F&& a_function,
				bool a_taskletCallable) :
				Super(kPapyrusClass, a_name, sizeof...(Args), true, true),
				_stub(std::forward<F>(a_function))
			{
				static_assert(RE::BSScript::detail::ValidateReturn<Result>());
				static_assert((RE::BSScript::detail::ValidateParameter<Args>() && ...));

				std::size_t index = 0;
				const auto setParameterType = [this, &index]<class T>() {
					const auto typeInfo =
						RE::BSScript::GetTypeInfo<RE::BSScript::detail::decay_t<T>>();
					if (!typeInfo) {
						_signatureValid = false;
						++index;
						return;
					}
					Super::descTable.entries[index++].second = *typeInfo;
				};
				(setParameterType.template operator()<Args>(), ...);

				const auto returnType =
					RE::BSScript::GetTypeInfo<RE::BSScript::detail::decay_t<Result>>();
				if (returnType) {
					Super::retType = *returnType;
				} else {
					_signatureValid = false;
				}
				Super::SetCallableFromTasklets(a_taskletCallable);
			}

			[[nodiscard]] bool HasStub() const override { return static_cast<bool>(_stub); }
			[[nodiscard]] bool HasValidSignature() const noexcept { return _signatureValid; }

			bool MarshallAndDispatch(
				RE::BSScript::Variable& a_self,
				RE::BSScript::Internal::VirtualMachine& a_vm,
				std::uint32_t a_stackID,
				RE::BSScript::Variable& a_returnValue,
				const RE::BSScript::StackFrame& a_stackFrame) const override
			{
				a_returnValue = nullptr;
				const auto stack = a_stackFrame.parent;
				if (!stack) {
					F4SE::log::error("Latent native {} called without a Papyrus stack", GetName().c_str());
					return false;
				}

				try {
					const bool enqueued = RE::BSScript::detail::DispatchHelper<true, std::monostate, Args...>(
						a_self,
						a_vm,
						a_stackID,
						a_stackFrame,
						*stack,
						_stub,
						std::index_sequence_for<Args...>{});
					a_returnValue = enqueued;
					return true;
				} catch (const std::exception& error) {
					F4SE::log::error("Could not enqueue latent native {}: {}", GetName().c_str(), error.what());
				} catch (...) {
					F4SE::log::error("Could not enqueue latent native {}: unknown exception", GetName().c_str());
				}

				a_returnValue = false;
				return true;
			}

		private:
			std::function<Function> _stub;
			bool _signatureValid{ true };
		};

		template <class Functor, class... Args>
		[[nodiscard]] bool EnqueueFunctor(std::uint32_t a_stackID, Args&&... a_arguments)
		{
			if (g_registrationState.load(std::memory_order_acquire) != RegistrationState::kReady || !g_manager) {
				Functor sample{ F4SE::SerializationTag{} };
				F4SE::log::error(
					"Could not enqueue latent functor {} for Papyrus stack {}: registration is not ready",
					sample.ClassName(),
					a_stackID);
				return false;
			}

			try {
				auto functor = std::make_unique<Functor>(a_stackID, std::forward<Args>(a_arguments)...);
				CLIPBOARD_DEBUG_LOG(F4SE::log::info(
					"Enqueueing latent functor {} for Papyrus stack {}",
					functor->ClassName(),
					a_stackID));
				g_manager->Enqueue(functor.get());
				(void)functor.release();
				return true;
			} catch (const std::exception& error) {
				F4SE::log::error("Could not allocate/enqueue latent functor: {}", error.what());
			} catch (...) {
				F4SE::log::error("Could not allocate/enqueue latent functor: unknown exception");
			}
			return false;
		}

		template <class Result, class... Args>
		[[nodiscard]] bool BindLatent(
			RE::BSScript::IVirtualMachine& a_vm,
			std::string_view a_name,
			bool (*a_callback)(RE::BSScript::IVirtualMachine&, std::uint32_t, std::monostate, Args...),
			bool a_taskletCallable)
		{
			auto function = std::make_unique<StaticLatentNativeFunction<Result, Args...>>(
				a_name, a_callback, a_taskletCallable);
			if (!function->HasValidSignature()) {
				F4SE::log::error("Could not resolve the Papyrus types for latent native {}.{}", kPapyrusClass, a_name);
				return false;
			}
			if (!Clipboard::PapyrusBinding::Bind(a_vm, std::move(function))) {
				F4SE::log::error("Could not bind latent native {}.{}; check the matching ClipboardExtension.pex and Papyrus log", kPapyrusClass, a_name);
				return false;
			}
			CLIPBOARD_DEBUG_LOG(F4SE::log::info("Bound latent native {}.{}", kPapyrusClass, a_name));
			return true;
		}

		[[nodiscard]] bool CallbacksComplete(const Callbacks& a_callbacks) noexcept
		{
			return a_callbacks.createSelectionBox && a_callbacks.scrapSelection &&
				a_callbacks.scrapObjects && a_callbacks.sendWorkshopEvent &&
				a_callbacks.enableObjects && a_callbacks.disableObjects &&
				a_callbacks.scaleSelection && a_callbacks.tryScaleSelection && a_callbacks.createTransmitPowerJob &&
				a_callbacks.pastePatternObjects && a_callbacks.createPatternWireJob &&
				a_callbacks.snapshotScrap && a_callbacks.scrapTarget &&
				a_callbacks.useWorkshopThrottling && a_callbacks.pastePatternObjectsWithReuse;
		}

		template <class Functor, const char* ReadAlias = nullptr>
		[[nodiscard]] bool PreflightFactoryName(F4SE::ObjectRegistry& a_registry)
		{
			Functor sample{ F4SE::SerializationTag{} };
			const auto* className = sample.ClassName();
			if (!className || className[0] == '\0') {
				F4SE::log::critical("Refusing to register an unnamed Clipboard latent functor factory");
				return false;
			}
			if (a_registry.GetFactoryByName(className)) {
				F4SE::log::critical("Clipboard latent functor factory name is already registered: {}", className);
				return false;
			}
			if constexpr (ReadAlias != nullptr) {
				if (a_registry.GetFactoryByName(ReadAlias)) {
					F4SE::log::critical("Clipboard legacy read alias is already registered: {}", ReadAlias);
					return false;
				}
			}
			return true;
		}

		template <class Functor, const char* ReadAlias = nullptr>
		[[nodiscard]] bool RegisterFactoryChecked(F4SE::ObjectRegistry& a_registry)
		{
			Functor sample{ F4SE::SerializationTag{} };
			const auto* className = sample.ClassName();
			if (!a_registry.RegisterClass<Functor>()) {
				F4SE::log::critical("F4SE rejected Clipboard latent functor factory: {}", className);
				return false;
			}

			const auto* registered = a_registry.GetFactoryByName(className);
			if (!registered || !registered->ClassName() ||
				std::string_view(registered->ClassName()) != className) {
				F4SE::log::critical("Clipboard latent functor factory could not be verified: {}", className);
				return false;
			}
			if constexpr (ReadAlias != nullptr) {
				if (!F4SE::RegisterReadAlias<Functor, ReadAlias>(a_registry)) {
					F4SE::log::critical("F4SE rejected Clipboard legacy read alias: {}", ReadAlias);
					return false;
				}
				const auto* alias = a_registry.GetFactoryByName(ReadAlias);
				if (!alias || !alias->ClassName() || std::string_view(alias->ClassName()) != ReadAlias) {
					F4SE::log::critical("Clipboard legacy read alias could not be verified: {}", ReadAlias);
					return false;
				}
			}
			return true;
		}

		[[nodiscard]] bool RegisterFactories(const Callbacks& a_callbacks)
		{
			std::scoped_lock lock(g_registrationLock);
			const auto state = g_registrationState.load(std::memory_order_relaxed);
			if (state == RegistrationState::kFactoriesReady || state == RegistrationState::kReady) {
				return true;
			}
			if (state == RegistrationState::kFailed ||
				!CallbacksComplete(a_callbacks)) {
				g_registrationState.store(RegistrationState::kFailed, std::memory_order_release);
				return false;
			}

			const auto objectInterface = F4SE::GetObjectInterface();
			if (!objectInterface || objectInterface->Version() != F4SE::ObjectInterface::kVersion) {
				F4SE::log::critical("F4SE Object interface ABI is not the required version 1");
				g_registrationState.store(RegistrationState::kFailed, std::memory_order_release);
				return false;
			}

			g_callbacks = a_callbacks;
			auto& registry = objectInterface->GetObjectRegistry();
			const bool namesAvailable =
				PreflightFactoryName<CreateSelectionBoxFunctor, kCreateSelectionBoxLegacyName>(registry) &&
				PreflightFactoryName<ScrapSelectionFunctor, kScrapSelectionLegacyName>(registry) &&
				PreflightFactoryName<ScrapObjectsFunctor, kScrapObjectsLegacyName>(registry) &&
				PreflightFactoryName<SendWorkshopEventFunctor, kSendWorkshopEventLegacyName>(registry) &&
				PreflightFactoryName<EnableObjectsFunctor, kEnableObjectsLegacyName>(registry) &&
				PreflightFactoryName<DisableObjectsFunctor, kDisableObjectsLegacyName>(registry) &&
				PreflightFactoryName<ScaleSelectionFunctor, kScaleSelectionLegacyName>(registry) &&
				PreflightFactoryName<TryScaleSelectionFunctor>(registry) &&
				PreflightFactoryName<TransmitPowerFunctor, kTransmitPowerLegacyName>(registry) &&
				PreflightFactoryName<PastePatternObjectsFunctor, kPastePatternObjectsLegacyName>(registry) &&
				PreflightFactoryName<PastePatternWiresFunctor, kPastePatternWiresLegacyName>(registry) &&
				PreflightFactoryName<PastePatternWiresForRowsFunctor, kPastePatternWiresForRowsLegacyName>(registry) &&
				PreflightFactoryName<PrepareImportedObjectsFunctor, kPrepareImportedObjectsLegacyName>(registry) &&
				PreflightFactoryName<PrepareImportedRowsFunctor>(registry) &&
				PreflightFactoryName<InitializeImportedWorkshopRowsFunctor>(registry) &&
				PreflightFactoryName<ReconnectImportedPowerForRowsFunctor>(registry) &&
				PreflightFactoryName<ConnectImportedPowerForRowsFunctor>(registry) &&
				PreflightFactoryName<RefreshImportedPowerForRowsFunctor>(registry) &&
				PreflightFactoryName<GetImportedPowerGeneratorsFunctor, kGetImportedPowerGeneratorsLegacyName>(registry) &&
				PreflightFactoryName<ReconnectImportedPowerFunctor, kReconnectImportedPowerLegacyName>(registry) &&
				PreflightFactoryName<InitializeImportedWorkshopObjectsFunctor, kInitializeImportedWorkshopObjectsLegacyName>(registry) &&
				PreflightFactoryName<ScrapObjectsPacedFunctor>(registry) &&
				PreflightFactoryName<GetImportedAnimationCandidatesFunctor>(registry) &&
				PreflightFactoryName<PastePatternObjectsWithReuseFunctor>(registry);
			if (!namesAvailable) {
				g_registrationState.store(RegistrationState::kFailed, std::memory_order_release);
				return false;
			}

			const bool registered =
				RegisterFactoryChecked<CreateSelectionBoxFunctor, kCreateSelectionBoxLegacyName>(registry) &&
				RegisterFactoryChecked<ScrapSelectionFunctor, kScrapSelectionLegacyName>(registry) &&
				RegisterFactoryChecked<ScrapObjectsFunctor, kScrapObjectsLegacyName>(registry) &&
				RegisterFactoryChecked<SendWorkshopEventFunctor, kSendWorkshopEventLegacyName>(registry) &&
				RegisterFactoryChecked<EnableObjectsFunctor, kEnableObjectsLegacyName>(registry) &&
				RegisterFactoryChecked<DisableObjectsFunctor, kDisableObjectsLegacyName>(registry) &&
				RegisterFactoryChecked<ScaleSelectionFunctor, kScaleSelectionLegacyName>(registry) &&
				RegisterFactoryChecked<TryScaleSelectionFunctor>(registry) &&
				RegisterFactoryChecked<TransmitPowerFunctor, kTransmitPowerLegacyName>(registry) &&
				RegisterFactoryChecked<PastePatternObjectsFunctor, kPastePatternObjectsLegacyName>(registry) &&
				RegisterFactoryChecked<PastePatternWiresFunctor, kPastePatternWiresLegacyName>(registry) &&
				RegisterFactoryChecked<PastePatternWiresForRowsFunctor, kPastePatternWiresForRowsLegacyName>(registry) &&
				RegisterFactoryChecked<PrepareImportedObjectsFunctor, kPrepareImportedObjectsLegacyName>(registry) &&
				RegisterFactoryChecked<PrepareImportedRowsFunctor>(registry) &&
				RegisterFactoryChecked<InitializeImportedWorkshopRowsFunctor>(registry) &&
				RegisterFactoryChecked<ReconnectImportedPowerForRowsFunctor>(registry) &&
				RegisterFactoryChecked<ConnectImportedPowerForRowsFunctor>(registry) &&
				RegisterFactoryChecked<RefreshImportedPowerForRowsFunctor>(registry) &&
				RegisterFactoryChecked<GetImportedPowerGeneratorsFunctor, kGetImportedPowerGeneratorsLegacyName>(registry) &&
				RegisterFactoryChecked<ReconnectImportedPowerFunctor, kReconnectImportedPowerLegacyName>(registry) &&
				RegisterFactoryChecked<InitializeImportedWorkshopObjectsFunctor, kInitializeImportedWorkshopObjectsLegacyName>(registry) &&
				RegisterFactoryChecked<ScrapObjectsPacedFunctor>(registry) &&
				RegisterFactoryChecked<GetImportedAnimationCandidatesFunctor>(registry) &&
				RegisterFactoryChecked<PastePatternObjectsWithReuseFunctor>(registry);

			if (!registered) {
				F4SE::log::critical("One or more Clipboard latent factory names could not be registered");
				g_registrationState.store(RegistrationState::kFailed, std::memory_order_release);
				return false;
			}

			g_manager = std::addressof(objectInterface->GetDelayFunctorManager());
			g_registrationState.store(RegistrationState::kFactoriesReady, std::memory_order_release);
			CLIPBOARD_DEBUG_LOG(F4SE::log::info("Registered twenty-three Clipboard latent functor factories and fifteen legacy read aliases"));
			return true;
		}
	}

	bool EnqueueCreateSelectionBox(
		RE::BSScript::IVirtualMachine&,
		std::uint32_t a_stackID,
		std::monostate,
		RE::TESObjectREFR* a_tool,
		RE::TESForm* a_wall,
		std::uint32_t a_xLength,
		std::uint32_t a_yLength,
		std::uint32_t a_zLength)
	{
		return EnqueueFunctor<CreateSelectionBoxFunctor>(
			a_stackID, a_tool, a_wall, a_xLength, a_yLength, a_zLength);
	}

	bool EnqueueScrapSelection(
		RE::BSScript::IVirtualMachine&,
		std::uint32_t a_stackID,
		std::monostate,
		RE::TESObjectREFR* a_reference)
	{
		return EnqueueFunctor<ScrapSelectionFunctor>(a_stackID, a_reference);
	}

	bool EnqueueScrapObjects(
		RE::BSScript::IVirtualMachine&,
		std::uint32_t a_stackID,
		std::monostate,
		ReferenceArray a_references)
	{
		return EnqueueFunctor<ScrapObjectsFunctor>(a_stackID, std::move(a_references));
	}

	bool EnqueueSendWorkshopEvent(
		RE::BSScript::IVirtualMachine&,
		std::uint32_t a_stackID,
		std::monostate,
		RE::TESObjectREFR* a_reference,
		RE::BSFixedString a_eventName)
	{
		return EnqueueFunctor<SendWorkshopEventFunctor>(
			a_stackID, a_reference, std::move(a_eventName));
	}

	bool EnqueueEnableObjects(
		RE::BSScript::IVirtualMachine&,
		std::uint32_t a_stackID,
		std::monostate,
		ReferenceArray a_references)
	{
		return EnqueueFunctor<EnableObjectsFunctor>(a_stackID, std::move(a_references));
	}

	bool EnqueueDisableObjects(
		RE::BSScript::IVirtualMachine&,
		std::uint32_t a_stackID,
		std::monostate,
		ReferenceArray a_references)
	{
		return EnqueueFunctor<DisableObjectsFunctor>(a_stackID, std::move(a_references));
	}

	bool EnqueueTryScaleSelection(RE::BSScript::IVirtualMachine&, std::uint32_t stack,
		std::monostate, RE::TESObjectREFR* reference, float factor, bool whole)
	{
		return EnqueueFunctor<TryScaleSelectionFunctor>(stack, reference, factor, whole);
	}

	bool EnqueueScaleSelection(
		RE::BSScript::IVirtualMachine&,
		std::uint32_t a_stackID,
		std::monostate,
		RE::TESObjectREFR* a_reference,
		float a_scale,
		bool a_maintainShape)
	{
		return EnqueueFunctor<ScaleSelectionFunctor>(
			a_stackID, a_reference, a_scale, a_maintainShape);
	}

	bool EnqueueTransmitPower(
		RE::BSScript::IVirtualMachine&,
		std::uint32_t a_stackID,
		std::monostate,
		RE::TESObjectREFR* a_reference)
	{
		return EnqueueFunctor<TransmitPowerFunctor>(a_stackID, a_reference);
	}

	bool EnqueuePastePatternObjects(
		RE::BSScript::IVirtualMachine&,
		std::uint32_t a_stackID,
		std::monostate,
		RE::TESObjectREFR* a_reference,
		std::uint32_t a_slot)
	{
		return EnqueueFunctor<PastePatternObjectsFunctor>(a_stackID, a_reference, a_slot);
	}

	bool EnqueuePastePatternObjectsWithReuse(
		RE::BSScript::IVirtualMachine&, std::uint32_t a_stackID, std::monostate,
		RE::TESObjectREFR* a_reference, std::uint32_t a_slot)
	{
		return EnqueueFunctor<PastePatternObjectsWithReuseFunctor>(a_stackID, a_reference, a_slot);
	}

	bool EnqueuePastePatternWires(
		RE::BSScript::IVirtualMachine&,
		std::uint32_t a_stackID,
		std::monostate,
		RE::TESObjectREFR* a_reference,
		std::uint32_t a_slot)
	{
		return EnqueueFunctor<PastePatternWiresFunctor>(a_stackID, a_reference, a_slot);
	}

	bool EnqueuePastePatternWiresForRows(
		RE::BSScript::IVirtualMachine&,
		std::uint32_t a_stackID,
		std::monostate,
		RE::TESObjectREFR* a_reference,
		std::uint32_t a_slot,
		ReferenceArray a_placedRows)
	{
		return EnqueueFunctor<PastePatternWiresForRowsFunctor>(
			a_stackID, a_reference, a_slot, std::move(a_placedRows));
	}

	bool EnqueuePrepareImportedObjects(RE::BSScript::IVirtualMachine&, std::uint32_t stackID, std::monostate,
		RE::TESObjectREFR* tool, ReferenceArray rows, bool disableHavok)
	{
		return EnqueueFunctor<PrepareImportedObjectsFunctor>(stackID, tool, std::move(rows), disableHavok);
	}

	ReferenceArray GetSuccessfulImportRows(StaticFunctionTag*, ReferenceArray sources, ReferenceArray originals, VMArray<std::int32_t> result, bool workshop)
	{
		try {
			if (sources.size() != originals.size() || !ImportRows::Valid(result, originals.size(), workshop)) {
				F4SE::log::error("Import row results invalid or completion uncertain; refusing subsequent wiring/power");
				return {};
			}
			const auto header = workshop ? ImportRows::kWorkshopHeader : ImportRows::kPreparationHeader;
			std::unordered_set<std::uint32_t> excluded;
			for (std::size_t i = 0; i < originals.size(); ++i) {
				if (auto* ref = originals[i]; ref && (result[header + i] != ImportRows::Complete || ref->IsDeleted())) { excluded.insert(ref->formID); }
				if (result[header + i] == ImportRows::Failed || result[header + i] == ImportRows::Pending ||
					(result[header + i] == ImportRows::Complete && !originals[i])) { sources[i] = nullptr; }
			}
			for (auto*& ref : sources) {
				if (ref && (ref->IsDeleted() || excluded.contains(ref->formID))) { ref = nullptr; }
			}
			return sources;
		} catch (...) { return {}; }
	}
	bool EnqueuePrepareImportedRows(RE::BSScript::IVirtualMachine&, std::uint32_t stackID, std::monostate,
		RE::TESObjectREFR* tool, ReferenceArray rows, bool disableHavok)
	{
		return EnqueueFunctor<PrepareImportedRowsFunctor>(stackID, tool, std::move(rows), disableHavok);
	}
	bool EnqueueInitializeImportedWorkshopRows(RE::BSScript::IVirtualMachine&, std::uint32_t stackID, std::monostate,
		RE::TESObjectREFR* tool, ReferenceArray rows)
	{
		return EnqueueFunctor<InitializeImportedWorkshopRowsFunctor>(stackID, tool, std::move(rows));
	}
	bool EnqueueReconnectImportedPowerForRows(RE::BSScript::IVirtualMachine&, std::uint32_t stackID, std::monostate,
		RE::TESObjectREFR* tool, ReferenceArray rows, ReferenceArray originals)
	{
		return EnqueueFunctor<ReconnectImportedPowerForRowsFunctor>(stackID, tool, std::move(rows), false, std::move(originals));
	}
	bool EnqueueConnectImportedPowerForRows(RE::BSScript::IVirtualMachine&, std::uint32_t stackID, std::monostate,
		RE::TESObjectREFR* tool, ReferenceArray rows, ReferenceArray originals)
	{
		return EnqueueFunctor<ConnectImportedPowerForRowsFunctor>(stackID, tool, std::move(rows), false, std::move(originals));
	}
	bool EnqueueRefreshImportedPowerForRows(RE::BSScript::IVirtualMachine&, std::uint32_t stackID, std::monostate,
		RE::TESObjectREFR* tool, ReferenceArray rows, ReferenceArray originals)
	{
		return EnqueueFunctor<RefreshImportedPowerForRowsFunctor>(stackID, tool, std::move(rows), false, std::move(originals));
	}

	bool EnqueueGetImportedPowerGenerators(RE::BSScript::IVirtualMachine&, std::uint32_t stackID, std::monostate,
		ReferenceArray rows)
	{
		return EnqueueFunctor<GetImportedPowerGeneratorsFunctor>(stackID, nullptr, std::move(rows), false);
	}

	bool EnqueueReconnectImportedPower(RE::BSScript::IVirtualMachine&, std::uint32_t stackID, std::monostate,
		RE::TESObjectREFR* tool, ReferenceArray rows)
	{
		return EnqueueFunctor<ReconnectImportedPowerFunctor>(stackID, tool, std::move(rows), false);
	}

	void CancelImportedObjects(StaticFunctionTag*, RE::TESObjectREFR* tool) { CancelImportTool(tool); }
	std::int32_t BeginImportedPowerProgress(StaticFunctionTag*, RE::TESObjectREFR* tool, ReferenceArray rows)
	{
		try { return StartPowerProgress(tool, rows); } catch (...) { return 0; }
	}
	void AdvanceImportedPowerProgress(StaticFunctionTag*, RE::TESObjectREFR* tool, std::int32_t token, std::int32_t row)
	{
		try {
			if (!tool || token <= 0 || row < 0) { return; }
			RE::BSScript::Variable value;
			RE::BSScript::PackVariable(value, tool);
			const auto session = FindPowerProgress(value);
			if (session && session->token == token) { session->Complete(static_cast<std::size_t>(row), 2); }
		} catch (...) { /* Progress reporting cannot fail an import. */ }
	}
	bool EndImportedPowerProgress(StaticFunctionTag*, RE::TESObjectREFR* tool, std::int32_t token)
	{
		try { return StopPowerProgress(tool, token); } catch (...) { return false; }
	}
	void ClearImportedProgress(StaticFunctionTag*, RE::TESObjectREFR* tool)
	{
		if (tool) { ImportProgress::ClearPercentage(tool->formID); }
	}
	bool EnqueueScrapObjectsPaced(RE::BSScript::IVirtualMachine&, std::uint32_t stackID, std::monostate,
		RE::TESObjectREFR* tool, ReferenceArray rows)
	{
		return EnqueueFunctor<ScrapObjectsPacedFunctor>(stackID, tool, std::move(rows));
	}
	bool EnqueueGetImportedAnimationCandidates(RE::BSScript::IVirtualMachine&, std::uint32_t stackID, std::monostate,
		RE::TESObjectREFR* tool, ReferenceArray rows)
	{
		return EnqueueFunctor<GetImportedAnimationCandidatesFunctor>(stackID, tool, std::move(rows));
	}
	bool EnqueueInitializeImportedWorkshopObjects(RE::BSScript::IVirtualMachine&, std::uint32_t stackID, std::monostate,
		RE::TESObjectREFR* tool, ReferenceArray rows)
	{
		return EnqueueFunctor<InitializeImportedWorkshopObjectsFunctor>(stackID, tool, std::move(rows));
	}
	std::int32_t BeginLegacyCleanup() { return g_cleanupGate.Begin(); }
	bool IsLegacyCleanupActive(std::int32_t token) { return g_cleanupGate.Active(token); }
	void EndLegacyCleanup(std::int32_t token) { g_cleanupGate.End(token); }
	void ResetImportJobs() { g_cleanupGate.ResetForLoad(); ResetImportLeases(); }

	bool Register(RE::BSScript::IVirtualMachine& a_vm, const Callbacks& a_callbacks)
	{
		try {
			if (!RegisterFactories(a_callbacks)) {
				return false;
			}

			std::scoped_lock lock(g_bindingLock);
			if (g_boundVM == std::addressof(a_vm)) {
				return true;
			}
			if (g_bindingAttemptedVM == std::addressof(a_vm)) {
				F4SE::log::error("Clipboard latent natives previously failed to bind on this Papyrus VM");
				return false;
			}
			g_bindingAttemptedVM = std::addressof(a_vm);

			// Preserve deployed metadata exactly. The v221 typo targeted a
			// nonexistent CreateSelectionBoxFunctor native, so CreateSelectionBox
			// was the sole non-tasklet-callable operation; the other nine were
			// explicitly marked NoWait/tasklet-callable.
			bool bound = true;
			bound &= BindLatent<ReferenceArray, RE::TESObjectREFR*, RE::TESForm*, std::uint32_t, std::uint32_t, std::uint32_t>(
				a_vm, "CreateSelectionBox", EnqueueCreateSelectionBox, false);
			bound &= BindLatent<void, RE::TESObjectREFR*>(a_vm, "ScrapSelection", EnqueueScrapSelection, true);
			bound &= BindLatent<void, ReferenceArray>(a_vm, "ScrapObjects", EnqueueScrapObjects, true);
			bound &= BindLatent<void, RE::TESObjectREFR*, RE::BSFixedString>(
				a_vm, "SendWorkshopEventToSelectedObjects", EnqueueSendWorkshopEvent, true);
			bound &= BindLatent<void, ReferenceArray>(a_vm, "EnableObjects", EnqueueEnableObjects, true);
			bound &= BindLatent<void, ReferenceArray>(a_vm, "DisableObjects", EnqueueDisableObjects, true);
			bound &= BindLatent<void, RE::TESObjectREFR*, float, bool>(
				a_vm, "ScaleSelection", EnqueueScaleSelection, true);
			bound &= BindLatent<bool, RE::TESObjectREFR*, float, bool>(a_vm, "TryScaleSelection", EnqueueTryScaleSelection, true);
			bound &= BindLatent<void, RE::TESObjectREFR*>(a_vm, "TransmitPowerInSelection", EnqueueTransmitPower, true);
			bound &= BindLatent<ReferenceArray, RE::TESObjectREFR*, std::uint32_t>(
				a_vm, "PastePatternObjects", EnqueuePastePatternObjects, true);
			bound &= BindLatent<VMArray<ImportPlacementRow>, RE::TESObjectREFR*, std::uint32_t>(
				a_vm, "PastePatternObjectsWithReuse", EnqueuePastePatternObjectsWithReuse, true);
			bound &= BindLatent<ReferenceArray, RE::TESObjectREFR*, std::uint32_t>(
				a_vm, "PastePatternWires", EnqueuePastePatternWires, true);
			bound &= BindLatent<ReferenceArray, RE::TESObjectREFR*, std::uint32_t, ReferenceArray>(
				a_vm, "PastePatternWiresForRows", EnqueuePastePatternWiresForRows, true);
			bound &= BindLatent<ImportSummary, RE::TESObjectREFR*, ReferenceArray, bool>(
				a_vm, "PrepareImportedObjects", EnqueuePrepareImportedObjects, true);
			bound &= BindLatent<ImportSummary, RE::TESObjectREFR*, ReferenceArray, bool>(
				a_vm, "PrepareImportedRows", EnqueuePrepareImportedRows, true);
			bound &= BindLatent<ImportSummary, RE::TESObjectREFR*, ReferenceArray>(
				a_vm, "InitializeImportedWorkshopRows", EnqueueInitializeImportedWorkshopRows, true);
			bound &= BindLatent<ImportSummary, RE::TESObjectREFR*, ReferenceArray, ReferenceArray>(
				a_vm, "ReconnectImportedPowerForRows", EnqueueReconnectImportedPowerForRows, true);
			bound &= BindLatent<ImportSummary, RE::TESObjectREFR*, ReferenceArray, ReferenceArray>(
				a_vm, "ConnectImportedPowerForRows", EnqueueConnectImportedPowerForRows, true);
			bound &= BindLatent<ImportSummary, RE::TESObjectREFR*, ReferenceArray, ReferenceArray>(
				a_vm, "RefreshImportedPowerForRows", EnqueueRefreshImportedPowerForRows, true);
			bound &= BindLatent<ReferenceArray, ReferenceArray>(
				a_vm, "GetImportedPowerGenerators", EnqueueGetImportedPowerGenerators, true);
			bound &= BindLatent<ImportSummary, RE::TESObjectREFR*, ReferenceArray>(
				a_vm, "ReconnectImportedPower", EnqueueReconnectImportedPower, true);
			bound &= BindLatent<ImportSummary, RE::TESObjectREFR*, ReferenceArray>(
				a_vm, "InitializeImportedWorkshopObjects", EnqueueInitializeImportedWorkshopObjects, true);
			bound &= BindLatent<ImportSummary, RE::TESObjectREFR*, ReferenceArray>(
				a_vm, "ScrapObjectsPaced", EnqueueScrapObjectsPaced, true);
			bound &= BindLatent<ImportSummary, RE::TESObjectREFR*, ReferenceArray>(
				a_vm, "GetImportedAnimationCandidates", EnqueueGetImportedAnimationCandidates, true);
			if (bound) {
				g_boundVM = std::addressof(a_vm);
				g_registrationState.store(RegistrationState::kReady, std::memory_order_release);
				CLIPBOARD_DEBUG_LOG(F4SE::log::info("Bound twenty-three Clipboard latent Papyrus natives with their eventual return types"));
			} else {
				g_registrationState.store(RegistrationState::kFailed, std::memory_order_release);
			}
			return bound;
		} catch (const std::exception& error) {
			g_registrationState.store(RegistrationState::kFailed, std::memory_order_release);
			F4SE::log::critical("Clipboard latent registration failed: {}", error.what());
		} catch (...) {
			g_registrationState.store(RegistrationState::kFailed, std::memory_order_release);
			F4SE::log::critical("Clipboard latent registration failed with an unknown exception");
		}
		return false;
	}
}
