#include "PCH.h"
#include "LoggingPolicy.h"

#include "EngineAPI.h"
#include "EngineSymbols.h"
#include "MotionTypeABI.h"
#include "PapyrusArgument.h"
#include "PapyrusDispatchABI.h"

// The pinned dependency declares this interface destructor but supplies no
// definition. The reviewed engine base destructor only resets its vptr; the
// compiler-generated default performs the same interface teardown. Derived
// callbacks keep their own deleting destructor and allocator ownership.
// See Docs/Phase4/WORKSHOP_CALLBACK_COMPLETION.md.
RE::BSScript::IStackCallbackFunctor::~IStackCallbackFunctor() = default;

namespace Clipboard::EngineAPI
{
	namespace
	{
		std::array<std::uintptr_t, kRequiredIDs.size()> resolvedAddresses{};

		PlaceAtMe_t placeAtMe{};
		GetLinkedRef_t getLinkedRef{};
		GetLocationReferenceID_t getLocationReferenceID{};
		SetLinkedRef_t setLinkedRef{};
		MoveRefrToPosition_t moveRefrToPosition{};
		Enable_t enable{};
		Disable_t disable{};
		MotionTypeABI::Execute_t executeMotionType{};
		EffectShaderPlay_t effectShaderPlay{};
		EffectShaderStop_t effectShaderStop{};
		WorkshopExtraAddItem_t workshopExtraAddItem{};
		WorkshopExtraAddConnection_t workshopExtraAddConnection{};
		UpdateSpline_t updateSpline{};
		UpdateMovingWirelessItem_t updateMovingWirelessItem{};
		EstablishTerminalLinks_t establishTerminalLinks{};
		ScrapReference_t scrapReference{};
		CallFunctionNoWait_t callFunctionNoWait{};
		GetSnappedReferenceImpl_t getSnappedReferenceImpl{};
		std::uint32_t* invalidRefHandle{};
		RE::ObjectRefHandle* currentWorkshop{};

		template <class T>
		bool ResolveFunction(std::uint64_t a_id, std::string_view a_name, T& a_out)
		{
			const auto* symbol = FindRequiredSymbol(a_id);
			if (!symbol) {
				logger::critical("Unmanifested engine binding {} ({})", a_id, a_name);
				return false;
			}
			const auto index = static_cast<std::size_t>(symbol - kRequiredIDs.data());
			a_out = reinterpret_cast<T>(resolvedAddresses[index]);
			return a_out != nullptr;
		}
	}

	bool ResolveRequiredSymbols()
	{
		bool valid = true;
		resolvedAddresses.fill(0);
		const auto& module = REL::Module::get();
		for (const auto& symbol : kRequiredIDs) {
			const auto result = ResolveRequiredSymbol(symbol);
			if (!result || !result.rva || *result.rva == 0 || *result.rva >= module.image_size()) {
				logger::critical(
					"Required Runtime Database ID {} ({}) failed: {}",
					symbol.key,
					symbol.name,
					REL::id_resolve_status_text(result.status));
				valid = false;
				continue;
			}
			resolvedAddresses[static_cast<std::size_t>(&symbol - kRequiredIDs.data())] = module.base() + *result.rva;
			CLIPBOARD_DEBUG_LOG(logger::info("Engine {}: OG ID {}, NG ID {}, AE ID {}; runtime RVA 0x{:X} ({})",
				symbol.name, symbol.id.og_id(), symbol.id.ng_id(), symbol.id.ae_id(),
				*result.rva, REL::id_resolve_status_text(result.status)));
			if (module.version() == REL::Version{ 1, 11, 240, 0 } && *result.rva != symbol.v240RVA) {
				logger::warn(
					"ID {} ({}) resolved to RVA 0x{:X}; recorded 1.11.240 RVA is 0x{:X}",
					symbol.key,
					symbol.name,
					*result.rva,
					symbol.v240RVA);
			}
		}

		valid &= ResolveFunction(2253499, "ObjectReference.PlaceAtMe", placeAtMe);
		valid &= ResolveFunction(2202683, "ObjectReference.GetLinkedRef", getLinkedRef);
		valid &= ResolveFunction(2199660, "reference XLRL FormID lookup", getLocationReferenceID);
		valid &= ResolveFunction(2202684, "ObjectReference.SetLinkedRef", setLinkedRef);
		valid &= ResolveFunction(2254251, "MoveRefrToPosition", moveRefrToPosition);
		valid &= ResolveFunction(2201150, "ObjectReference.Enable", enable);
		valid &= ResolveFunction(2204302, "ObjectReference.Disable", disable);
		valid &= ResolveFunction(2253642, "SetMotionTypeFunctor synchronous execution", executeMotionType);
		valid &= ResolveFunction(2205201, "EffectShader.Play", effectShaderPlay);
		valid &= ResolveFunction(2234097, "EffectShader.Stop", effectShaderStop);
		valid &= ResolveFunction(2194996, "Workshop extra data AddItem", workshopExtraAddItem);
		valid &= ResolveFunction(2194998, "Workshop extra data AddConnection", workshopExtraAddConnection);
		valid &= ResolveFunction(2195071, "SplineUtils.UpdateSpline", updateSpline);
		valid &= ResolveFunction(2195088, "PowerUtils.UpdateMovingWirelessItem", updateMovingWirelessItem);
		valid &= ResolveFunction(2195102, "TerminalUtils.EstablishTerminalLinks", establishTerminalLinks);
		valid &= ResolveFunction(2195125, "Workshop.ScrapReference", scrapReference);
		valid &= ResolveFunction(2252785, "ScriptObject.CallFunctionNoWait", callFunctionNoWait);
		valid &= ResolveFunction(2195571, "Workshop.GetSnappedReferenceImpl", getSnappedReferenceImpl);
		valid &= ResolveFunction(4795988, "invalid reference handle", invalidRefHandle);
		valid &= ResolveFunction(4797241, "current workshop handle", currentWorkshop);

		if (valid) {
			CLIPBOARD_DEBUG_LOG(logger::info("Resolved all {} required Runtime Database IDs", kRequiredIDs.size()));
		}
		return valid;
	}

	bool ApplyKeyframedMotion(
		RE::BSScript::IVirtualMachine* a_vm,
		std::uint32_t a_stackID,
		RE::TESObjectREFR* a_ref,
		bool a_allowActivate)
	{
		if (!executeMotionType || !a_vm || !a_ref || !a_ref->Get3D() ||
			!a_ref->loadedData || (a_ref->loadedData->flags & 1u) == 0) {
			return false;
		}
		auto handle = a_ref->GetHandle();
		const auto rawHandle = handle.native_handle();
		if (!handle || rawHandle == InvalidRefHandle()) {
			return false;
		}

		MotionTypeABI::CallData callData;
		callData.stackID = a_stackID;
		callData.referenceHandle = rawHandle;
		callData.vm = a_vm;
		callData.allowActivate = a_allowActivate;
		RE::BSScript::Variable result;
		executeMotionType(&callData, &result);
		return true;
	}

	RE::TESObjectREFR* PlaceAtMe(
		RE::BSScript::IVirtualMachine* a_vm,
		std::uint32_t a_stackID,
		RE::TESObjectREFR** a_target,
		RE::TESForm* a_form,
		std::int32_t a_count,
		bool a_forcePersist,
		bool a_initiallyDisabled,
		bool a_deleteWhenAble)
	{
		return placeAtMe ? placeAtMe(
			a_vm, a_stackID, a_target, a_form, a_count,
			a_forcePersist, a_initiallyDisabled, a_deleteWhenAble) : nullptr;
	}

	RE::TESObjectREFR* GetLinkedRef(RE::TESObjectREFR* a_target, RE::BGSKeyword* a_keyword)
	{
		return a_target && a_keyword && getLinkedRef ? getLinkedRef(a_target, a_keyword) : nullptr;
	}

	std::uint32_t GetLocationReferenceID(RE::TESObjectREFR* a_target)
	{
		if (!a_target || !getLocationReferenceID) {
			return 0;
		}
		// FO4 1.11.240: engine caller at RVA 004F38CE obtains GetHandle,
		// calls ID 2199660, resolves the returned FormID, and checks for LCTN.
		// RCX points to the 32-bit handle; EAX returns the XLRL FormID or zero.
		// This is not ExtraLocation (XLCN). See the API inventory for provenance.
		const auto handle = a_target->GetHandle();
		return getLocationReferenceID(handle);
	}

	void SetLinkedRef(RE::TESObjectREFR* a_target, RE::TESObjectREFR* a_linked, RE::BGSKeyword* a_keyword)
	{
		if (a_target && a_keyword && setLinkedRef) {
			setLinkedRef(a_target, a_linked, a_keyword);
		}
	}

	void MoveRefrToPosition(
		RE::TESObjectREFR* a_source,
		std::uint32_t* a_targetHandle,
		RE::TESObjectCELL* a_parentCell,
		RE::TESWorldSpace* a_worldSpace,
		RE::NiPoint3* a_position,
		RE::NiPoint3* a_rotation)
	{
		if (a_source && moveRefrToPosition) {
			moveRefrToPosition(
				a_source, a_targetHandle, a_parentCell, a_worldSpace, a_position, a_rotation);
		}
	}

	void Enable(RE::TESObjectREFR* a_ref, bool a_resetInventory)
	{
		if (a_ref && enable) {
			enable(a_ref, a_resetInventory);
		}
	}

	std::uint32_t Disable(RE::TESObjectREFR* a_ref, bool a_fadeOut)
	{
		return a_ref && disable ? disable(a_ref, a_fadeOut) : 0;
	}

	void PlayEffectShader(RE::TESObjectREFR* a_ref, RE::TESEffectShader* a_shader)
	{
		if (a_ref && a_shader && effectShaderPlay) {
			effectShaderPlay(a_ref, a_shader, -1.0F, nullptr, 0, nullptr, nullptr, 0);
		}
	}

	void StopEffectShader(RE::TESObjectREFR* a_ref, RE::TESEffectShader* a_shader)
	{
		if (a_ref && a_shader && effectShaderStop) {
			if (auto* processLists = RE::ProcessLists::GetSingleton()) {
				effectShaderStop(processLists, a_ref, a_shader);
			}
		}
	}

	void WorkshopExtraAddItem(RE::Workshop::ExtraData* a_extra, RE::TESObjectREFR* a_ref)
	{
		if (a_extra && a_ref && workshopExtraAddItem) {
			workshopExtraAddItem(a_extra, a_ref);
		}
	}

	void WorkshopExtraAddConnection(
		RE::Workshop::ExtraData* a_extra,
		RE::TESObjectREFR* a_ref1,
		RE::TESObjectREFR* a_ref2,
		RE::TESObjectREFR* a_wire)
	{
		if (a_extra && a_ref1 && a_ref2 && workshopExtraAddConnection) {
			workshopExtraAddConnection(a_extra, a_ref1, a_ref2, a_wire);
		}
	}

	void UpdateSpline(
		RE::Workshop::ContextData* a_context,
		RE::TESObjectREFR* a_wire,
		RE::TESObjectREFR* a_endpoint1,
		std::int32_t a_type1,
		RE::TESObjectREFR* a_endpoint2,
		std::int32_t a_type2)
	{
		if (a_context && a_wire && a_endpoint1 && a_endpoint2 && updateSpline) {
			updateSpline(a_context, a_wire, a_endpoint1, a_type1, a_endpoint2, a_type2);
		}
	}

	void UpdateMovingWirelessItem(RE::TESObjectREFR* a_ref, RE::Workshop::ExtraData* a_extra)
	{
		if (a_ref && a_extra && updateMovingWirelessItem) {
			updateMovingWirelessItem(a_ref, a_extra);
		}
	}

	void EstablishTerminalLinks(RE::TESObjectREFR* a_wire)
	{
		if (a_wire && establishTerminalLinks) {
			establishTerminalLinks(a_wire);
		}
	}

	void ScrapReference(RE::Workshop::ContextData* a_context, RE::TESObjectREFR* a_ref)
	{
		if (a_context && a_ref && scrapReference) {
			RE::NiPointer<RE::TESObjectREFR> smartRef{ a_ref };
			scrapReference(a_context, std::addressof(smartRef), nullptr);
		}
	}

	bool CallFunctionNoWait(
		RE::BSScript::IVirtualMachine* a_vm,
		RE::TESObjectREFR* a_target,
		const RE::BSFixedString& a_function,
		RE::TESObjectREFR* a_argument)
	{
		if (!a_vm || !a_target || a_function.empty() || !callFunctionNoWait) {
			return false;
		}

		RE::BSScript::Variable targetValue;
		RE::BSScript::PackVariable(targetValue, a_target);
		if (!targetValue.is<RE::BSScript::Object>()) {
			return false;
		}
		auto targetObject = RE::BSScript::get<RE::BSScript::Object>(targetValue);
		if (!targetObject) {
			return false;
		}

		RE::BSScript::Variable argumentValue;
		RE::BSScript::PackVariable(argumentValue, a_argument);
		if (a_argument) {
			RE::BSTSmartPointer<RE::BSScript::ObjectTypeInfo> referenceType;
			if (!argumentValue.is<RE::BSScript::Object>() ||
				!RE::BSScript::get<RE::BSScript::Object>(argumentValue) ||
				!a_vm->GetScriptObjectType(RE::BSScript::GetVMTypeID<RE::TESObjectREFR>(), referenceType) ||
				!referenceType ||
				!PapyrusArgument::RestoreDeclaredObjectType(argumentValue, referenceType.get())) {
				logger::error("Cannot pack ObjectReference argument for {} on {:08X}", a_function.c_str(), a_target->formID);
				return false;
			}
		}
		RE::BSTSmartPointer<RE::BSScript::Array> parameters;
		RE::BSScript::TypeInfo arrayType{ RE::BSScript::TypeInfo::RawType::kVar };
		arrayType.SetArray(true);
		if (!a_vm->CreateArray(arrayType, 1, parameters) || !parameters) {
			return false;
		}

		// ScriptObject.CallFunctionNoWait takes a Var[] rather than an array of
		// the argument's concrete type.  A Var array owns one heap-allocated
		// Variable wrapper per element, matching the VMVariable representation
		// used by the v221 F4SE helper.
		parameters->elements[0] = new RE::BSScript::Variable(std::move(argumentValue));
		RE::BSScript::Variable packedParameters;
		packedParameters = std::move(parameters);
		callFunctionNoWait(
			a_vm,
			0,
			targetObject.get(),
			std::addressof(a_function),
			std::addressof(packedParameters));
		return true;
	}

	bool DispatchReferenceCallback(
		RE::BSScript::IVirtualMachine* a_vm,
		RE::TESObjectREFR* a_target,
		const RE::BSFixedString& a_function,
		RE::TESObjectREFR* a_argument,
		const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor>& a_callback)
	{
		if (!a_vm || !a_target || a_function.empty() || !a_callback) {
			return false;
		}

		RE::BSScript::Variable targetValue;
		RE::BSScript::PackVariable(targetValue, a_target);
		if (!targetValue.is<RE::BSScript::Object>()) {
			return false;
		}
		const auto targetObject = RE::BSScript::get<RE::BSScript::Object>(targetValue);
		if (!targetObject) {
			return false;
		}

		RE::BSScript::Variable argumentValue;
		RE::BSScript::PackVariable(argumentValue, a_argument);
		if (a_argument) {
			RE::BSTSmartPointer<RE::BSScript::ObjectTypeInfo> referenceType;
			if (!argumentValue.is<RE::BSScript::Object>() ||
				!RE::BSScript::get<RE::BSScript::Object>(argumentValue) ||
				!a_vm->GetScriptObjectType(RE::BSScript::GetVMTypeID<RE::TESObjectREFR>(), referenceType) ||
				!referenceType ||
				!PapyrusArgument::RestoreDeclaredObjectType(argumentValue, referenceType.get())) {
				return false;
			}
		}

		using Arguments = RE::BSScrapArray<RE::BSScript::Variable>;
		using Signature = bool(Arguments&);
		// The builder owns a Variable copy. Dispatch consumes the builder before
		// returning and copies its produced Variables into the queued call; no
		// caller-local reference or borrowed argument array survives this scope.
		PapyrusDispatchABI::BorrowedFunction<Signature> builder{
			std::function<Signature>{ [value = argumentValue](Arguments& a_values) {
				try {
					a_values.clear();
					a_values.emplace_back(value);
					return true;
				} catch (...) {
					// The VM may hold internal locks while invoking this builder.
					// Report rejection through its normal false/canceled path.
					a_values.clear();
					return false;
				}
			} }
		};
		const bool originalGeneration = REL::Module::get().version() < REL::Version{ 1, 10, 980, 0 };
		// CommonLib's declaration is an opaque reference at this ABI boundary.
		// Its current msvc::function view describes OG only; the borrowed storage
		// selects the exact engine callable offset for the current family.
		const auto& arguments = *static_cast<const RE::BSTThreadScrapFunction<Signature>*>(builder.View(originalGeneration));
		return a_vm->DispatchMethodCall(targetObject, a_function, arguments, a_callback);
	}

	RE::TESObjectREFR* GetObjectAtConnectPoint(
		const RE::TESObjectREFR& a_source,
		const RE::NiPoint3& a_position,
		const RE::bhkWorld& a_world,
		float a_radius,
		ConnectPoint::Status* a_status)
	{
		if (a_status) {
			*a_status = ConnectPoint::Status::kCount;
		}
		if (!getSnappedReferenceImpl) {
			return nullptr;
		}
		ConnectPoint::Result result;
		auto* reference = getSnappedReferenceImpl(a_source, a_position, a_world, result, a_radius);
		if (a_status) {
			*a_status = result.status;
		}
		return reference;
	}

	RE::ObjectRefHandle CurrentWorkshop() noexcept
	{
		return currentWorkshop ? *currentWorkshop : RE::ObjectRefHandle{};
	}

	void SetCurrentWorkshop(RE::ObjectRefHandle a_handle) noexcept
	{
		if (currentWorkshop) {
			*currentWorkshop = a_handle;
		}
	}

	std::uint32_t InvalidRefHandle() noexcept
	{
		return invalidRefHandle ? *invalidRefHandle : 0;
	}
}
