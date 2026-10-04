// SPDX-License-Identifier: GPL-3.0-or-later
#include "ConduitConnections.h"
#include "ConduitAssemblyPlan.h"
#include "LoggingPolicy.h"

#include "EngineAPI.h"
#include "EngineTaskDispatch.h"
#include "LegacyCompat.h"

namespace Clipboard::ConduitConnections
{
	namespace
	{
		struct CancellationObserved {};
		bool PlanCancelled(const std::shared_ptr<AssemblyPlan>& plan) noexcept;
		bool CheckTaskContext(const char* operation) noexcept
		{
			if (EngineTaskDispatch::InTask()) { return true; }
			try { logger::error("Clipboard refused conduit {} outside the engine task queue", operation); }
			catch (...) {}
			return false;
		}
		void CheckCancellation(const Batch& batch)
		{
			if (batch.Cancelled() || PlanCancelled(batch.assemblyPlan)) { throw CancellationObserved{}; }
		}
		// Read-only prefix from official F4SE 0.6.23/0.7.2/0.7.9 GameWorkshop.h.
		// Snapped edges belong to this workshop graph; ExtraPowerLinks describes
		// reference/wire attachments and is not sufficient to check this graph.
		// Do not construct, destroy, copy or mutate engine storage through these
		// views. See CONDUIT_REPAIR_IMPLEMENTATION.md for the executable evidence.
		struct GridConnection
		{
			std::uint32_t connection;
			std::uint32_t connector;  // Zero for AddConnection(..., nullptr).
			bool operator==(const GridConnection&) const = default;
		};
		struct GridConnectionHash
		{
			std::uint32_t operator()(const GridConnection& a_edge) const noexcept
			{
				const auto value = static_cast<std::uint64_t>(a_edge.connection) |
					(static_cast<std::uint64_t>(a_edge.connector) << 32);
				return RE::BSCRC32<std::uint64_t>{}(value);
			}
		};
		using ConnectionSet = RE::BSTSet<GridConnection, GridConnectionHash>;
		struct PowerGridPrefix
		{
			RE::BSTHashMap<std::uint32_t, ConnectionSet*> adjacencyMap;
		};
		static_assert(sizeof(GridConnection) == 0x8);
		static_assert(sizeof(ConnectionSet) == 0x30);
		static_assert(sizeof(PowerGridPrefix) == 0x30);
		static_assert(offsetof(PowerGridPrefix, adjacencyMap) == 0);

		class ScopedWorkshop
		{
		public:
			explicit ScopedWorkshop(RE::TESObjectREFR& a_workshop) :
				_previous(EngineAPI::CurrentWorkshop())
			{
				EngineAPI::SetCurrentWorkshop(a_workshop.GetHandle());
			}
			~ScopedWorkshop() { EngineAPI::SetCurrentWorkshop(_previous); }
			ScopedWorkshop(const ScopedWorkshop&) = delete;
			ScopedWorkshop& operator=(const ScopedWorkshop&) = delete;

		private:
			RE::ObjectRefHandle _previous;
		};

		[[nodiscard]] bool IsValidReference(RE::TESObjectREFR* a_reference)
		{
			return a_reference && a_reference->formID != 0 && !a_reference->IsDeleted() &&
				a_reference->data.objectReference && !a_reference->data.objectReference->IsDeleted();
		}

		[[nodiscard]] bool HasDirectedEdge(
			const RE::Workshop::ExtraData& a_workshop, std::uint32_t a_source, std::uint32_t a_target,
			const Batch& batch)
		{
			CheckCancellation(batch);
			for (const auto* grid : a_workshop.powerGrid) {
				CheckCancellation(batch);
				if (!grid) {
					continue;
				}
				const auto& map = reinterpret_cast<const PowerGridPrefix*>(grid)->adjacencyMap;
				const auto found = map.find(a_source);
				if (found == map.end() || !found->second) {
					continue;
				}
				for (const auto& edge : *found->second) {
					CheckCancellation(batch);
					if (IsDirectConnection(a_target, edge.connection, edge.connector)) {
						CheckCancellation(batch);
						return true;
					}
				}
			}
			CheckCancellation(batch);
			return false;
		}

		[[nodiscard]] bool HasCompleteEdge(
			const RE::Workshop::ExtraData& a_workshop, std::uint32_t a_first, std::uint32_t a_second,
			const Batch& batch)
		{
			return HasDirectedEdge(a_workshop, a_first, a_second, batch) &&
				HasDirectedEdge(a_workshop, a_second, a_first, batch);
		}

		[[nodiscard]] bool IsFinite(const RE::NiPoint3& a_point)
		{
			return std::isfinite(a_point.x) && std::isfinite(a_point.y) && std::isfinite(a_point.z);
		}
	}

	#include "ConduitPairTrace.inl"
	#include "ConduitAssemblyPlan.inl"

	Progress ProcessReference(
		RE::TESObjectREFR* a_source,
		RE::BGSKeyword* a_workshopItemKeyword,
		RE::TESObjectREFR* a_expectedWorkshop,
		Batch& a_batch,
		ReferenceCursor& a_cursor,
		std::size_t a_maxPointVisits,
		ExecutionMode a_mode) try
	{
		if (!CheckTaskContext("reference processing")) {
			++a_batch.counts.failed; a_cursor.complete = true; return Progress::Complete;
		}
		CheckCancellation(a_batch);
		auto& counts = a_batch.counts;
		if (a_source && a_batch.excludedReferences.contains(a_source->formID)) {
			++counts.rejectedEndpoints;
			a_cursor.complete = true;
			return Progress::Complete;
		}
		if (a_cursor.complete) {
			return Progress::Complete;
		}
		if (!a_cursor.started) {
			a_cursor.started = true;
			++counts.references;
		}
		// Counting flags may survive a save/load while the transient FormID and
		// point index restart. Bind to the caller's freshly resolved reference.
		if (a_cursor.sourceFormID == 0 && a_source) {
			a_cursor.sourceFormID = a_source->formID;
		}
		if (!IsValidReference(a_source) || a_cursor.sourceFormID != a_source->formID ||
			!a_workshopItemKeyword) {
			++counts.failed;
			a_cursor.complete = true;
			return Progress::Complete;
		}
		CheckCancellation(a_batch);
		auto* workshop = EngineAPI::GetLinkedRef(a_source, a_workshopItemKeyword);
		if (!IsValidReference(workshop) || (a_expectedWorkshop && workshop != a_expectedWorkshop)) {
			++counts.ineligible;
			if (a_expectedWorkshop) {
				++counts.failed;
			}
			a_cursor.complete = true;
			return Progress::Complete;
		}
		CheckCancellation(a_batch);
		auto* workshopData = workshop->extraList ? workshop->extraList->GetByType<RE::Workshop::ExtraData>() : nullptr;
		if (!workshopData) {
			++counts.failed;
			a_cursor.complete = true;
			return Progress::Complete;
		}
		CheckCancellation(a_batch);
		auto* root = a_source->Get3D();
		if (!root) {
			++counts.pending;
			return Progress::Pending;
		}
		if (a_mode == ExecutionMode::RefreshOnly) {
			CheckCancellation(a_batch);
			ScopedWorkshop currentWorkshop{ *workshop };
			return RefreshPlannedReference(a_source, a_workshopItemKeyword, workshop, workshopData,
				a_batch, a_cursor, a_maxPointVisits);
		}
		CheckCancellation(a_batch);
		auto* points = RE::fallout_cast<RE::BSConnectPoint::Parents*>(root->GetExtraData("CPA"));
		if (!points || points->points.empty()) {
			++counts.ineligible;
			a_cursor.complete = true;
			return Progress::Complete;
		}
		if (!a_cursor.candidate) {
			a_cursor.candidate = true;
			++counts.candidates;
			if (a_mode == ExecutionMode::ConnectionsOnly && a_batch.assemblyPlan) {
				if (!a_batch.assemblyPlan->records.Candidate(a_source->formID)) { throw std::runtime_error("Conduit assembly candidate rejected"); }
				a_batch.assemblyPlan->references.try_emplace(a_source->formID, a_source);
			}
		}
		CheckCancellation(a_batch);
		ScopedWorkshop currentWorkshop{ *workshop };
		std::size_t visits = 0;
		while (a_cursor.nextPoint < points->points.size() && visits < a_maxPointVisits) {
			CheckCancellation(a_batch);
			++visits;
			const auto* point = points->points[static_cast<std::uint32_t>(a_cursor.nextPoint)];
			if (!point || point->parent == "") {
				++a_cursor.nextPoint;
				continue;
			}
			auto* parent = root->GetObjectByName(point->parent);
			if (!parent) {
				// The inherited helper compared nullptr != root and accidentally
				// queried an untransformed local coordinate in this case.
				// CPA also contains structural/model-specific points. Without a
				// usable parent this is a skipped point, not a failed power edge.
				++counts.missingParents;
				++a_cursor.nextPoint;
				continue;
			}
			if (parent == root) {
				++a_cursor.nextPoint;
				continue;
			}
			auto* world = a_source->parentCell ? a_source->parentCell->GetbhkWorld() : nullptr;
			if (!world) {
				++counts.pending;
				return Progress::Pending;
			}
			// Preserve the official F4SE helper's transform and 8-unit matcher.
			// Scale handling is a separate runtime gate, not an inferred repair.
			const auto position = MultiplyTranspose(parent->world.rotate, point->position) + parent->world.translate;
			if (!IsFinite(position)) {
				++counts.failed;
				++a_cursor.nextPoint;
				continue;
			}
			++counts.points;
			EngineAPI::ConnectPoint::Status status{ EngineAPI::ConnectPoint::Status::kCount };
			CheckCancellation(a_batch);
			auto* target = EngineAPI::GetObjectAtConnectPoint(*a_source, position, *world, 8.0f, &status);
			CheckCancellation(a_batch);
			switch (status) {
			case EngineAPI::ConnectPoint::Status::kNoReference:
				++counts.noReference;
				break;
			case EngineAPI::ConnectPoint::Status::kNoSnapPoint:
				++counts.noSnapPoint;
				break;
			case EngineAPI::ConnectPoint::Status::kNonReferenceHit:
				++counts.nonReferenceHit;
				break;
			case EngineAPI::ConnectPoint::Status::kSnapPointFound:
				break;
			default:
				++counts.unexpectedQueryStatus;
				++counts.failed;
				break;
			}
			if (!target) {
				++a_cursor.nextPoint;
				continue;
			}
			if (a_batch.excludedReferences.contains(target->formID)) {
				logger::warn("Import conduit connection excluded failed/unready endpoint {:08X} from source {:08X}", target->formID, a_source->formID);
				++counts.rejectedEndpoints;
				++a_cursor.nextPoint;
				continue;
			}
			CheckCancellation(a_batch);
			auto* targetWorkshop = IsValidReference(target) ?
				EngineAPI::GetLinkedRef(target, a_workshopItemKeyword) : nullptr;
			if (status != EngineAPI::ConnectPoint::Status::kSnapPointFound || !targetWorkshop ||
				!IsSameWorkshopPair(a_source->formID, target->formID, workshop->formID, targetWorkshop->formID)) {
				++counts.rejectedEndpoints;
				++a_cursor.nextPoint;
				continue;
			}
			if (!target->Get3D() || !target->parentCell || !target->parentCell->GetbhkWorld()) {
				++counts.pending;
				return Progress::Pending;
			}
			++a_cursor.nextPoint;
			++counts.matches;
			const auto key = PairKey(a_source->formID, target->formID);
			if (!a_batch.pairs.insert(key).second) {
				++counts.duplicatePairs;
				continue;
			}
			std::size_t traceIndex = Trace::PairLimit;
			bool firstCheck = true;
			bool completeEdge = false;
			ProcessMatchedEdge(a_mode, counts,
				[&] {
					CheckCancellation(a_batch);
					if (!a_batch.trace || !a_batch.trace->Active()) { return completeEdge = HasCompleteEdge(*workshopData, a_source->formID, target->formID, a_batch); }
					const auto bits = TraceBits(*workshopData, a_source->formID, target->formID, a_batch);
					a_batch.trace->Safely([&] {
						if (firstCheck) {
							traceIndex = a_batch.trace->MatchPair(TraceEndpoint(a_source), TraceEndpoint(target),
								a_mode == ExecutionMode::ConnectionsOnly, static_cast<std::uint32_t>(a_cursor.nextPoint - 1),
								{ position.x, position.y, position.z }, bits);
							if (a_mode == ExecutionMode::ConnectionsOnly && traceIndex < Trace::PairLimit) {
								a_batch.trace->pairs[traceIndex].added = a_batch.trace->pairs[traceIndex].assembly.observation;
							}
						} else if (traceIndex < Trace::PairLimit) { a_batch.trace->Observe(traceIndex, Trace::Point::AssemblyAdded, bits); }
					});
					firstCheck = false;
					return completeEdge = bits == 7;
				},
				[&] {
					// Engine AddConnection inserts both directions in its sets. A
					// partially present edge is repaired by that same idempotent API.
					CheckCancellation(a_batch);
					EngineAPI::WorkshopExtraAddConnection(workshopData, a_source, target, nullptr);
				},
				[&] { TraceRefresh(a_batch, target, workshopData, Trace::Operation::Target); });
			CheckCancellation(a_batch);
			if (a_mode == ExecutionMode::ConnectionsOnly && completeEdge && a_batch.assemblyPlan) {
				auto& plan = *a_batch.assemblyPlan;
				plan.references.try_emplace(target->formID, target);
				if (!plan.records.Add(a_source->formID, target->formID, static_cast<std::uint32_t>(a_cursor.nextPoint - 1),
					{ position.x, position.y, position.z })) { throw std::runtime_error("Conduit assembly membership limit or state failure"); }
			}
		}
		if (a_cursor.nextPoint < points->points.size()) {
			return Progress::InProgress;
		}
		if (a_mode != ExecutionMode::ConnectionsOnly) {
			TraceRefresh(a_batch, a_source, workshopData, Trace::Operation::Source);
			++counts.refreshed;
		}
		CheckCancellation(a_batch);
		a_cursor.complete = true;
		return Progress::Complete;
	}
	catch (const CancellationObserved&) {
		// The owner consumes cancellation separately. Do not credit an unfinished
		// row or misclassify an interrupted graph read as a missing connection.
		return Progress::InProgress;
	}

	std::string Describe(const Counts& a_counts)
	{
		std::ostringstream text;
		text << "Conduit repair: references=" << a_counts.references
			 << " candidates=" << a_counts.candidates << " points=" << a_counts.points
			 << " matches=" << a_counts.matches << " added=" << a_counts.added
			 << " existing=" << a_counts.existing << " pendingVisits=" << a_counts.pending
			 << " failed=" << a_counts.failed << " ineligible=" << a_counts.ineligible
			 << " missingParents=" << a_counts.missingParents
			 << " rejectedEndpoints=" << a_counts.rejectedEndpoints
			 << " duplicatePairs=" << a_counts.duplicatePairs << " refreshed=" << a_counts.refreshed
			 << " noReference=" << a_counts.noReference << " noSnapPoint=" << a_counts.noSnapPoint
			 << " nonReferenceHit=" << a_counts.nonReferenceHit
			 << " unexpectedQueryStatus=" << a_counts.unexpectedQueryStatus;
		return text.str();
	}
}
