#pragma once
#include "control_mapping.hpp"
#include "eq_state.hpp"

namespace sonicsync::win {
constexpr LONG eqProtocol = 2;
struct EqCommand {
    LONG profile{}, bypass{}, preamp{};
    std::array<LONG,10> gains{};
};

inline void publishEq(ControlBlock& block, const EqCommand& state) noexcept {
    const auto next = static_cast<DWORD>(load(&block.generation))+1u;
    auto& slot = block.slots[next & 1u];
    store(&slot.profile,state.profile);
    store(&slot.bypass,state.bypass);
    store(&slot.preamp,state.preamp);
    for(size_t i=0;i<10;++i) store(&slot.gains[i], state.gains[i]);
    store(&block.generation,static_cast<LONG>(next));
    store(&block.version,eqProtocol);
}

// At most one attempt per buffer. A concurrent publish is picked up next buffer.
inline bool readEq(ControlBlock& block,LONG& previous, EqCommand& state) noexcept {
    if(load(&block.version)!=eqProtocol) return false;
    const LONG sequence=load(&block.generation);
    if(sequence==previous) return false;
    auto& slot=block.slots[static_cast<DWORD>(sequence)&1u];
    EqCommand candidate{load(&slot.profile),load(&slot.bypass),load(&slot.preamp)};
    for(size_t i=0;i<10;++i) candidate.gains[i]=load(&slot.gains[i]);
    if(load(&block.generation)!=sequence) return false;
    state=candidate;
    previous=sequence;
    return true;
}

struct LiveEq {
    LONG generation{};
    Preset parameters{};
    bool bypass{};
    bool update(ControlBlock* block, Processor& processor, double sr) noexcept {
        EqCommand command;
        if(!block || !readEq(*block,generation,command)) return false;
        parameters.preampDb=command.preamp/1000.0;
        for(int i=0;i<10;++i) setGraphicBand(parameters.bands[i],i,command.gains[i]/1000.0,sr);
        processor.setPresetRealtime(parameters);
        bypass=command.bypass!=0;
        processor.setBypass(bypass);
        return true;
    }
};
}
