// SPDX-License-Identifier: GPL-3.0-or-later
#include "ImportJobState.h"
#include "ImportRowResultsTests.h"
#include "PapyrusDispatchABI.h"
#include "WorkshopCallbackState.h"
#include "WorkshopNoOpPolicy.h"
#include "SerializationFactoryChecks.h"
#include "SerializationValues.h"
#include "LegacyCleanupGate.h"
#include "PerformanceBatchTests.h"
#include "PowerTaskStateTests.h"
#include "EnginePowerFunctorTests.h"
#include "WorkshopProgressTests.h"
#include "WorkshopCallbackSliceTests.h"
#include "WorkshopThrottleTests.h"
#include "WorkshopWaitSnapshotsTests.h"
#include "WorkshopSingleCallbackTests.h"
#include "WorkshopCallbackSpacingTests.h"
#include "WorkshopCallbackLimitTests.h"
#include "WorkshopCallbackDrainTests.h"
#include "ImportDuplicatesTests.h"
#include "ExistingPowerEndpointsTests.h"
#include "ResourceStreamABI.h"

#include <cstring>
#include <iostream>
#include <limits>
#include <memory>

namespace
{
	template <class Check>
	void CheckLegacyCleanupGate(Check&& check)
	{
		using Clipboard::LegacyCleanup::Gate;
		using Clipboard::LegacyCleanup::WorkTicket;
		Gate gate;
		const auto first = gate.Begin();
		check(first > 0 && gate.Active(first) && gate.Active(), "idle cleanup obtains exclusive token");
		check(gate.Begin() == 0, "another cleanup cannot overlap");
		WorkTicket metadataSample;
		check(gate.Active(first), "factory class-name samples do not acquire work tickets");
		gate.End(first + 1);
		gate.End(0);
		check(gate.Active(first), "stale and zero tokens cannot end current cleanup");
		bool rejected{};
		try { WorkTicket denied{ gate }; } catch (const std::runtime_error&) { rejected = true; }
		check(rejected && gate.Active(first), "cleanup rejects allocation of new latent work before enqueue");
		gate.End(first);
		metadataSample.Start(gate);
		check(gate.Begin() == 0, "restored work acquires a ticket before its payload is resolved");
		metadataSample.Complete();
		{
			WorkTicket queued{ gate };
			check(gate.Begin() == 0, "queued work blocks cleanup before its first execution");
		}
		auto job = std::make_unique<WorkTicket>(gate);
		auto callback = std::make_unique<WorkTicket>(gate);
		job.reset();
		check(gate.Begin() == 0, "callback keeps cleanup blocked after owning job timeout or cancellation");
		callback->Complete();
		callback->Complete();
		callback.reset();
		const auto second = gate.Begin();
		check(second > first, "actual callback return permits a new unique cleanup token");
		gate.ResetForLoad();
		check(!gate.Active(second), "loading a save invalidates suspended cleanup token");
		const auto third = gate.Begin();
		gate.End(second);
		check(third > second && gate.Active(third), "old stack cannot finish the replacement session");
		gate.End(third);
		gate.MarkUncertain();
		check(gate.Begin() == 0, "lost restored callback signals fail closed after functor retirement");
		{
			WorkTicket stillRunning{ gate };
			gate.ResetForLoad();
			check(gate.Begin() == 0, "load reset never erases the count of a still-owned job");
		}
		check(gate.Begin() > third, "load reset releases uncertainty only after surviving work ends");
		Gate exhausted{ std::numeric_limits<std::int32_t>::max() };
		check(exhausted.Begin() == 0, "token exhaustion fails closed instead of reusing a saved token");
	}

	struct Bytes
	{
		std::vector<std::byte> data;
		std::vector<std::size_t> fields;
		std::size_t cursor{};
		template <class T> bool Write(const T& value)
		{
			fields.push_back(data.size());
			const auto* first = reinterpret_cast<const std::byte*>(&value);
			data.insert(data.end(), first, first + sizeof(T));
			return true;
		}
		template <class T> bool Read(T& value)
		{
			if (cursor > data.size() || sizeof(T) > data.size() - cursor) { return false; }
			std::memcpy(&value, data.data() + cursor, sizeof(T));
			cursor += sizeof(T);
			return true;
		}
	};

	template <class Check>
	void CheckSerializedBooleans(Check&& check)
	{
		using Clipboard::SerializationValues::ReadBool;
		for (std::uint16_t value = 0; value < 256; ++value) {
			for (const bool initial : { false, true }) {
				Bytes saved;
				saved.Write(static_cast<std::uint8_t>(value));
				bool result = initial;
				const bool accepted = ReadBool([&](std::uint8_t& byte) { return saved.Read(byte); }, result);
				check(accepted == (value <= 1) && result == (value <= 1 ? value != 0 : initial) && saved.cursor == 1,
					"serialized bool accepts only 0/1 bytes and leaves rejected destinations unchanged");
			}
		}
		for (const bool initial : { false, true }) {
			Bytes truncated;
			bool result = initial;
			check(!ReadBool([&](std::uint8_t& byte) { return truncated.Read(byte); }, result) &&
				result == initial && truncated.cursor == 0,
				"truncated serialized bool does not replace its destination");
			check(!ReadBool([](std::uint8_t& byte) { byte = 255; return false; }, result) && result == initial,
				"failed byte reader cannot write an invalid representation into a bool");
			Bytes historical;
			historical.Write(initial);
			check(historical.data.size() == 1 && historical.data.front() == (initial ? std::byte{ 1 } : std::byte{ 0 }),
				"historical bool writer retains its canonical one-byte payload");
			result = !initial;
			check(ReadBool([&](std::uint8_t& byte) { return historical.Read(byte); }, result) && result == initial,
				"canonical historical bool payloads still load without migration");
		}
	}

	template <class Check>
	void CheckWorkshopCallbacks(Check&& check)
	{
		using namespace Clipboard::WorkshopCallbacks;
		for (const auto count : { 0u, 127u, 128u, 129u, 1194u }) {
			State state(count);
			std::vector<std::uint8_t> phases(count);
			bool ordered = true;
			while (const auto token = state.Reserve(0)) {
				ordered &= token->row < count && static_cast<std::uint8_t>(token->phase) == phases[token->row];
				ordered &= state.Accept(*token) && state.Complete(*token);
				++phases[token->row];
			}
			check(ordered && std::all_of(phases.begin(), phases.end(), [](auto phase) { return phase == 4; }),
				"every physical row receives placed/workshop, placed/object, moved/workshop, moved/object in order");
			check(state.done && state.Valid() && state.Summary()[0] == 1 && state.completedRows == count &&
				state.planned == count * 4 && state.dispatched == state.planned && state.returned == state.planned && state.outstanding == 0,
				"callback completion accounts for every row beyond the Papyrus array limit");
			Bytes saved;
			check(state.Save([&](const auto& value) { return saved.Write(value); }), "save a completed callback job");
			State restored;
			check(restored.Load([&](auto& value) { return saved.Read(value); }) && restored.Summary() == state.Summary() &&
				!restored.Reserve(1) && saved.cursor == saved.data.size(), "completed callback load preserves its result and never replays");
		}
		{
			State state(129, SchedulingPolicy::Baseline);
			std::vector<Token> held;
			while (const auto token = state.Reserve(100)) {
				check(state.Accept(*token), "accept reserved callback");
				held.push_back(*token);
			}
			check(held.size() == 4 && state.outstanding == 4 && state.peakOutstanding == 4 && !state.done && state.Summary()[0] == 0,
				"withheld callbacks cap dispatch at four and cannot report completion");
			check(state.Complete(held.back()), "callbacks may return out of row order");
			check(!state.Complete(held.back()), "duplicate completion cannot release a second slot");
			const auto next = state.Reserve(101);
			check(next && next->row == 4 && state.Accept(*next) && state.outstanding == 4 && !state.Reserve(101),
				"one return permits exactly one more row while three other calls remain outstanding");
			check(!state.Complete(held.back()) && !state.Accept(held.back()) && !state.Reject(held.back()) && state.Valid(),
				"a stale token cannot acknowledge a reused slot");
		}
		{
			State state(1);
			const auto token = state.Reserve(0).value();
			CompletionSignal signal;
			signal.Resolve(Completion::Returned); // The engine may invoke its callback during dispatch.
			signal.Resolve(Completion::Cancelled);
			check(signal.Get() == Completion::Returned && !state.Complete(token),
				"reentrant return is retained without completing an unaccepted reservation");
			check(state.Accept(token) && state.Complete(token) && state.dispatched == 1 && state.returned == 1 &&
				state.outstanding == 0 && state.rows[0].phase == Phase::ObjectPlaced && state.Valid(),
				"accept then consume a synchronous return exactly once");
			const auto next = state.Reserve(1);
			check(next && next->phase == Phase::ObjectPlaced && !state.Complete(token),
				"the next phase starts only after its previous callback returned");
		}
		{
			State state(1);
			const auto token = state.Reserve(0).value();
			CompletionSignal signal;
			signal.Resolve(Completion::Cancelled);
			signal.Resolve(Completion::Returned);
			check(signal.Get() == Completion::Cancelled && state.Reject(token) && !state.Complete(token, true) &&
				!state.Reject(token) && state.dispatched == 0 && state.failed == 1 && state.outstanding == 0 && state.done &&
				state.Summary()[0] == 0 && state.Valid(), "immediate dispatch rejection and cancellation signal count one failed row");
		}
		{
			State state(1);
			const auto token = state.Reserve(0).value();
			check(state.Accept(token) && state.Complete(token, true) && !state.Complete(token) && state.failed == 1 &&
				state.dispatched == 1 && state.returned == 0 && state.done && state.Valid(),
				"a cancelled accepted callback fails its row without pretending it returned");
		}
		{
			State state(3);
			check(state.SkipRow(0) && state.SkipRow(2) && !state.SkipRow(0) && !state.SkipRow(3),
				"missing rows are skipped once without changing physical indices");
			const auto token = state.Reserve(0).value();
			check(token.row == 1 && !state.SkipRow(1) && state.planned == 4 && state.skippedRows == 2 && state.Valid(),
				"a live middle row retains its index and active rows cannot be skipped");
		}
		{
			State state(1);
			check(state.SkipRow(0) && state.done && state.Summary()[0] == 1 && state.planned == 0 && state.Valid(),
				"an entirely missing selection finishes with zero planned calls");
		}
		for (const bool dispatchFirst : { false, true }) {
			State state(2);
			std::optional<Token> token;
			if (dispatchFirst) { token = state.Reserve(10); check(state.Accept(*token), "accept callback before cancelling its job"); }
			state.Cancel();
			check(state.done && state.cancelled && state.Summary()[0] == -1 && !state.Reserve(20) && state.Valid(),
				"cancellation stops dispatch before or during active work");
			if (token) {
				check(!state.Complete(*token) && state.outstanding == 1 && state.returned == 0,
					"late returns cannot change the cancelled result or erase uncertain outstanding work");
			}
		}
		{
			State state(1);
			const auto token = state.Reserve(100).value();
			check(state.Accept(token), "accept callback before timeout");
			state.Tick(kCallTimeoutMs + 99);
			check(!state.done && !state.timedOut && state.outstanding == 1, "the callback deadline is measured from dispatch");
			state.Tick(kCallTimeoutMs + 100);
			check(state.done && state.timedOut && state.Summary()[0] == 0 && state.outstanding == 1 &&
				!state.Reserve(kCallTimeoutMs + 101) && !state.Complete(token) && state.Valid(),
				"five minutes without return stops dispatch with an explicit incomplete result");
		}
		for (const bool accepted : { false, true }) {
			State state(3);
			const auto token = state.Reserve(10).value();
			if (accepted) { check(state.Accept(token), "accept callback before saving unfinished work"); }
			Bytes saved;
			check(state.Save([&](const auto& value) { return saved.Write(value); }), "save reserved or accepted callback ownership");
			State restored;
			check(restored.Load([&](auto& value) { return saved.Read(value); }) && restored.done && restored.interrupted &&
				restored.Summary()[0] == -1 && restored.outstanding == 1 && restored.dispatched == state.dispatched &&
				!restored.Reserve(11) && !restored.Complete(token) && restored.Valid(),
				"loading unfinished callbacks reports interruption without replaying or inventing returns");
		}
		{
			State state(1);
			const auto token = state.Reserve(10).value();
			check(state.Accept(token), "accept callback for corrupt-save fixtures");
			Bytes saved;
			check(state.Save([&](const auto& value) { return saved.Write(value); }), "create callback serialization fixture");
			const auto reject = [&](Bytes corrupt, const char* message) {
				State previous(7);
				check(!previous.Load([&](auto& value) { return corrupt.Read(value); }), message);
				check(previous.rows.size() == 7 && previous.planned == 28 && !previous.done,
					"failed callback load does not replace the prior live job");
			};
			const auto overwrite = [&](std::size_t field, const auto& value, const char* message) {
				auto corrupt = saved;
				// v4 adds mode and capacity before the preserved legacy fields.
				std::memcpy(corrupt.data.data() + corrupt.fields[field + 2], &value, sizeof(value));
				reject(std::move(corrupt), message);
			};
			overwrite(0, kMaximumRows + 1, "reject excessive callback row count before allocation");
			overwrite(1, std::uint32_t{ 3 }, "reject a planned-call count inconsistent with physical rows");
			overwrite(5, std::uint32_t{ 9 }, "reject callback ownership beyond the dispatch cap");
			overwrite(11, std::uint64_t{ 0 }, "reject invalid token generation");
			overwrite(12, std::uint8_t{ 2 }, "reject a malformed saved completion boolean");
			overwrite(16, std::uint8_t{ 99 }, "reject an unknown saved callback phase");
			overwrite(22, std::uint32_t{ 1 }, "reject an active token outside physical rows");
			saved.data.pop_back();
			reject(std::move(saved), "reject a truncated callback slot record");
		}
	}

	template <class Check>
	void CheckWorkshopNoOps(Check&& check)
	{
		using namespace Clipboard::WorkshopCallbacks;
		using namespace Clipboard::WorkshopNoOp;
		{
			struct Receiver { ReceiverKind kind{}; bool valid{ true }; const Receiver* parent{}; };
			Receiver workshop{ ReceiverKind::Workshop }, vault{ ReceiverKind::Vault, true, &workshop };
			Receiver replacement{ ReceiverKind::Unknown, true, &workshop }, grandchild{ ReceiverKind::Unknown, true, &vault };
			const auto audited = [&](const Receiver* receiver, std::string_view sha) {
				return AuditedWorkshopType(receiver, sha, [](const auto* type) { return type->kind; },
					[](const auto* type) { return type->parent; }, [](const auto* type) { return type->valid; });
			};
			check(audited(&workshop, "") == &workshop, "existing exact workshop receiver does not depend on Vault DLC availability");
			check(audited(&vault, kVaultWorkshopSHA) == &workshop,
				"audited Vault receiver selects the base declaring WorkshopParent, not the derived property table");
			check(!audited(&vault, "") && !audited(&vault, "changed"), "missing or replaced winning Vault resource keeps all workshop callbacks");
			check(!audited(&replacement, kVaultWorkshopSHA) && !audited(&grandchild, kVaultWorkshopSHA),
				"unknown receivers and subclasses of the audited Vault script cannot inherit omission permission");
			vault.parent = &replacement;
			check(!audited(&vault, kVaultWorkshopSHA), "an unexpected Vault parent keeps the original callback path");
			vault.parent = nullptr;
			check(!audited(&vault, kVaultWorkshopSHA), "unlinked Vault ancestry cannot authorize omission");
			vault.parent = &workshop; workshop.valid = false;
			check(!audited(&vault, kVaultWorkshopSHA) && !audited(&workshop, kVaultWorkshopSHA), "invalid parent and base types cannot authorize omission");
			workshop.valid = true; vault.valid = false;
			check(!audited(&vault, kVaultWorkshopSHA) && !audited(nullptr, kVaultWorkshopSHA), "unready or missing receiver retains all callbacks");
		}
		{
			using namespace Clipboard::ResourceStreamABI;
			static unsigned constructed{}, destroyed{}, reads{}, infoCalls{};
			static bool opens{};
			constructed = destroyed = reads = infoCalls = 0;
			Operations ops{
				[](void* data, const char*, bool writable, void* location, bool fullRead) -> void* {
					++constructed;
					const std::uintptr_t handle = opens && !writable && !location && !fullRead ? 1 : 0;
					std::memcpy(static_cast<std::byte*>(data) + 0x10, &handle, sizeof(handle));
					return data;
				},
				[](void*) { ++destroyed; },
				[](void*, BufferInfo& info) { ++infoCalls; info.fileSize = 1234; },
				[](void*, void*, std::size_t bytes) { ++reads; return bytes; }
			};
			{
				opens = false;
				Stream missing(ops, "missing.pex");
				check(!missing.Good() && missing.Size() == 0 && missing.Read(nullptr, 10) == 0 && !reads && !infoCalls,
					"unavailable resource never enters engine read with a null stream");
			}
			try {
				opens = true;
				Stream available(ops, "known.pex");
				check(available.Good() && available.Size() == 1234 && available.Read(nullptr, 12) == 12,
					"resource wrapper delegates size and reads without owning the engine stream member");
				throw 1;
			} catch (int) {}
			check(constructed == 2 && destroyed == 2 && reads == 1 && infoCalls == 1,
				"engine destruction occurs exactly once on unavailable and exceptional resource paths");
		}
		struct Type { bool workshop{}, valid{ true }; Type* parent{}; };
		struct Object { Type* type{}; bool ready{ true }; };
		Type base, workshop{ true, true, &base }, derived{ false, true, &workshop }, custom{ false, true, &base };
		Object plain{ &base }, workshopObject{ &derived }, customObject{ &custom }, broken{ nullptr }, unfinished{ &base, false };
		std::vector<Object*> attachments;
		const auto classify = [&] { return ClassifyAttachments(attachments, [](auto* o) { return o; },
			[](const auto* o) { return o->ready; }, [](const auto* o) { return o->type; },
			[](const auto* t) { return t->valid; }, [](const auto* t) { return t->parent; }, [](const auto* t) { return t->workshop; }); };
		check(classify() == Kind::Unknown, "missing binding does not prove plain-reference capability");
		attachments = { &plain, &customObject };
		check(classify() == Kind::Plain, "non-workshop custom handlers remain plain for workshop-side classification only");
		attachments.push_back(&workshopObject);
		check(classify() == Kind::WorkshopObject, "fresh dynamic attachment and inherited workshop capability defeat omission");
		attachments = { &plain, &broken };
		check(classify() == Kind::Unknown, "one broken attachment prevents a negative workshop capability result");
		attachments = { &unfinished };
		check(classify() == Kind::Unknown, "uninitialized binding retains normal dispatch");
		attachments = { &customObject };
		custom.parent = &custom;
		check(classify() == Kind::Unknown, "cyclic ancestry is bounded and cannot authorize omission");
		custom.parent = &base;
		for (const auto kind : { Kind::Unknown, Kind::Plain, Kind::WorkshopObject, Kind::Actor }) {
			check(!CanOmit(kind, false) && CanOmit(kind, true) == (kind == Kind::Plain),
				"unknown receiver, actor and real workshop references retain workshop callbacks");
		}
		check(!WorkshopRevision("") && !ParentRevision("unknown") &&
			WorkshopRevision("3A59DD9F4CE673949360E669A63119923B1F204BE345DD1890031E232DF1468C") == 2411 &&
			ParentRevision("DF5F7E81B86181550C22E3835D5C0DCDBB1C2736C8FCA7386D96F2160A911AD3") == 260,
			"only audited full-resource identities pass the compatibility gate");
		for (unsigned omitMask : { 0u, 1u, 4u, 5u }) {
			State state(1, SchedulingPolicy::ContinuationsFirst, true, kLegacyOutstanding);
			while (const auto token = state.Reserve(10)) {
				if (omitMask & (1u << static_cast<unsigned>(token->phase))) {
					check(state.Omit(*token) && !state.Omit(*token) && !state.Complete(*token) && state.Valid(),
						"omitted workshop phase advances once without inventing a callback or accepting a stale token");
				} else {
					if (token->phase == Phase::ObjectPlaced || token->phase == Phase::ObjectMoved) {
						check(!state.Omit(*token), "object-side handlers cannot be omitted by the workshop fast path");
					}
					check(state.Accept(*token) && !state.Omit(*token) && state.Complete(*token), "accepted calls still require actual returns");
				}
			}
			check(state.Valid() && state.Summary()[0] == 1 && state.returned == 4u - std::popcount(omitMask) &&
				state.planned == state.returned && state.omittedCalls == static_cast<unsigned>(std::popcount(omitMask)) && state.completedRows == 1,
				"ten-field Papyrus summary counts actual required calls and still completes every physical row");
			Bytes bytes;
			check(state.Save([&](const auto& value) { return bytes.Write(value); }, 2), "save v2 omitted-phase mask");
			State restored;
			check(restored.Load([&](auto& value) { return bytes.Read(value); }, 2) && restored.Valid() &&
				restored.omittedCalls == state.omittedCalls && restored.Summary() == state.Summary() && bytes.cursor == bytes.data.size(),
				"v2 round trip preserves distinct omission and real-return accounting");
			bytes.cursor = 0;
			bytes.data.back() = std::byte{ 0x02 };
			check(!restored.Load([&](auto& value) { return bytes.Read(value); }, 2), "corrupt object-handler omission mask is rejected");
		}
		for (bool legacy : { false, true }) {
			State state(2, SchedulingPolicy::ContinuationsFirst, true, kLegacyOutstanding);
			if (!legacy) { check(state.Omit(state.Reserve(0).value()), "omit a verified phase before saving uncertain work"); }
			const auto token = state.Reserve(1).value();
			check(state.Accept(token), "accept callback in a migration fixture");
			Bytes bytes;
			check(state.Save([&](const auto& value) { return bytes.Write(value); }, legacy ? 1 : 2), "save unfinished callback fixture");
			State restored;
			check(restored.Load([&](auto& value) { return bytes.Read(value); }, legacy ? 1 : 2) && restored.interrupted &&
				restored.done && restored.outstanding == 1 && !restored.Reserve(2) && bytes.cursor == bytes.data.size(),
				"v1 and v2 unfinished loads retain uncertain calls and never replay skipped or dispatched phases");
		}
		State mixed(1327);
		std::vector<Token> held;
		std::vector<unsigned> nextPhase(1327);
		bool ordered = true;
		std::uint64_t now{};
		while (!mixed.done) {
			while (const auto token = mixed.Reserve(now)) {
				ordered &= nextPhase[token->row] == static_cast<unsigned>(token->phase);
				if (token->row % 3 == 0 && (token->phase == Phase::WorkshopPlaced || token->phase == Phase::WorkshopMoved)) {
					ordered &= mixed.Omit(*token);
					++nextPhase[token->row];
				} else { ordered &= mixed.Accept(*token); held.push_back(*token); }
			}
			if (held.empty()) { break; }
			const auto token = held.back();
			held.pop_back();
			ordered &= mixed.Complete(token);
			++nextPhase[token.row];
			++now;
		}
		check(ordered && mixed.done && mixed.Valid() && mixed.completedRows == 1327 && mixed.omittedCalls == 886 &&
			mixed.returned == 4422 && mixed.peakOutstanding == 4 && held.empty(),
			"mixed large import keeps per-row ordering, four real slots and exact reduced-call completion");
	}

	template <class Check>
	void CheckWorkshopAdmission(Check&& check)
	{
		using namespace Clipboard::WorkshopCallbacks;
		{
			State state(7, SchedulingPolicy::ContinuationsFirst, true, kLegacyOutstanding);
			std::vector<Token> placed;
			while (const auto token = state.Reserve(0)) { placed.push_back(*token); }
			check(placed.size() == 4 && state.outstanding == 4 && state.Valid() &&
				std::all_of(placed.begin(), placed.end(), [](const auto& token) { return token.phase == Phase::WorkshopPlaced; }),
				"four unaccepted workshop placed reservations use the full direct-call capacity");
			check(!state.Reserve(0) && state.Reject(placed[0]), "a fifth placed call waits for a slot and rejection releases one");
			const auto replacement = state.Reserve(1).value();
			check(replacement.row == 4 && state.Accept(replacement) && state.Complete(replacement, true),
				"a new placed row uses a released slot while three placed calls remain pending");
			check(state.Accept(placed[2]) && state.Complete(placed[2]), "an out-of-order placed return enables its continuation");
			const auto object = state.Reserve(2).value();
			check(object.row == 2 && object.phase == Phase::ObjectPlaced && state.Accept(object) && state.Complete(object),
				"a returned-row continuation precedes untouched rows without inventing failed-row phases");
			const auto moved = state.Reserve(3).value();
			check(moved.row == 2 && moved.phase == Phase::WorkshopMoved && state.Accept(moved) && state.Valid(),
				"synchronous object completion preserves the next ordered continuation");
		}
		{
			State state(8, SchedulingPolicy::ContinuationsFirst, true, kLegacyOutstanding);
			std::vector<Token> objects;
			for (std::uint32_t row = 0; row < 3; ++row) {
				const auto placed = state.Reserve(row).value();
				check(placed.row == row && placed.phase == Phase::WorkshopPlaced && state.Accept(placed) && state.Complete(placed),
					"a returned workshop placed call enables its own object phase");
				const auto object = state.Reserve(row).value();
				check(object.row == row && object.phase == Phase::ObjectPlaced && state.Accept(object),
					"eligible object phases precede untouched rows and may stay outstanding");
				objects.push_back(object);
			}
			const auto heldPlaced = state.Reserve(3).value();
			check(heldPlaced.row == 3 && state.Accept(heldPlaced) && state.outstanding == 4 && !state.Reserve(3),
				"one placed and three object calls can use all four slots without exceeding the total cap");
			check(state.Complete(objects[1]), "an object may return ahead of an earlier physical row");
			const auto moved = state.Reserve(4).value();
			check(moved.row == 1 && moved.phase == Phase::WorkshopMoved && state.Accept(moved) && state.outstanding == 4,
				"workshop moved receives the freed slot ahead of untouched rows");
			check(!state.Complete(objects[1]) && !state.Accept(objects[1]) && !state.Reject(objects[1]),
				"a stale object token cannot release the reused moved slot");
			check(state.Complete(moved), "workshop moved completion enables only its own object moved phase");
			const auto objectMoved = state.Reserve(5).value();
			check(objectMoved.row == 1 && objectMoved.phase == Phase::ObjectMoved && state.Accept(objectMoved),
				"object moved also receives priority over untouched rows");
			check(state.Complete(objectMoved) && state.completedRows == 1 && !state.done && state.Valid(),
				"completing one row leaves the remaining import active");
			const auto nextPlaced = state.Reserve(6).value();
			check(nextPlaced.row == 4 && nextPlaced.phase == Phase::WorkshopPlaced && state.Accept(nextPlaced) && state.outstanding == 4,
				"an empty continuation queue fills spare capacity with another placed call");
			check(state.Complete(heldPlaced), "an actual placed return opens its continuation");
			const auto nextObject = state.Reserve(7).value();
			check(nextObject.row == 3 && nextObject.phase == Phase::ObjectPlaced && state.Accept(nextObject),
				"returned-row continuation is admitted before another untouched row");
			check(!state.Reserve(8) && state.outstanding == 4 && state.Valid(),
				"mixed placed and object work still respects the four-call ceiling");
		}
		{
			// Exercise asynchronous, out-of-order returns with many more rows than
			// slots. The observed row phases and ownership counts form the oracle;
			// no scheduling queue internals are inspected by the test.
			constexpr std::uint32_t count = 1327;
			State state(count);
			std::vector<std::uint8_t> phases(count);
			std::vector<Token> held;
			std::uint64_t now{};
			std::uint32_t completions{}, random = 0x61C88647;
			bool ordered = true, bounded = true, filled = true;
			std::size_t maximumPlaced{};
			while (!state.done && completions <= count * 4) {
				while (const auto token = state.Reserve(now)) {
					ordered &= static_cast<std::uint8_t>(token->phase) == phases[token->row] && state.Accept(*token);
					held.push_back(*token);
					const auto placedCount = std::count_if(held.begin(), held.end(), [](const auto& active) {
						return active.phase == Phase::WorkshopPlaced;
					});
					maximumPlaced = std::max(maximumPlaced, static_cast<std::size_t>(placedCount));
					bounded &= held.size() <= kDefaultOutstanding;
				}
				filled &= held.size() == kDefaultOutstanding || held.size() == static_cast<std::size_t>(
					std::count_if(phases.begin(), phases.end(), [](auto phase) { return phase < 4; }));
				if (held.empty()) { break; }
				random = random * 1664525u + 1013904223u;
				const auto selected = static_cast<std::size_t>(random) % held.size();
				const auto token = held[selected];
				held.erase(held.begin() + static_cast<std::ptrdiff_t>(selected));
				ordered &= state.Complete(token);
				++phases[token.row];
				++completions;
				++now;
			}
			check(ordered && bounded && filled && maximumPlaced == 4 && state.done && state.Valid() && held.empty() && completions == count * 4 &&
				state.Summary()[0] == 1 && std::all_of(phases.begin(), phases.end(), [](auto phase) { return phase == 4; }),
				"mixed asynchronous returns preserve every row sequence, fill eligible capacity and finish without starvation");
		}
		{
			State state(kMaximumRows);
			std::vector<Token> placed;
			while (const auto token = state.Reserve(0)) {
				check(state.Accept(*token), "accept placed calls up to the ceiling for maximum-row polling");
				placed.push_back(*token);
			}
			bool blocked = true;
			for (std::uint64_t now = 1; now <= 10000; ++now) { blocked &= !state.Reserve(now); }
			check(blocked && !state.done && state.dispatched == 4 && state.outstanding == 4 && state.Valid(),
				"repeated full-capacity polls preserve maximum-row work without excess admission");
			check(state.Complete(placed[2]), "release a non-leading row in the maximum-size import");
			const auto continuation = state.Reserve(10001).value();
			check(continuation.row == 2 && continuation.phase == Phase::ObjectPlaced && state.Accept(continuation) && state.Valid(),
				"a maximum-row import gives the released slot to a continuation before untouched rows");
		}
		{
			State baseline(8, SchedulingPolicy::Baseline, true, kLegacyOutstanding);
			std::vector<Token> held;
			while (const auto token = baseline.Reserve(100)) {
				check(baseline.Accept(*token), "accept predecessor-style workshop placed wave");
				held.push_back(*token);
			}
			Bytes saved;
			check(held.size() == 4 && baseline.Save([&](const auto& value) { return saved.Write(value); }, 1),
				"retain the original v1 payload for four uncertain workshop placed calls");
			State restored(8);
			check(restored.Load([&](auto& value) { return saved.Read(value); }, 1) && saved.cursor == saved.data.size() &&
				restored.done && restored.interrupted && restored.outstanding == 4 && restored.Valid() && restored.Summary()[0] == -1 &&
				!restored.Reserve(101) && !restored.Complete(held.front()),
				"the new admission policy accepts older four-placed saves as interrupted without replay or fabricated returns");
		}
	}

	template <class Check>
	void CheckDispatchABI(Check&& check)
	{
		using namespace Clipboard::PapyrusDispatchABI;
		std::int32_t calls{};
		BorrowedFunction<bool(int&)> borrowed(std::function<bool(int&)>{ [&calls](int& value) {
			++calls;
			value += calls;
			return value == 11 || value == 13;
		} });
		int value = 10;
		void* firstCallable{};
		for (const bool originalGeneration : { true, false }) {
			const auto* view = static_cast<const std::byte*>(borrowed.View(originalGeneration));
			void* callable{};
			std::memcpy(&callable, view + (originalGeneration ? kOGCallableOffset : kModernCallableOffset), sizeof(callable));
			check(callable != nullptr, "each family view exposes a callable at its audited offset");
			if (!callable) { continue; }
			void** vtable{};
			std::memcpy(&vtable, callable, sizeof(vtable));
			using Invoke = bool (*)(void*, int&);
			Invoke invoke{};
			std::memcpy(&invoke, vtable + 2, sizeof(invoke));
			check(invoke(callable, value), "the audited virtual invoke slot executes the live captured callable");
			if (originalGeneration) { firstCallable = callable; }
			else { check(callable == firstCallable, "OG and NG/AE borrow one owning callable without copying it"); }
		}
		check(calls == 2 && value == 13, "both family views preserve reference arguments and mutable captured state");
	}
}

int main()
{
	using namespace Clipboard::ImportJobs;
	int failures{}, checks{};
	const auto check = [&](bool passed, const char* message) {
		++checks;
		if (!passed) { ++failures; std::cerr << message << '\n'; }
	};
	CheckSerializedBooleans(check);
	for (const auto count : { 0u, 127u, 128u, 129u, 1194u, kMaximumRows }) {
		State state;
		state.rows.resize(count);
		for (std::uint32_t i = 0; i < count; ++i) {
			auto& row = state.rows[i];
			row.stage = i % 9 == 0 ? RowStage::Missing : RowStage::Complete;
			row.eligible = i % 3 != 0;  // Actors/non-Havok imports remain excluded.
			row.handled = row.eligible && row.stage == RowStage::Complete;
			row.restored = row.handled && i % 2 == 0;
			row.generator = i % 31 == 1;
			row.transform = { static_cast<float>(i), -12.5F, 999.125F, 0.25F, -0.5F, 6.25F };
		}
		state.cursor = count;
		state.passes = 4;
		state.elapsedMs = 3001;
		state.preparing = false;
		check(!state.HasPending(), "terminal physical rows should have no pending work");
		if (count) { state.rows[count / 2].stage = RowStage::Waiting; }
		check(state.HasPending() == (count != 0), "pending detection must not stop at128");
		Bytes bytes;
		check(state.Save([&](const auto& value) { return bytes.Write(value); }), "save native rows beyond Papyrus capacity");
		State restored;
		check(restored.Load([&](auto& value) { return bytes.Read(value); }), "restore suspended import state");
		check(restored.rows.size() == count && restored.cursor == count && restored.passes == 4 && restored.elapsedMs == 3001,
			"save/load preserves row cardinality and continuation");
		bool equal = true;
		for (std::uint32_t i = 0; i < count; ++i) {
			const auto& before = state.rows[i];
			const auto& after = restored.rows[i];
			equal &= before.stage == after.stage && before.transform == after.transform && before.eligible == after.eligible &&
				before.handled == after.handled && before.restored == after.restored && before.generator == after.generator;
		}
		check(equal, "null rows, actors, transform targets and handled state preserve physical indices");
		check(bytes.cursor == bytes.data.size(), "serializer consumes exactly its fields");
	}
	State sample;
	sample.rows.resize(1194);
	sample.cursor = 64;
	sample.rows[0].stage = RowStage::Complete;
	sample.rows[0].eligible = sample.rows[0].handled = sample.rows[0].restored = true;
	sample.rows[0].transform[0] = -12345.25F;
	sample.cancelled = true;
	Bytes valid;
	check(sample.Save([&](const auto& value) { return valid.Write(value); }), "save cancelled native job");
	const auto reject = [&](Bytes corrupt, const char* message) {
		State previous;
		previous.rows.resize(7);
		check(!previous.Load([&](auto& value) { return corrupt.Read(value); }), message);
		check(previous.rows.size() == 7, "failed load must not partially replace live state");
	};
	{
		auto corrupt = valid;
		std::uint32_t tooMany = kMaximumRows + 1;
		std::memcpy(corrupt.data.data(), &tooMany, sizeof(tooMany));
		reject(corrupt, "reject excessive serialized row count before allocating");
	}
	{
		auto corrupt = valid;
		std::uint32_t invalidCursor = 1195;
		std::memcpy(corrupt.data.data() + 4, &invalidCursor, sizeof(invalidCursor));
		reject(corrupt, "reject cursor outside serialized physical rows");
	}
	{
		auto corrupt = valid;
		corrupt.data[16] = std::byte{ 2 };
		reject(corrupt, "reject non-boolean state before reading C++ bool");
	}
	{
		auto corrupt = valid;
		corrupt.data[19] = std::byte{ 99 };
		reject(corrupt, "reject unknown row state");
	}
	{
		auto corrupt = valid;
		const auto invalid = std::numeric_limits<float>::quiet_NaN();
		std::memcpy(corrupt.data.data() + 24, &invalid, sizeof(invalid));
		reject(corrupt, "reject non-finite saved target before engine transform calls");
	}
	{
		auto corrupt = valid;
		corrupt.data[20] = std::byte{ 0 };
		reject(corrupt, "reject restoration state for an ineligible actor");
	}
	{
		auto corrupt = valid;
		corrupt.data.pop_back();
		reject(corrupt, "reject truncated saved transform");
	}
	check(!SliceComplete(0, 99), "a resumed slice can make progress");
	check(!SliceComplete(63, 3), "slice can continue within both budgets");
	check(SliceComplete(64, 0), "row budget yields without blocking for a full import");
	check(SliceComplete(1, 4), "time budget yields after one expensive operation");
	check(kMaximumPasses == 60 && kRetryDelayMs == 500, "readiness retries remain finite");
	CheckImportDuplicates(check);
	CheckExistingPowerEndpoints(check);
	CheckImportRowResults(check);
	CheckWorkshopCallbacks(check);
	CheckWorkshopNoOps(check);
	CheckWorkshopAdmission(check);
	CheckPerformanceBatches(check);
	CheckPowerTaskState(check);
	CheckEnginePowerFunctor(check);
	CheckWorkshopProgress(check);
	CheckWorkshopWaitSnapshots(check);
	CheckWorkshopCallbackSlices(check);
	CheckWorkshopThrottling<Bytes>(check);
	CheckWorkshopSingleCallback<Bytes>(check);
	CheckWorkshopCallbackSpacing<Bytes>(check);
	CheckWorkshopCallbackLimit<Bytes>(check);
	CheckWorkshopCallbackDrain<Bytes>(check);
	CheckLegacyCleanupGate(check);
	CheckDispatchABI(check);
	Clipboard::SerializationFactoryChecks::Run(check);
	std::cout << checks << " import state/callback/dispatch ABI checks; " << failures << " failures\n";
	return failures == 0 ? 0 : 1;
}
