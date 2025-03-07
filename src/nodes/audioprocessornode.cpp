// Copyright 2023 Kushview, LLC <info@kushview.net>
// SPDX-License-Identifier: GPL3-or-later

#include "nodes/audioprocessor.hpp"
#include "nodes/baseprocessor.hpp"
#include "nodes/mididevice.hpp"
#include "scopedflag.hpp"

namespace element {

//=============================================================================
class AudioProcessorNodeParameter : public Parameter,
                                    private AudioProcessorParameter::Listener,
                                    private Parameter::Listener
{
public:
    AudioProcessorNodeParameter (AudioProcessorParameter& p)
        : param (p)
    {
        param.addListener (this);
        addListener (this);
    }

    ~AudioProcessorNodeParameter()
    {
        param.removeListener (this);
        removeListener (this);
    }

    int getPortIndex() const noexcept override { return portIndex; }
    int getParameterIndex() const noexcept override { return param.getParameterIndex(); }
    float getValue() const override { return param.getValue(); }
    void setValue (float newValue) override { param.setValue (newValue); }
    float getDefaultValue() const override { return param.getDefaultValue(); }
    float getValueForText (const String& text) const override { return param.getValueForText (text); }
    String getName (int maximumStringLength) const override { return param.getName (maximumStringLength); }
    String getLabel() const override { return param.getLabel(); }
    int getNumSteps() const override { return param.getNumSteps(); }
    bool isDiscrete() const override { return param.isDiscrete(); }
    bool isBoolean() const override { return param.isBoolean(); }
    String getText (float value, int maxLen) const override { return param.getText (value, maxLen); }
    bool isOrientationInverted() const override { return param.isOrientationInverted(); }
    bool isAutomatable() const override { return param.isAutomatable(); }
    bool isMetaParameter() const override { return param.isMetaParameter(); }
    Category getCategory() const override { return static_cast<Parameter::Category> (param.getCategory()); }
    String getCurrentValueAsText() const override { return param.getCurrentValueAsText(); }
    StringArray getValueStrings() const override { return param.getAllValueStrings(); }

private:
    friend class AudioProcessorNode;
    AudioProcessorParameter& param;
    int portIndex = -1;
    bool ignoreChanges { false };

    void controlValueChanged (int /*index*/, float value) override
    {
        if (ignoreChanges)
            return;
        ScopedFlag sf (ignoreChanges, true);
        param.sendValueChangedMessageToListeners (value);
    }

    void controlTouched (int /*index*/, bool grabbed) override
    {
        if (ignoreChanges)
            return;
        ScopedFlag sf (ignoreChanges, true);
        grabbed ? param.beginChangeGesture() : param.endChangeGesture();
    }

    void parameterValueChanged (int /*index*/, float value) override
    {
        if (ignoreChanges)
            return;
        ScopedFlag sf (ignoreChanges, true);
        sendValueChangedMessageToListeners (value);
    }

    void parameterGestureChanged (int /*index*/, bool grabbed) override
    {
        if (ignoreChanges)
            return;
        ScopedFlag sf (ignoreChanges, true);
        sendGestureChangedMessageToListeners (grabbed);
    }
};

//=============================================================================
void AudioProcessorNode::prepareToRender (double sampleRate, int maxBufferSize)
{
    if (! proc)
    {
        jassertfalse;
        return;
    }

    proc->setRateAndBufferSizeDetails (sampleRate, maxBufferSize);
    proc->prepareToPlay (sampleRate, maxBufferSize);
}

void AudioProcessorNode::releaseResources()
{
    if (! proc)
    {
        jassertfalse;
        return;
    }

    proc->releaseResources();
}

void AudioProcessorNode::EnablementUpdater::handleAsyncUpdate()
{
    node.setEnabled (! node.isEnabled());
}

AudioProcessorNode::AudioProcessorNode (AudioProcessor* processor)
    : AudioProcessorNode (0, processor) {}

AudioProcessorNode::AudioProcessorNode (uint32 nodeId, AudioProcessor* processor)
    : Processor (nodeId),
      enablement (*this)
{
    proc.reset (processor);
    jassert (proc != nullptr);
    setLatencySamples (proc->getLatencySamples());
    setName (proc->getName());
    proc->refreshParameterList();

    for (auto* param : proc->getParameters())
        params.add (new AudioProcessorNodeParameter (*param));
}

AudioProcessorNode::~AudioProcessorNode()
{
    params.clear();
    Processor::clearParameters();
    enablement.cancelPendingUpdate();
    pluginState.reset();
    proc = nullptr;
}

void AudioProcessorNode::getState (MemoryBlock& block)
{
    if (proc != nullptr)
        proc->getStateInformation (block);
}

void AudioProcessorNode::setState (const void* data, int size)
{
    if (proc != nullptr)
        proc->setStateInformation (data, size);
}

void AudioProcessorNode::refreshPorts()
{
    PortList newPorts;

    auto* const midiDevice = dynamic_cast<MidiDeviceProcessor*> (proc.get());
    const bool isMidiDevice = nullptr != midiDevice;
    const bool isMidiDeviceInput = isMidiDevice && midiDevice->isInputDevice();
    const bool isMidiDeviceOutput = isMidiDevice && midiDevice->isOutputDevice();
    ignoreUnused (isMidiDeviceInput, isMidiDeviceOutput);

    int index = 0, channel = 0, busIdx = 0;
    for (busIdx = 0; busIdx < proc->getBusCount (true); ++busIdx)
    {
        auto* const bus = proc->getBus (true, busIdx);
        for (int busCh = 0; busCh < bus->getNumberOfChannels(); ++busCh)
        {
            String name = bus->getName();
            name << " " << int (channel + 1);
            String symbol = "audio_in_";
            symbol << int (channel + 1);
            newPorts.add (PortType::Audio, index, channel, symbol, name, true);
            ++index;
            ++channel;
            jassert (! isMidiDevice);
        }
    }
    jassert (channel == proc->getTotalNumInputChannels());

    channel = 0;
    for (busIdx = 0; busIdx < proc->getBusCount (false); ++busIdx)
    {
        auto* const bus = proc->getBus (false, busIdx);
        for (int busCh = 0; busCh < bus->getNumberOfChannels(); ++busCh)
        {
            String name = bus->getName();
            name << " " << int (channel + 1);
            String symbol = "audio_out_";
            symbol << int (channel + 1);
            newPorts.add (PortType::Audio, index, channel, symbol, name, false);
            ++index;
            ++channel;
            jassert (! isMidiDevice);
        }
    }
    jassert (channel == proc->getTotalNumOutputChannels());

    const auto& procParams = proc->getParameters();
    for (int i = 0; i < procParams.size(); ++i)
    {
        auto* const param = procParams.getUnchecked (i);
        String symbol = "control_";
        symbol << i;
        newPorts.add (PortType::Control, index, i, symbol, param->getName (32), true);
        ++index;
    }

    if (procParams.size() != params.size())
    {
        // TODO: better controlled handling of parameter management.
        // This should have been setup in the ctor, but some plugins return an
        // empty array there.  Need to redo the plugin initialization steps preferably
        // with Unit tests.
        jassertfalse;
        clearParameters();
        params.clear();
        for (auto* procParam : procParams)
            params.add (new AudioProcessorNodeParameter (*procParam));
    }

    if (proc->acceptsMidi())
    {
        newPorts.add (PortType::Midi, index, 0, "midi_in_0", "MIDI", true);
        ++index;
    }

    if (proc->producesMidi())
    {
        if (isMidiDevice)
        {
            jassert (isMidiDeviceInput);
        }
        newPorts.add (PortType::Midi, index, 0, "midi_out_0", "MIDI", false);
        ++index;
    }

    jassert (index == newPorts.size());
    setPorts (newPorts);
}

void AudioProcessorNode::getPluginDescription (PluginDescription& desc) const
{
    if (auto pi = dynamic_cast<AudioPluginInstance*> (proc.get()))
        pi->fillInPluginDescription (desc);
}

ParameterPtr AudioProcessorNode::getParameter (const PortDescription& port)
{
    jassert (isPositiveAndBelow (port.channel, params.size()));
    auto* const param = dynamic_cast<AudioProcessorNodeParameter*> (
        params.getObjectPointerUnchecked (port.channel));
    jassert (port.channel == param->getParameterIndex());
    param->portIndex = port.index;
    return param;
}

} // namespace element
