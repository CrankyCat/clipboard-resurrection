#pragma once

#include "PCH.h"

namespace Clipboard::EngineAPI
{
	using PlaceAtMe_t = RE::TESObjectREFR* (*)(
		RE::BSScript::IVirtualMachine*,
		std::uint32_t,
		RE::TESObjectREFR**,
		RE::TESForm*,
		std::int32_t,
		bool,
		bool,
		bool);
	using GetLinkedRef_t = RE::TESObjectREFR* (*)(RE::TESObjectREFR*, RE::BGSKeyword*);
	using GetLocationReferenceID_t = std::uint32_t (*)(const RE::ObjectRefHandle&);
	using SetLinkedRef_t = void (*)(RE::TESObjectREFR*, RE::TESObjectREFR*, RE::BGSKeyword*);
	using MoveRefrToPosition_t = void (*)(
		RE::TESObjectREFR*,
		std::uint32_t*,
		RE::TESObjectCELL*,
		RE::TESWorldSpace*,
		RE::NiPoint3*,
		RE::NiPoint3*);
	using Enable_t = void (*)(RE::TESObjectREFR*, bool);
	using Disable_t = std::uint32_t (*)(RE::TESObjectREFR*, bool);
	using EffectShaderPlay_t = void (*)(
		RE::TESObjectREFR*,
		RE::TESEffectShader*,
		float,
		void*,
		std::uint32_t,
		void*,
		void*,
		std::uint32_t);
	using EffectShaderStop_t = void (*)(
		RE::ProcessLists*,
		RE::TESObjectREFR*,
		RE::TESEffectShader*);
	using WorkshopExtraAddItem_t = void (*)(RE::Workshop::ExtraData*, RE::TESObjectREFR*);
	using WorkshopExtraAddConnection_t = void (*)(
		RE::Workshop::ExtraData*,
		RE::TESObjectREFR*,
		RE::TESObjectREFR*,
		RE::TESObjectREFR*);
	using UpdateSpline_t = void (*)(
		RE::Workshop::ContextData*,
		RE::TESObjectREFR*,
		RE::TESObjectREFR*,
		std::int32_t,
		RE::TESObjectREFR*,
		std::int32_t);
	using UpdateMovingWirelessItem_t = void (*)(RE::TESObjectREFR*, RE::BSExtraData*);
	using EstablishTerminalLinks_t = void (*)(RE::TESObjectREFR*);
	// CommonLibF4RD leaves the current smart-reference parameter incomplete in
	// Workshop.h.  Fallout 4 1.11.221 exposed the same x64 call as a NiPointer;
	// the pointer/reference ABI is unchanged at the reviewed 1.11.240 symbol.
	using ScrapReference_t = void (*)(
		RE::Workshop::ContextData*,
		RE::NiPointer<RE::TESObjectREFR>*,
		RE::BSTArray<RE::BSTTuple<RE::TESBoundObject*, std::uint32_t>>*);
	using CallFunctionNoWait_t = void (*)(
		RE::BSScript::IVirtualMachine*,
		std::uint64_t,
		RE::BSScript::Object*,
		const RE::BSFixedString*,
		RE::BSScript::Variable*);

	namespace ConnectPoint
	{
		class Parent : public RE::BSIntrusiveRefCounted
		{
		public:
			F4_HEAP_REDEFINE_NEW(Parent);

			RE::BSFixedString parentName;
			RE::BSFixedString name;
			RE::NiQuaternion rotation;
			RE::NiPoint3 position;
			float scale;
		};
		static_assert(sizeof(Parent) == 0x38);

		enum class Status : std::int32_t
		{
			kNoReference = 0,
			kNoSnapPoint,
			kSnapPointFound,
			kNonReferenceHit,
			kCount
		};

		struct Result
		{
			Status status{ Status::kCount };
			RE::BSTSmartPointer<Parent> foundSnapPoint;
		};
		static_assert(sizeof(Result) == 0x10);
	}

	using GetSnappedReferenceImpl_t = RE::TESObjectREFR* (*)(
		const RE::TESObjectREFR&,
		const RE::NiPoint3&,
		const RE::bhkWorld&,
		ConnectPoint::Result&,
		float);

	// Resolves the reviewed eager manifest after F4SE::Init and before Papyrus
	// registration; failure here rejects plugin initialization. RuntimeSymbols
	// separately preflights the reviewed CommonLib dependency closure. Resolution is
	// not validation of the running executable's class layouts or behavior.
	[[nodiscard]] bool ResolveRequiredSymbols();

	[[nodiscard]] RE::TESObjectREFR* PlaceAtMe(
		RE::BSScript::IVirtualMachine* a_vm,
		std::uint32_t a_stackID,
		RE::TESObjectREFR** a_target,
		RE::TESForm* a_form,
		std::int32_t a_count,
		bool a_forcePersist,
		bool a_initiallyDisabled,
		bool a_deleteWhenAble);
	[[nodiscard]] RE::TESObjectREFR* GetLinkedRef(RE::TESObjectREFR* a_target, RE::BGSKeyword* a_keyword);
	[[nodiscard]] std::uint32_t GetLocationReferenceID(RE::TESObjectREFR* a_target);
	void SetLinkedRef(RE::TESObjectREFR* a_target, RE::TESObjectREFR* a_linked, RE::BGSKeyword* a_keyword);
	void MoveRefrToPosition(
		RE::TESObjectREFR* a_source,
		std::uint32_t* a_targetHandle,
		RE::TESObjectCELL* a_parentCell,
		RE::TESWorldSpace* a_worldSpace,
		RE::NiPoint3* a_position,
		RE::NiPoint3* a_rotation);
	void Enable(RE::TESObjectREFR* a_ref, bool a_resetInventory);
	std::uint32_t Disable(RE::TESObjectREFR* a_ref, bool a_fadeOut);
	// Call from the game's delayed-task execution context while retaining a_ref.
	// Runs the original motion body synchronously, including world locking and
	// save-change tracking. True means its prerequisites held and the call
	// completed; the original Papyrus operation does not return physical state.
	[[nodiscard]] bool ApplyKeyframedMotion(
		RE::BSScript::IVirtualMachine* a_vm,
		std::uint32_t a_stackID,
		RE::TESObjectREFR* a_ref,
		bool a_allowActivate = true);
	void PlayEffectShader(RE::TESObjectREFR* a_ref, RE::TESEffectShader* a_shader);
	void StopEffectShader(RE::TESObjectREFR* a_ref, RE::TESEffectShader* a_shader);
	void WorkshopExtraAddItem(RE::Workshop::ExtraData* a_extra, RE::TESObjectREFR* a_ref);
	void WorkshopExtraAddConnection(
		RE::Workshop::ExtraData* a_extra,
		RE::TESObjectREFR* a_ref1,
		RE::TESObjectREFR* a_ref2,
		RE::TESObjectREFR* a_wire);
	void UpdateSpline(
		RE::Workshop::ContextData* a_context,
		RE::TESObjectREFR* a_wire,
		RE::TESObjectREFR* a_endpoint1,
		std::int32_t a_type1,
		RE::TESObjectREFR* a_endpoint2,
		std::int32_t a_type2);
	void UpdateMovingWirelessItem(RE::TESObjectREFR* a_ref, RE::Workshop::ExtraData* a_extra);
	void EstablishTerminalLinks(RE::TESObjectREFR* a_wire);
	void ScrapReference(RE::Workshop::ContextData* a_context, RE::TESObjectREFR* a_ref);
	[[nodiscard]] bool CallFunctionNoWait(
		RE::BSScript::IVirtualMachine* a_vm,
		RE::TESObjectREFR* a_target,
		const RE::BSFixedString& a_function,
		RE::TESObjectREFR* a_argument);
	// The return value reports dispatch acceptance. The callback may complete or
	// cancel synchronously; callers must initialize their state before this call.
	[[nodiscard]] bool DispatchReferenceCallback(
		RE::BSScript::IVirtualMachine* a_vm,
		RE::TESObjectREFR* a_target,
		const RE::BSFixedString& a_function,
		RE::TESObjectREFR* a_argument,
		const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor>& a_callback);
	[[nodiscard]] RE::TESObjectREFR* GetObjectAtConnectPoint(
		const RE::TESObjectREFR& a_source,
		const RE::NiPoint3& a_position,
		const RE::bhkWorld& a_world,
		float a_radius,
		ConnectPoint::Status* a_status = nullptr);
	[[nodiscard]] RE::ObjectRefHandle CurrentWorkshop() noexcept;
	void SetCurrentWorkshop(RE::ObjectRefHandle a_handle) noexcept;
	[[nodiscard]] std::uint32_t InvalidRefHandle() noexcept;
}

// Compatibility spellings used by the reconstructed algorithm. Their bodies
// are Runtime Database backed; no raw executable address remains here.
inline RE::TESObjectREFR* PlaceAtMe_Native(
	RE::BSScript::IVirtualMachine* a_vm,
	std::uint32_t a_stackID,
	RE::TESObjectREFR** a_target,
	RE::TESForm* a_form,
	std::int32_t a_count,
	bool a_forcePersist,
	bool a_initiallyDisabled,
	bool a_deleteWhenAble)
{
	return Clipboard::EngineAPI::PlaceAtMe(
		a_vm, a_stackID, a_target, a_form, a_count,
		a_forcePersist, a_initiallyDisabled, a_deleteWhenAble);
}

inline RE::TESObjectREFR* GetLinkedRef_Native(RE::TESObjectREFR* a_target, RE::BGSKeyword* a_keyword)
{
	return Clipboard::EngineAPI::GetLinkedRef(a_target, a_keyword);
}

inline void SetLinkedRef_Native(
	RE::TESObjectREFR* a_target,
	RE::TESObjectREFR* a_linked,
	RE::BGSKeyword* a_keyword)
{
	Clipboard::EngineAPI::SetLinkedRef(a_target, a_linked, a_keyword);
}

inline void MoveRefrToPosition(
	RE::TESObjectREFR* a_source,
	std::uint32_t* a_targetHandle,
	RE::TESObjectCELL* a_parentCell,
	RE::TESWorldSpace* a_worldSpace,
	RE::NiPoint3* a_position,
	RE::NiPoint3* a_rotation)
{
	Clipboard::EngineAPI::MoveRefrToPosition(
		a_source, a_targetHandle, a_parentCell, a_worldSpace, a_position, a_rotation);
}

inline void Enable_Native(RE::TESObjectREFR* a_ref, bool a_resetInventory)
{
	Clipboard::EngineAPI::Enable(a_ref, a_resetInventory);
}

inline std::uint32_t Disable_Native(RE::TESObjectREFR* a_ref, bool a_fadeOut)
{
	return Clipboard::EngineAPI::Disable(a_ref, a_fadeOut);
}

inline RE::TESObjectREFR* GetObjectAtConnectPoint(
	const RE::TESObjectREFR& a_source,
	RE::NiPoint3& a_position,
	const RE::bhkWorld& a_world,
	float a_radius)
{
	return Clipboard::EngineAPI::GetObjectAtConnectPoint(a_source, a_position, a_world, a_radius);
}
