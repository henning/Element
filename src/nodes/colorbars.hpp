// Copyright 2023 Kushview, LLC <info@kushview.net>
// SPDX-License-Identifier: GPL3-or-later

#pragma once

#include <element/ui/nodeeditor.hpp>
#include <element/ui/style.hpp>
#include <element/processor.hpp>
#include <element/porttype.hpp>

#include "nodes/nodetypes.hpp"

namespace element {

class ColorBarsNode : public Processor,
                      public ChangeBroadcaster
{
public:
    ColorBarsNode()
        : Processor (PortCount().with (PortType::Midi, 0, 1)) {}
    ~ColorBarsNode() {}

    //==========================================================================
    void prepareToRender (double sampleRate, int maxBufferSize) override
    {
        ignoreUnused (sampleRate, maxBufferSize);
        col.reset (sampleRate);
    }

    void releaseResources() override {}

    inline bool wantsMidiPipe() const override { return true; }
    void render (AudioSampleBuffer& audio, MidiPipe& midi) override
    {
        auto buf = midi.getWriteBuffer (0);
        for (const auto msg : *buf)
        {
            juce::ignoreUnused (msg);
        }
        midi.clear();
        col.removeNextBlockOfMessages (*buf, audio.getNumSamples());
    }

    void getState (MemoryBlock&) override {}
    void setState (const void*, int sizeInBytes) override {}

    CriticalSection& getLock() { return lock; }

    int getNumPrograms() const override { return 1; }
    int getCurrentProgram() const override { return 0; }
    void setCurrentProgram (int index) override {}
    const String getProgramName (int index) const override
    {
        return "MCU";
    }

    void getPluginDescription (PluginDescription& desc) const override
    {
        desc.fileOrIdentifier = "el.ColorBars";
        desc.uniqueId = 1130;
        desc.name = "Color Bars";
        desc.descriptiveName = "Support for Mackie Control Universal";
        desc.numInputChannels = 0;
        desc.numOutputChannels = 0;
        desc.hasSharedContainer = false;
        desc.isInstrument = false;
        desc.manufacturerName = EL_NODE_FORMAT_AUTHOR;
        desc.pluginFormatName = "Element";
        desc.version = "1.0.0";
    }

    void refreshPorts() override
    {
        // nothing to do yet
    }

private:
    CriticalSection lock;
};

class MackieControlEditor : public NodeEditor
{
public:
    MackieControlEditor (const Node& node)
        : NodeEditor (node)
    {
        setOpaque (true);
        addAndMakeVisible (testButton);
        testButton.setButtonText ("Test");
        testButton.onClick = [this]() {
            proc()->sendMidi (MidiMessage::noteOn (1, 100, 1.f));
            proc()->sendMidi (MidiMessage::noteOff (1, 100));
        };

        addAndMakeVisible (onlineButton);
        onlineButton.setButtonText ("Reset");
        onlineButton.setClickingTogglesState (true);
        onlineButton.setToggleState (false, dontSendNotification);
        onlineButton.onClick = [this]() {
            if (onlineButton.getToggleState())
            {
                proc()->open();
            }
            else
            {
                proc()->close();
            }
        };

        addAndMakeVisible (fader);
        fader.setRange (0.0, 16383.0, 1.0);
        fader.setSliderStyle (Slider::LinearVertical);
        fader.onValueChange = [this]() {
            proc()->sendMidi (MidiMessage::pitchWheel (1, roundToInt (fader.getValue())));
        };

        setSize (300, 500);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (20);
        testButton.setBounds (r.removeFromTop (24));
        onlineButton.setBounds (r.removeFromTop (24));
        fader.setBounds (r.removeFromLeft (20));
    }

    void paint (Graphics& g) override
    {
        g.fillAll (element::Colors::widgetBackgroundColor);
    }

private:
    TextButton testButton;
    TextButton onlineButton;
    Slider fader;

    ColorBarsNode* proc() const noexcept { return getNodeObjectOfType<ColorBarsNode>(); }
};

} // namespace element
