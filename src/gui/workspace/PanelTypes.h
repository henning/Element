#pragma once

#include "ElementApp.h"

namespace Element {

namespace PanelIDs
{
    static const Identifier controllers         = "controllers";
    static const Identifier maps                = "maps";
    static const Identifier graphEditor         = "graphEditor";
    static const Identifier graphMixer          = "graphMixer";
    static const Identifier graphSettings       = "graphSettings";
    static const Identifier keymaps             = "keymaps";
    static const Identifier nodeChannelStrip    = "nodeChannelStrip";
    static const Identifier nodeEditor          = "nodeEditor";
    static const Identifier nodeMidi            = "nodeMidi";
    static const Identifier plugins             = "plugins";
    static const Identifier session             = "session";
    static const Identifier sessionSettings     = "sessionSettings";
    static const Identifier virtualKeyboard     = "virtualKeyboard";
}

/** A generic panel used for testing and development */
class GenericPanelType : public DockPanelType
{
public:
    int lastPanelNo = 0;
    static const Identifier genericType;

    inline void getAllTypes (OwnedArray<DockPanelInfo>& types) override
    {
        auto* type = types.add (new DockPanelInfo());
        type->identifier = genericType;
        type->name = "Generic";
        type->description = "A generic panel for development purposes";
    }

    DockPanel* createPanel (const Identifier& panelType) override;
};

/** Application level panels */
class ApplicationPanelType : public DockPanelType
{
public:
    int lastPanelNo = 0;
    static const Identifier genericType;

    inline void getAllTypes (OwnedArray<DockPanelInfo>& types) override
    {
        auto* type = types.add (new DockPanelInfo());
        type->identifier    = PanelIDs::virtualKeyboard;
        type->name          = "Virtual Keyboard";
        type->description   = "Embedded virtual keyboard which sends MIDI events to the Global MIDI input";

        type = types.add (new DockPanelInfo());
        type->identifier    = PanelIDs::graphMixer;
        type->name          = "Graph Mixer";
        type->description   = "A mixer where the channel strips represent a node on a graph";

        type = types.add (new DockPanelInfo());
        type->identifier    = PanelIDs::graphEditor;
        type->name          = "Graph Editor";
        type->description   = "The Graph Editor";

        type = types.add (new DockPanelInfo());
        type->identifier    = PanelIDs::nodeEditor;
        type->name          = "Node Editor";
        type->description   = "The Node Editor";

        type = types.add (new DockPanelInfo());
        type->identifier    = PanelIDs::nodeChannelStrip;
        type->name          = "Node Channel Strip";
        type->description   = "Displays a single channel strip for a given node";

        type = types.add (new DockPanelInfo());
        type->identifier    = PanelIDs::nodeMidi;
        type->name          = "MIDI";
        type->description   = "Displays MIDI properties for a Node";

        type = types.add (new DockPanelInfo());
        type->identifier    = PanelIDs::plugins;
        type->name          = "Plugins";
        type->description   = "Available plugins";

        type = types.add (new DockPanelInfo());
        type->identifier    = PanelIDs::session;
        type->name          = "Session";
        type->description   = "Displays all objects in the Session";

        type = types.add (new DockPanelInfo());
        type->identifier    = PanelIDs::sessionSettings;
        type->name          = "Session Settings";
        type->description   = "Session Settings";

        type = types.add (new DockPanelInfo());
        type->identifier    = PanelIDs::graphSettings;
        type->name          = "Graph Settings";
        type->description   = "Graph Settings";

        type = types.add (new DockPanelInfo());
        type->identifier    = PanelIDs::keymaps;
        type->name          = "Keymaps";
        type->description   = "Key Mappings";

        type = types.add (new DockPanelInfo());
        type->identifier    = PanelIDs::maps;
        type->name          = "Maps";
        type->description   = "MIDI Mappings";

        type = types.add (new DockPanelInfo());
        type->identifier    = PanelIDs::controllers;
        type->name          = "Controllers";
        type->description   = "Controller Device Management";
    }

    DockPanel* createPanel (const Identifier& panelType) override;
};

};
