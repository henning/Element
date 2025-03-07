// Copyright 2023 Kushview, LLC <info@kushview.net>
// SPDX-License-Identifier: GPL3-or-later

#pragma once

#include "nodes/scriptnode.hpp"
#include <element/ui/nodeeditor.hpp>
#include "ui/luaconsole.hpp"
#include "ui/luatokenizer.hpp"

namespace element {

class ScriptingEngine;
class ScriptNodeEditor : public NodeEditor,
                         public ChangeListener
{
public:
    explicit ScriptNodeEditor (ScriptingEngine& scripts, const Node& node);
    ~ScriptNodeEditor() override;

    void resized() override;
    void paint (Graphics&) override;

    void setToolbarVisible (bool);
    void changeListenerCallback (ChangeBroadcaster*) override;

private:
    ScriptingEngine& engine;
    sol::state_view state;
    sol::environment env;
    sol::table widget;
    Component* comp = nullptr;

    bool showToolbar = false;
    TextButton paramsButton;

    PropertyPanel props;
    int propsWidth = 220,
        propsGap = 2;

    SignalConnection portsChangedConnection;
    ScriptNode::Ptr lua;

    FileBrowserComponent fileBrowser;
    std::unique_ptr<FileChooser> chooser;

    void updateAll();
    void updatePreview();
    void updateCodeEditor();
    void updateProperties();
    void updateSize();
    void onPortsChanged();
    sol::table createContext();
    void log (const String& txt) { Logger::writeToLog (txt); }
};

} // namespace element
