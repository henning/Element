/*
    This file is part of Element
    Copyright (C) 2019  Kushview, LLC.  All rights reserved.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <element/services.hpp>
#include <element/context.hpp>
#include <element/settings.hpp>
#include <element/devices.hpp>
#include <element/node.hpp>
#include <element/plugins.hpp>

#include "services/mappingservice.hpp"
#include "services/sessionservice.hpp"
#include "gui/widgets/MidiBlinker.h"

#include <element/ui/style.hpp>
#include "gui/MainWindow.h"
#include "gui/MainMenu.h"
#include "gui/TempoAndMeterBar.h"
#include "gui/TransportBar.h"
#include "gui/ViewHelpers.h"
#include <element/ui/commands.hpp>

#include <element/ui/content.hpp>

#ifndef EL_USE_ACCESSORY_BUTTONS
#define EL_USE_ACCESSORY_BUTTONS 0
#endif

namespace element {

ContentView::ContentView()
{
}

ContentView::~ContentView()
{
}

void ContentView::paint (Graphics& g)
{
    g.fillAll (Colors::backgroundColor);
}

bool ContentView::keyPressed (const KeyPress& k)
{
    if (escapeTriggersClose && k == KeyPress::escapeKey)
    {
        ViewHelpers::invokeDirectly (this, Commands::showLastContentView, true);
        return true;
    }

    return false;
}

//=============================================================================
class ContentComponent::Toolbar : public Component,
                                  public Button::Listener,
                                  public Timer
{
public:
    Toolbar (ContentComponent& o)
        : owner (o), viewBtn ("e")
    {
        addAndMakeVisible (viewBtn);
        viewBtn.setButtonText ("view");

#if EL_USE_ACCESSORY_BUTTONS
        addAndMakeVisible (panicBtn);
#endif

        if (isPluginVersion())
        {
            addAndMakeVisible (menuBtn);
            menuBtn.setButtonText ("settings");
        }

        for (auto* b : { (Button*) &viewBtn, (Button*) &panicBtn, (Button*) &menuBtn })
            b->addListener (this);
        addAndMakeVisible (tempoBar);
        addAndMakeVisible (transport);

        mapButton.setButtonText ("map");
        mapButton.setColour (SettingButton::backgroundOnColourId, Colors::toggleBlue);
        mapButton.addListener (this);
        addAndMakeVisible (mapButton);
        addAndMakeVisible (midiBlinker);
    }

    ~Toolbar()
    {
        for (const auto& conn : connections)
            conn.disconnect();
        connections.clear();
    }

    void setSession (SessionPtr s)
    {
        session = s;
        auto& settings (ViewHelpers::getGlobals (this)->settings());
        auto engine (ViewHelpers::getGlobals (this)->audio());

        if (midiIOMonitor == nullptr)
        {
            midiIOMonitor = engine->getMidiIOMonitor();
            connections.add (midiIOMonitor->sigSent.connect (
                std::bind (&MidiBlinker::triggerSent, &midiBlinker)));
            connections.add (midiIOMonitor->sigReceived.connect (
                std::bind (&MidiBlinker::triggerReceived, &midiBlinker)));
        }

        auto* props = settings.getUserSettings();

        bool showExt = false;
        if (isPluginVersion())
        {
            // Plugin always has host sync option
            showExt = true;
            ignoreUnused (props);
        }
        else
        {
            showExt = props->getValue ("clockSource") == "midiClock";
        }

        if (session)
        {
            tempoBar.setUseExtButton (showExt);
            tempoBar.getTempoValue().referTo (session->getPropertyAsValue (tags::tempo));
            tempoBar.getExternalSyncValue().referTo (session->getPropertyAsValue (tags::externalSync));
            tempoBar.stabilizeWithSession (false);
        }

        mapButton.setEnabled (true);
        resized();
    }

    void resized() override
    {
        Rectangle<int> r (getLocalBounds());

        const int tempoBarWidth = jmax (120, tempoBar.getWidth());
        const int tempoBarHeight = getHeight() - 16;

        tempoBar.setBounds (10, 8, tempoBarWidth, tempoBarHeight);

        r.removeFromRight (10);

        if (menuBtn.isVisible())
        {
            menuBtn.setBounds (r.removeFromRight (tempoBarHeight * 3)
                                   .withSizeKeepingCentre (tempoBarHeight * 3, tempoBarHeight));
            r.removeFromRight (4);
        }

        if (panicBtn.isVisible())
        {
            panicBtn.setBounds (r.removeFromRight (tempoBarHeight)
                                    .withSizeKeepingCentre (tempoBarHeight, tempoBarHeight));
            r.removeFromRight (4);
        }

        if (midiBlinker.isVisible())
        {
            const int blinkerW = 8;
            midiBlinker.setBounds (r.removeFromRight (blinkerW).withSizeKeepingCentre (blinkerW, tempoBarHeight));
            r.removeFromRight (4);
        }

        if (viewBtn.isVisible())
        {
            viewBtn.setBounds (r.removeFromRight (tempoBarHeight * 2)
                                   .withSizeKeepingCentre (tempoBarHeight * 2, tempoBarHeight));
        }

        if (mapButton.isVisible())
        {
            r.removeFromRight (4);
            mapButton.setBounds (r.removeFromRight (tempoBarHeight * 2)
                                     .withSizeKeepingCentre (tempoBarHeight * 2, tempoBarHeight));
        }

        if (transport.isVisible())
        {
            r = getLocalBounds().withX ((getWidth() / 2) - (transport.getWidth() / 2));
            r.setWidth (transport.getWidth());
            transport.setBounds (r.withSizeKeepingCentre (r.getWidth(), tempoBarHeight));
        }
    }

    void paint (Graphics& g) override
    {
        g.setColour (Colors::contentBackgroundColor.brighter (0.1f));
        g.fillRect (getLocalBounds());
    }

    void buttonClicked (Button* btn) override
    {
        if (btn == &viewBtn)
        {
            // FIXME:
        }
        else if (btn == &panicBtn)
        {
            ViewHelpers::invokeDirectly (this, Commands::panic, true);
        }
        else if (btn == &menuBtn)
        {
            PopupMenu menu;
            if (auto* cc = ViewHelpers::getGuiController (this))
                MainMenu::buildPluginMainMenu (cc->commands(), menu);
            auto result = menu.show();
            if (99999 == result)
            {
                ViewHelpers::closePluginWindows (this, false);
            }
        }
        else if (btn == &mapButton)
        {
            if (auto* mapping = owner.services().find<MappingService>())
            {
                mapping->learn (! mapButton.getToggleState());
                mapButton.setToggleState (mapping->isLearning(), dontSendNotification);
                if (mapping->isLearning())
                {
                    startTimer (600);
                }
            }
        }
    }

    void timerCallback() override
    {
        if (auto* mapping = owner.services().find<MappingService>())
        {
            if (! mapping->isLearning())
            {
                mapButton.setToggleState (false, dontSendNotification);
                stopTimer();
            }
        }
    }

private:
    ContentComponent& owner;
    SessionPtr session;
    MidiIOMonitorPtr midiIOMonitor;
    SettingButton menuBtn;
    SettingButton viewBtn;
    SettingButton mapButton;
    PanicButton panicBtn;
    TempoAndMeterBar tempoBar;
    TransportBar transport;
    MidiBlinker midiBlinker;
    Array<SignalConnection> connections;
    bool isPluginVersion() const
    {
        return owner.services().getRunMode() == RunMode::Plugin;
    }
};

class ContentComponent::StatusBar : public Component,
                                    public Value::Listener,
                                    private Timer
{
public:
    StatusBar (Context& g)
        : world (g),
          devices (world.devices()),
          plugins (world.plugins())
    {
        sampleRate.addListener (this);
        streamingStatus.addListener (this);
        if (isPluginVersion())
            latencySamplesChangedConnection = world.audio()->sampleLatencyChanged.connect (
                std::bind (&StatusBar::updateLabels, this));

        addAndMakeVisible (sampleRateLabel);
        addAndMakeVisible (streamingStatusLabel);
        addAndMakeVisible (statusLabel);

        const Colour labelColor (0xffaaaaaa);
        const Font font (12.0f);

        for (int i = 0; i < getNumChildComponents(); ++i)
        {
            if (Label* label = dynamic_cast<Label*> (getChildComponent (i)))
            {
                label->setFont (font);
                label->setColour (Label::textColourId, labelColor);
                label->setJustificationType (Justification::centredLeft);
            }
        }

        startTimer (2000);
        updateLabels();
    }

    ~StatusBar()
    {
        latencySamplesChangedConnection.disconnect();
        sampleRate.removeListener (this);
        streamingStatus.removeListener (this);
    }

    void paint (Graphics& g) override
    {
        g.setColour (Colors::contentBackgroundColor.brighter (0.1f));
        g.fillRect (getLocalBounds());

        const Colour lineColor (0xff545454);
        g.setColour (lineColor);

        g.drawLine (streamingStatusLabel.getX(), 0, streamingStatusLabel.getX(), getHeight());
        g.drawLine (sampleRateLabel.getX(), 0, sampleRateLabel.getX(), getHeight());
        g.setColour (lineColor.darker());
        g.drawLine (0, 0, getWidth(), 0);
        g.setColour (lineColor);
        g.drawLine (0, 1, getWidth(), 1);
    }

    void resized() override
    {
        Rectangle<int> r (getLocalBounds());
        statusLabel.setBounds (r.removeFromLeft (getWidth() / 5));
        streamingStatusLabel.setBounds (r.removeFromLeft (r.getWidth() / 2));
        sampleRateLabel.setBounds (r);
    }

    void valueChanged (Value&) override
    {
        updateLabels();
    }

    void updateLabels()
    {
        auto engine = world.audio();
        if (isPluginVersion())
        {
            String text = "Latency: ";
            text << "unknown";
            sampleRateLabel.setText (text, dontSendNotification);
            streamingStatusLabel.setText ("", dontSendNotification);
            statusLabel.setText ("Plugin", dontSendNotification);
        }
        else
        {
            if (auto* dev = devices.getCurrentAudioDevice())
            {
                String text = "Sample Rate: ";
                text << String (dev->getCurrentSampleRate() * 0.001, 1) << " KHz";
                text << ":  Buffer: " << dev->getCurrentBufferSizeSamples();
                sampleRateLabel.setText (text, dontSendNotification);

                text.clear();
                String strText = streamingStatus.getValue().toString();
                if (strText.isEmpty())
                    strText = "Running";
                text << "Engine: " << strText << ":  CPU: " << String (devices.getCpuUsage() * 100.f, 1) << "%";
                streamingStatusLabel.setText (text, dontSendNotification);

                statusLabel.setText (String ("Device: ") + dev->getName(), dontSendNotification);
            }
            else
            {
                sampleRateLabel.setText ("", dontSendNotification);
                streamingStatusLabel.setText ("", dontSendNotification);
                statusLabel.setText ("No Device", dontSendNotification);
            }

            if (plugins.isScanningAudioPlugins())
            {
                auto text = streamingStatusLabel.getText();
                auto name = plugins.getCurrentlyScannedPluginName();
                name = File::createFileWithoutCheckingPath (name).getFileName();

                text << " - Scanning: " << name;
                if (name.isNotEmpty())
                    streamingStatusLabel.setText (text, dontSendNotification);
            }
        }
    }

private:
    Context& world;
    DeviceManager& devices;
    PluginManager& plugins;

    Label sampleRateLabel, streamingStatusLabel, statusLabel;
    ValueTree node;
    Value sampleRate, streamingStatus, status;

    SignalConnection latencySamplesChangedConnection;

    friend class Timer;
    void timerCallback() override
    {
        updateLabels();
    }
    bool isPluginVersion()
    {
        if (auto* cc = ViewHelpers::findContentComponent (this))
            return cc->services().getRunMode() == RunMode::Plugin;
        return false;
    }
};

struct ContentComponent::Tooltips
{
    Tooltips() { tooltipWindow = new TooltipWindow(); }
    ScopedPointer<TooltipWindow> tooltipWindow;
};

ContentComponent::ContentComponent (Context& ctl_)
    : _context (ctl_),
      controller (ctl_.services())
{
    setOpaque (true);

    addAndMakeVisible (statusBar = new StatusBar (context()));
    statusBarVisible = true;
    statusBarSize = 22;

    addAndMakeVisible (toolBar = new Toolbar (*this));
    toolBar->setSession (context().session());
    toolBarVisible = true;
    toolBarSize = 32;

    const Node node (context().session()->getCurrentGraph());
    setCurrentNode (node);

    resized();
}

ContentComponent::~ContentComponent() noexcept
{
}

void ContentComponent::paint (Graphics& g)
{
    g.fillAll (Colors::backgroundColor);
}

void ContentComponent::resized()
{
    Rectangle<int> r (getLocalBounds());

    if (toolBarVisible && toolBar)
        toolBar->setBounds (r.removeFromTop (toolBarSize));
    if (statusBarVisible && statusBar)
        statusBar->setBounds (r.removeFromBottom (statusBarSize));

    resizeContent (r);
}

bool ContentComponent::isInterestedInDragSource (const SourceDetails& dragSourceDetails)
{
    const auto& desc (dragSourceDetails.description);
    return desc.toString() == "ccNavConcertinaPanel" || (desc.isArray() && desc.size() >= 2 && desc[0] == "plugin");
}

void ContentComponent::itemDropped (const SourceDetails& dragSourceDetails)
{
    const auto& desc (dragSourceDetails.description);
    if (desc.toString() == "ccNavConcertinaPanel")
    {
        // if (auto* panel = nav->findPanel<DataPathTreeComponent>())
        //     filesDropped (StringArray ({ panel->getSelectedFile().getFullPathName() }),
        //                   dragSourceDetails.localPosition.getX(),
        //                   dragSourceDetails.localPosition.getY());
    }
    else if (desc.isArray() && desc.size() >= 2 && desc[0] == "plugin")
    {
        auto& list (context().plugins().getKnownPlugins());
        if (auto plugin = list.getTypeForIdentifierString (desc[1].toString()))
            this->post (new LoadPluginMessage (*plugin, true));
        else
            AlertWindow::showMessageBoxAsync (AlertWindow::InfoIcon,
                                              "Could not load plugin",
                                              "The plugin you dropped could not be loaded for an unknown reason.");
    }
}

bool ContentComponent::isInterestedInFileDrag (const StringArray& files)
{
    for (const auto& path : files)
    {
        const File file (path);
        if (file.hasFileExtension ("elc;elg;els;dll;vst3;vst;elpreset"))
            return true;
    }
    return false;
}

void ContentComponent::filesDropped (const StringArray& files, int x, int y)
{
    for (const auto& path : files)
    {
        const File file (path);
        if (file.hasFileExtension ("els"))
        {
            this->post (new OpenSessionMessage (file));
        }
        else if (file.hasFileExtension ("elg"))
        {
            if (true)
            {
                if (auto* sess = controller.find<SessionService>())
                    sess->importGraph (file);
            }
            else
            {
                //
            }
        }
        else if (file.hasFileExtension ("elpreset"))
        {
            const auto data = Node::parse (file);
            if (data.hasType (types::Node))
            {
                const Node node (data, false);
                this->post (new AddNodeMessage (node));
            }
            else
            {
                AlertWindow::showMessageBox (AlertWindow::InfoIcon, "Presets", "Error adding preset");
            }
        }
        else if ((file.hasFileExtension ("dll") || file.hasFileExtension ("vst") || file.hasFileExtension ("vst3")))
        {
            PluginDescription desc;
            desc.pluginFormatName = file.hasFileExtension ("vst3") ? "VST3" : "VST";
            desc.fileOrIdentifier = file.getFullPathName();

            this->post (new LoadPluginMessage (desc, false));
        }
    }
}

void ContentComponent::post (Message* message)
{
    controller.postMessage (message);
}

void ContentComponent::setToolbarVisible (bool visible)
{
    if (toolBarVisible == visible)
        return;
    toolBarVisible = visible;
    toolBar->setVisible (toolBarVisible);
    resized();
    refreshToolbar();
}

void ContentComponent::refreshToolbar()
{
    toolBar->setSession (context().session());
}

void ContentComponent::setStatusBarVisible (bool vis)
{
    if (statusBarVisible == vis)
        return;
    statusBarVisible = vis;
    statusBar->setVisible (vis);
    resized();
    refreshStatusBar();
}

void ContentComponent::refreshStatusBar()
{
    statusBar->updateLabels();
}

Context& ContentComponent::context() { return _context; }
SessionPtr ContentComponent::session() { return _context.session(); }
void ContentComponent::stabilize (const bool refreshDataPathTrees) {}
void ContentComponent::stabilizeViews() {}
void ContentComponent::saveState (PropertiesFile*) {}
void ContentComponent::restoreState (PropertiesFile*) {}
void ContentComponent::setCurrentNode (const Node& node) { ignoreUnused (node); }
void ContentComponent::setNodeChannelStripVisible (const bool) {}
bool ContentComponent::isNodeChannelStripVisible() const { return false; }

} // namespace element
