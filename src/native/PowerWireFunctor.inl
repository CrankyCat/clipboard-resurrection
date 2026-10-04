// SPDX-License-Identifier: GPL-3.0-or-later
// Engine-side worker with the legacy argument serialization unchanged.
template <const char* Name, bool Transmit, bool ExplicitRows>
class PowerWireWorker final : public LatentFunctorBase
{
public:
    explicit PowerWireWorker(F4SE::SerializationTag tag) : LatentFunctorBase(tag) {}
    explicit PowerWireWorker(std::uint32_t stack, RE::TESObjectREFR* tool, std::uint32_t slot = 0, ReferenceArray rows = {}) :
        LatentFunctorBase(stack)
    {
        RE::BSScript::PackVariable(_tool, tool);
        if constexpr (!Transmit) { RE::BSScript::PackVariable(_slot, slot); }
        if constexpr (ExplicitRows) { RE::BSScript::PackVariable(_rows, std::move(rows)); }
    }
    const char* ClassName() const override { return Name; }
    std::uint32_t ClassVersion() const override { return kFunctorVersion; }
    bool Save(const F4SE::SerializationInterface* intfc) override
    {
        try {
            if (!SaveStack(intfc) || !WriteVariable(intfc, _tool)) { return false; }
            if constexpr (!Transmit) { if (!WriteVariable(intfc, _slot)) { return false; } }
            if constexpr (ExplicitRows) { if (!WriteVariable(intfc, _rows)) { return false; } }
            return true;
        } catch (...) { return false; }
    }
    bool Load(const F4SE::SerializationInterface* intfc, std::uint32_t version) override
    {
        try {
            if (version != kFunctorVersion || !LoadStack(intfc) || !ReadVariable(intfc, _tool)) { return false; }
            if constexpr (!Transmit) { if (!ReadVariable(intfc, _slot)) { return false; } }
            if constexpr (ExplicitRows) { if (!ReadVariable(intfc, _rows)) { return false; } }
            return true;
        } catch (...) { return false; }
    }
    std::uint64_t PowerToolHandle() const { return ImportToolHandle(_tool); }
    void SetPowerCancellation(PowerCancellation test) { _cancelled = std::move(test); }
    void CopyPowerCheckpoint(PowerWireWorker& target, bool initial)
    {
        if (initial) {
            target._stackID = _stackID; target._tool = _tool;
            target._slot = _slot; target._rows = _rows;
        }
        // Legacy wire payload contains only arguments. Restored calls are
        // interrupted by the coordinator, never replayed from these arguments.
    }
    static void PowerFailureResult(RE::BSScript::Variable& result)
    {
        if constexpr (Transmit) { result = nullptr; }
        else { RE::BSScript::PackVariable(result, ReferenceArray{}); }
    }
    void AbortPowerTask() noexcept
    {
        if (_work) { _work->Cancel(); _work.reset(); }
        _done = true;
    }
    bool ShouldReschedule(std::int32_t& delay) override { delay = 1; return !_done; }
    bool ShouldResumeStack(std::uint32_t& stack) override { stack = _stackID; return _done; }
    bool Run(RE::BSScript::Variable& result) override
    {
        if (_cancelled && _cancelled()) { AbortPowerTask(); PowerFailureResult(result); return true; }
        if (!_work) {
            auto* tool = UnpackSafely<RE::TESObjectREFR*>(_tool).value_or(nullptr);
            if (!tool || tool->IsDeleted()) { _done = true; PowerFailureResult(result); return true; }
            if constexpr (Transmit) {
                if (g_callbacks.createTransmitPowerJob) { _work = g_callbacks.createTransmitPowerJob(_stackID, tool, _cancelled); }
            } else {
                const auto slot = UnpackSafely<std::uint32_t>(_slot);
                ReferenceArray rows;
                if constexpr (ExplicitRows) {
                    auto unpacked = UnpackSafely<ReferenceArray>(_rows);
                    if (!unpacked) { _done = true; PowerFailureResult(result); return true; }
                    rows = std::move(*unpacked);
                }
                if (slot && g_callbacks.createPatternWireJob) {
                    _work = g_callbacks.createPatternWireJob(_stackID, tool, *slot, std::move(rows), ExplicitRows, _cancelled);
                }
            }
            if (!_work) { _done = true; PowerFailureResult(result); return true; }
        }
        ReferenceArray rows;
        _done = _work->RunSlice(rows);
        if (_done) {
            if constexpr (Transmit) { result = nullptr; }
            else { RE::BSScript::PackVariable(result, std::move(rows)); }
            _work.reset();
        }
        return true;
    }
private:
    RE::BSScript::Variable _tool, _slot, _rows;
    PowerCancellation _cancelled;
    std::shared_ptr<PowerWorkJob> _work;
    bool _done{};
};

using TransmitPowerFunctor = EnginePowerFunctor<PowerWireWorker<kTransmitPowerName, true, false>>;
using PastePatternWiresFunctor = EnginePowerFunctor<PowerWireWorker<kPastePatternWiresName, false, false>>;
using PastePatternWiresForRowsFunctor = EnginePowerFunctor<PowerWireWorker<kPastePatternWiresForRowsName, false, true>>;
