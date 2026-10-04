// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2018 to 2026 Everett C Sands
// Included after ImportToolHandle. The process-local power session owns both
// UI accounting and the split assembly plan; neither is serialized. Only the
// assembly plan retains deduplicated references and gates refresh work.
struct PowerProgressSession
{
	explicit PowerProgressSession(std::int32_t id, const ReferenceArray& rows) : token(id), units(rows.size())
	{
		for (std::size_t i = 0; i < rows.size(); ++i) { if (rows[i]) { units.Require(i); } }
	}
	void SelectNetworkPlan(bool split) noexcept
	{
		std::scoped_lock lock(mutex);
		if (!active) { return; }
		units.SelectNetworkPlan(split);
		notice.Update(units.Done(), units.Total());
	}
	void Complete(std::size_t row, std::uint8_t bit) noexcept
	{
		std::scoped_lock lock(mutex);
		if (!active) { return; }
		units.Complete(row, bit);
		 notice.Update(units.Done(), units.Total());
	}
	std::shared_ptr<ConduitConnections::AssemblyPlan> AssemblyMembership(bool assembly,
		std::uint32_t tool, RE::TESObjectREFR* workshop, const ReferenceArray& rows)
	{
		std::scoped_lock lock(mutex);
		if (!active) { return nullptr; }
		std::vector<std::uint32_t> ids;
		ids.reserve(rows.size());
		for (auto* ref : rows) { ids.push_back(ref && !ref->IsDeleted() ? ref->formID : 0); }
		if (assembly) {
			if (assemblyStarted) { ConduitConnections::CancelAssemblyPlan(assemblyPlan); return nullptr; }
			assemblyStarted = true;
			assemblyPlan = ConduitConnections::CreateAssemblyPlan(tool, workshop, std::move(ids));
		} else if (!ConduitConnections::ClaimAssemblyPlan(assemblyPlan, tool, workshop, ids)) { return nullptr; }
		return assemblyPlan;
	}
	void ReleaseAssemblyMembership() noexcept
	{
		std::scoped_lock lock(mutex);
		ConduitConnections::CancelAssemblyPlan(assemblyPlan);
		assemblyPlan.reset();
	}
	std::shared_ptr<ConduitConnections::Trace::Session> PairTrace(bool assembly,
		std::uint32_t tool, std::uint32_t workshop, std::uint32_t stack, const ReferenceArray& rows) noexcept
	{
		std::scoped_lock lock(mutex);
		if (!active || !Logging::Enabled()) { return nullptr; }
		try {
			if (assembly) {
				// Do not merge a repeated assembly job into an older history.
				if (pairTrace) { pairTrace->Cancel(); return nullptr; }
				pairTrace = std::make_shared<ConduitConnections::Trace::Session>(token);
				pairTrace->tool = tool;
				pairTrace->workshop = workshop;
				pairTrace->assemblyStack = stack;
				pairTrace->Safely([&] {
					for (std::size_t row = 0; row < rows.size(); ++row) {
						if (rows[row] && !rows[row]->IsDeleted()) { pairTrace->AddRow(rows[row]->formID, static_cast<std::uint32_t>(row)); }
					}
				});
			} else {
				if (!pairTrace || pairTrace->tool != tool || pairTrace->workshop != workshop || pairTrace->refreshStack) { return nullptr; }
				pairTrace->refreshStack = stack;
			}
			return pairTrace;
		} catch (...) {
			if (pairTrace) { pairTrace->Cancel(); }
			return nullptr;
		}
	}
	bool Stop(bool reportCompletion = false) noexcept
	{
		std::scoped_lock lock(mutex);
		const bool done = active && units.AllDone();
		active = false;
		ConduitConnections::CancelAssemblyPlan(assemblyPlan);
		if (pairTrace) { pairTrace->Cancel(); }
		if (reportCompletion && done) { notice.Finish(); } else { notice.Stop(); }
		return done;
	}
	const std::int32_t token;
	std::mutex mutex;
	ImportProgress::PowerUnits units;
	ImportProgress::PercentageNotice notice;
	std::shared_ptr<ConduitConnections::Trace::Session> pairTrace;
	std::shared_ptr<ConduitConnections::AssemblyPlan> assemblyPlan;
	bool assemblyStarted{};
	bool active{ true };
};
std::unordered_map<std::uint64_t, std::shared_ptr<PowerProgressSession>> g_powerProgress; // g_importLock
std::uint32_t g_powerProgressSerial = [] {
	try { return static_cast<std::uint32_t>(std::random_device{}() & 0x3FFFFFFF); }
	catch (...) { return static_cast<std::uint32_t>(GetTickCount64() & 0x3FFFFFFF); }
}();

std::shared_ptr<PowerProgressSession> FindPowerProgress(const RE::BSScript::Variable& tool)
{
	std::scoped_lock lock(g_importLock);
	const auto found = g_powerProgress.find(ImportToolHandle(tool));
	return found == g_powerProgress.end() ? nullptr : found->second;
}

std::int32_t StartPowerProgress(RE::TESObjectREFR* tool, const ReferenceArray& rows)
{
	if (!tool || tool->IsDeleted()) { return 0; }
	RE::BSScript::Variable value;
	RE::BSScript::PackVariable(value, tool);
	const auto handle = ImportToolHandle(value);
	if (!handle) { return 0; }
	std::scoped_lock lock(g_importLock);
	if (const auto found = g_powerProgress.find(handle); found != g_powerProgress.end()) { found->second->Stop(); }
	g_powerProgressSerial = (g_powerProgressSerial % 0x7FFFFFFE) + 1;
	auto session = std::make_shared<PowerProgressSession>(static_cast<std::int32_t>(g_powerProgressSerial), rows);
	g_powerProgress[handle] = session;
	session->notice.Start(tool->formID, ImportProgress::Phase::Power);
	session->notice.Update(session->units.Done(), session->units.Total());
	return session->token;
}

bool StopPowerProgress(RE::TESObjectREFR* tool, std::int32_t token)
{
	if (!tool || token <= 0) { return false; }
	RE::BSScript::Variable value;
	RE::BSScript::PackVariable(value, tool);
	std::scoped_lock lock(g_importLock);
	const auto found = g_powerProgress.find(ImportToolHandle(value));
	if (found == g_powerProgress.end() || found->second->token != token) { return false; }
	const bool done = found->second->Stop(true);
	g_powerProgress.erase(found);
	return done;
}
