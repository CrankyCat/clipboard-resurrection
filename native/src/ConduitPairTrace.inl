// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
// Included in ConduitConnections.cpp; uses the same reviewed graph reads as
// the existing edge predicate. No engine hooks, extra mutations or VM calls.
namespace
{
	std::uint8_t TraceBits(const RE::Workshop::ExtraData& data, std::uint32_t a, std::uint32_t b,
		const Batch& batch)
	{
		return Trace::Bits(HasDirectedEdge(data, std::min(a, b), std::max(a, b), batch),
			HasDirectedEdge(data, std::max(a, b), std::min(a, b), batch));
	}
	Trace::Endpoint TraceEndpoint(RE::TESObjectREFR* ref)
	{
		const auto* base = ref->data.objectReference;
		return { ref->formID, base ? base->formID : 0, ref->parentCell ? ref->parentCell->formID : 0,
			base ? static_cast<std::uint32_t>(base->GetFormType()) : 0, Trace::NoRow,
			{ ref->data.location.x, ref->data.location.y, ref->data.location.z } };
	}
	void TraceRefresh(Batch& batch, RE::TESObjectREFR* ref, RE::Workshop::ExtraData* data, Trace::Operation kind)
	{
		CheckCancellation(batch);
		const auto read = [&](const Trace::Pair& pair) -> std::uint8_t {
			try { return TraceBits(*data, pair.low.ref, pair.high.ref, batch); }
			catch (const CancellationObserved&) { batch.trace->Cancel(); return 0; }
		};
		if (batch.trace) { batch.trace->AroundUpdate(ref->formID, kind, true, read); }
		// Diagnostic reads may have observed a cancellation swallowed by Safely.
		// Recheck before the actual mutation; trace failures alone do not skip it.
		CheckCancellation(batch);
		EngineAPI::UpdateMovingWirelessItem(ref, data);
		CheckCancellation(batch);
		if (batch.trace) { batch.trace->AroundUpdate(ref->formID, kind, false, read); }
		CheckCancellation(batch);
	}
	const char* TracePointName(Trace::Point point)
	{
		switch (point) {
		case Trace::Point::AssemblyMatch: return "assembly-match";
		case Trace::Point::AssemblyAdded: return "assembly-added";
		case Trace::Point::AssemblyEnd: return "assembly-end";
		case Trace::Point::PreRefresh: return "pre-refresh";
		case Trace::Point::RefreshMatch: return "refresh-match";
		case Trace::Point::BeforeUpdate: return "before-update";
		case Trace::Point::AfterUpdate: return "after-update";
		default: return "final";
		}
	}
	std::string TraceObservation(const Trace::Observation& value)
	{
		return fmt::format("{}:bits={},seq={},ms={},slice={},op={},actor={:08X},kind={}",
			TracePointName(value.point), value.bits, value.sequence, value.elapsedMs, value.slice,
			value.operation, value.actor, static_cast<unsigned>(value.kind));
	}
}

bool TraceBoundary(RE::TESObjectREFR* workshop, Batch& batch, Trace::Point point, std::size_t& cursor) noexcept
{
	const auto trace = batch.trace;
	if (!trace || !trace->Active()) { return true; }
	if (!CheckTaskContext("diagnostic validation")) { trace->error = true; return true; }
	if (batch.Cancelled()) { trace->Cancel(); return true; }
	trace->Safely([&] { try {
		CheckCancellation(batch);
		// Refuse a changed destination; no retained workshop pointer crosses tasks.
		if (!IsValidReference(workshop) || workshop->formID != trace->workshop) { trace->error = true; return; }
		const auto* data = workshop->extraList ? workshop->extraList->GetByType<RE::Workshop::ExtraData>() : nullptr;
		const auto start = std::chrono::steady_clock::now();
		std::size_t visits = 0;
		while (cursor < trace->pairs.size() && visits < 64) {
			CheckCancellation(batch);
			const auto& pair = trace->pairs[cursor];
			const auto bits = data ? TraceBits(*data, pair.low.ref, pair.high.ref, batch) : 0;
			CheckCancellation(batch);
			trace->Observe(cursor++, point, static_cast<std::uint8_t>(bits));
			++visits;
			if (std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(1)) { break; }
		}
		if (cursor == trace->pairs.size()) {
			if (point == Trace::Point::AssemblyEnd) { trace->assemblyFinished = true; }
			if (point == Trace::Point::PreRefresh) { trace->preFinished = true; }
			if (point == Trace::Point::Final) { trace->finalFinished = true; }
		}
	} catch (const CancellationObserved&) { trace->Cancel(); } });
	if (batch.Cancelled()) { trace->Cancel(); return true; }
	return !trace->Active() || cursor == trace->pairs.size();
}

void LogPairTrace(Batch& batch, bool assembly, bool cancelled) noexcept
{
	const auto trace = batch.trace;
	if (!trace || !Logging::Enabled()) { return; }
	// Logging remains useful when collection failed: never turn truncation into
	// a conclusion that a pair was absent. All bounds are per power session.
	try {
		std::size_t assembled{}, refreshed{}, newPairs{}, lostBefore{}, failedChecks{}, changed{}, finalMissing{}, unknown{}, assemblyUnknown{}, preUnknown{}, details{}, healthy{};
		for (const auto& pair : trace->pairs) {
			assembled += pair.assembled; refreshed += pair.refreshed;
			newPairs += trace->NewToRefresh(pair);
			lostBefore += pair.assembled && pair.assemblyEnd.bits == 7 && Trace::Incomplete(pair.preRefresh.bits);
			failedChecks += pair.refreshed && Trace::Incomplete(pair.refresh.observation.bits);
			changed += pair.refreshChanged;
			finalMissing += Trace::Incomplete(pair.final.bits);
			unknown += assembly ? pair.assemblyEnd.bits == 0 : pair.final.bits == 0;
			assemblyUnknown += pair.assembled && pair.assemblyEnd.bits == 0;
			preUnknown += pair.assembled && pair.preRefresh.bits == 0;
		}
		logger::info("Conduit pair trace token={} phase={} tool={:08X} workshop={:08X} assemblyStack={} refreshStack={} retained={} assembled={} refreshed={} newToRefresh={} lostBetweenPasses={} failedChecks={} changedDuringRefresh={} finalIncomplete={} unknown={} assemblySweep={} preSweep={} finalSweep={} pairDrops={} assemblyDrops={} rowDrops={} eventDrops={} incidentChecksDropped={} error={} cancelled={} operations={} events={} loggingStopped={}",
			trace->token, assembly ? "assembly" : "refresh", trace->tool, trace->workshop, trace->assemblyStack, trace->refreshStack,
			trace->pairs.size(), assembled, refreshed, newPairs, lostBefore, failedChecks, changed, finalMissing, unknown,
			trace->assemblyFinished, trace->preFinished, trace->finalFinished, trace->pairsDropped, trace->assemblyDropped,
			trace->rowsDropped, trace->eventsDropped, trace->incidentChecksDropped, trace->error,
			cancelled || trace->cancelled.load(), trace->operation, trace->events.size(), trace->loggingStopped);
		logger::info("Conduit pair trace coverage token={} assemblyUnknown={} preRefreshUnknown={} membershipComplete={} pairLimit={} incidentLimit={} eventLimit={}",
			trace->token, assemblyUnknown, preUnknown, trace->assemblyFinished && !trace->assemblyDropped && trace->Active(),
			Trace::PairLimit, Trace::IncidentLimit, Trace::EventLimit);
		if (assembly && !cancelled && !batch.counts.HasFailures() && trace->Active()) { return; }
		std::size_t interesting{}, emitted{};
		for (const auto& pair : trace->pairs) {
			const bool selected = Trace::Session::Interesting(pair);
			interesting += selected;
			if (selected ? details == Trace::DetailLimit : healthy == 8) { continue; }
			if (selected) { ++details; } else { ++healthy; }
			const auto key = Trace::Key(pair.low.ref, pair.high.ref);
			logger::info("Conduit pair token={} pair={:016X} assembled={} refreshed={} newToRefresh={} lowRow={} highRow={} before={} added={} assemblyEnd={} pre={} match={} final={}",
				trace->token, key, pair.assembled, pair.refreshed, trace->NewToRefresh(pair), pair.low.row, pair.high.row,
				TraceObservation(pair.assembly.observation), TraceObservation(pair.added), TraceObservation(pair.assemblyEnd),
				TraceObservation(pair.preRefresh), TraceObservation(pair.refresh.observation), TraceObservation(pair.final));
			for (const auto& endpoint : { pair.low, pair.high }) {
				logger::info("Conduit endpoint token={} pair={:016X} ref={:08X} base={:08X} type={} cell={:08X} row={} xyz=[{},{},{}]",
					trace->token, key, endpoint.ref, endpoint.base, endpoint.type, endpoint.cell, endpoint.row,
					endpoint.position[0], endpoint.position[1], endpoint.position[2]);
			}
			logger::info("Conduit match token={} pair={:016X} assemblySource={:08X} assemblyPoint={} assemblyXYZ=[{},{},{}] refreshSource={:08X} refreshPoint={} refreshXYZ=[{},{},{}]",
				trace->token, key, pair.assembly.source, pair.assembly.pointIndex,
				pair.assembly.queryPosition[0], pair.assembly.queryPosition[1], pair.assembly.queryPosition[2],
				pair.refresh.source, pair.refresh.pointIndex, pair.refresh.queryPosition[0], pair.refresh.queryPosition[1], pair.refresh.queryPosition[2]);
		}
		std::size_t relevantEvents{};
		for (const auto& change : trace->events) {
			// Expected absent->added transitions are already in pair baselines.
			if (change.after.point == Trace::Point::AssemblyAdded) { continue; }
			++relevantEvents;
			if (emitted == Trace::EventLogLimit) { continue; }
			++emitted;
			const auto& pair = trace->pairs[change.pair];
			logger::info("Conduit change token={} pair={:016X} from={} to={}", trace->token,
				Trace::Key(pair.low.ref, pair.high.ref), TraceObservation(change.before), TraceObservation(change.after));
		}
		logger::info("Conduit pair trace output token={} detailRows={} healthyRows={} detailOmitted={} changeRows={} changeOmitted={} bits=0:unknown,4:absent,5:low-to-high,6:high-to-low,7:both rowUnknown={} operationKind=0:none,1:target,2:source",
			trace->token, details, healthy, interesting - details, emitted, relevantEvents - emitted, Trace::NoRow);
	} catch (...) { trace->error = true; }
}
