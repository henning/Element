// Copyright 2023 Kushview, LLC <info@kushview.net>
// SPDX-License-Identifier: GPL3-or-later

#pragma once

#include "nodes/baseprocessor.hpp"
#include <element/processor.hpp>
#include "sol/sol.hpp"

namespace element {

class DSPScript;

class ScriptNode : public Processor,
                   public ChangeBroadcaster
{
public:
    using Ptr = ReferenceCountedObjectPtr<ScriptNode>;

    explicit ScriptNode() noexcept;
    virtual ~ScriptNode();

    struct Context;

    void getPluginDescription (PluginDescription& desc) const override;
    void prepareToRender (double sampleRate, int maxBufferSize) override;
    void releaseResources() override;
    void render (AudioSampleBuffer& audio, MidiPipe& midi, AudioSampleBuffer&) override;
    void setState (const void* data, int size) override;
    void getState (MemoryBlock& block) override;

    Result loadScript (const String&);

    CodeDocument& getCodeDocument (bool forEditor = false) { return forEditor ? edCode : dspCode; }

    /** Set a parameter value by index
     
        @param index    The parameter index to set
        @param value    The value to set
    */
    void setParameter (int index, float value);

    void refreshPorts() override;

protected:
    inline bool wantsMidiPipe() const override { return true; }
    ParameterPtr getParameter (const PortDescription& port) override;

private:
    CriticalSection lock;
    sol::state lua;
    CodeDocument dspCode, edCode;
    std::unique_ptr<DSPScript> script;
    ParameterArray inParams, outParams;

    int blockSize = 512;
    double sampleRate = 44100.0;
    bool prepared = false;
};

} // namespace element
