#pragma once
#include "Nsmp4SamplePlayer.hpp"
#include "ScxtBlockAdapter.hpp"
#include "SynthParameters.hpp"
#include <memory>
namespace nord {struct Bank;}
namespace nord::synth {
class SampleSynthEngine {
 struct State;std::unique_ptr<State> state_;
public:
 SampleSynthEngine();~SampleSynthEngine();
 SampleSynthEngine(const SampleSynthEngine&)=delete;SampleSynthEngine&operator=(const SampleSynthEngine&)=delete;
 void prepare(double hostRate,uint32_t maximumHostBlockSize);
 // Offline preparation only. Live publications use the existing Bank/worker guard.
 void prepareInstrument(std::shared_ptr<const Instrument>);
 void resetVoices() noexcept;
 // Audio thread only: preset changes start with a centered Pitch Bend position.
 void resetPitchBend() noexcept;
 void setBank(const nord::Bank*)noexcept;
 void setParameters(const SynthParameters&)noexcept;
 void render(const RenderContext&)noexcept;
 size_t activeVoices()const noexcept;
 void addMidiOverflows(uint64_t)noexcept;
 uint64_t midiOverflows()const noexcept;
};
}
