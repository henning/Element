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
#include "services/engineservice.hpp"
#include "services/deviceservice.hpp"
#include "services/mappingservice.hpp"
#include <element/services/guiservice.hpp>
#include "services/presetservice.hpp"
#include "services/sessionservice.hpp"

#include <element/ui/content.hpp>

#include <element/node.hpp>
#include <element/context.hpp>
#include <element/settings.hpp>

namespace element {

class SessionService::ChangeResetter : public AsyncUpdater
{
public:
    explicit ChangeResetter (SessionService& sc) : owner (sc) {}
    ~ChangeResetter() = default;

    void handleAsyncUpdate() override
    {
        owner.resetChanges (false);
        jassert (! owner.hasSessionChanged());
    }

private:
    SessionService& owner;
};

SessionService::SessionService() {}
SessionService::~SessionService() {}

void SessionService::activate()
{
    currentSession = context().session();
    document.reset (new SessionDocument (currentSession));
    changeResetter.reset (new ChangeResetter (*this));
}

void SessionService::deactivate()
{
    auto& world = context();
    auto& settings (world.getSettings());
    auto* props = settings.getUserSettings();

    if (document)
    {
        if (document->getFile().existsAsFile())
            props->setValue (Settings::lastSessionKey, document->getFile().getFullPathName());
        document = nullptr;
    }

    changeResetter->cancelPendingUpdate();
    changeResetter.reset (nullptr);

    currentSession->clear();
    currentSession = nullptr;
}

void SessionService::openDefaultSession()
{
    if (auto* gc = sibling<GuiService>())
        gc->closeAllPluginWindows();

    loadNewSessionData();
    refreshOtherControllers();
    sibling<GuiService>()->stabilizeContent();
    resetChanges (true);
}

void SessionService::openFile (const File& file)
{
    bool didSomething = true;

    if (file.hasFileExtension ("elg"))
    {
        const ValueTree node (Node::parse (file));
        if (Node::isProbablyGraphNode (node))
        {
            const Node model (node, true);
            model.forEach ([] (const ValueTree& tree) {
                if (! tree.hasType (Tags::node))
                    return;
                auto nodeRef = tree;
                nodeRef.setProperty (Tags::uuid, Uuid().toString(), nullptr);
            });
            if (auto* ec = sibling<EngineService>())
                ec->addGraph (model);
        }
    }
    else if (file.hasFileExtension ("els"))
    {
        document->saveIfNeededAndUserAgrees();
        Session::ScopedFrozenLock freeze (*currentSession);
        Result result = document->loadFrom (file, true);

        if (result.wasOk())
        {
            auto& gui = *sibling<GuiService>();
            gui.closeAllPluginWindows();
            refreshOtherControllers();

            if (auto* cc = gui.getContentComponent())
            {
                auto ui = currentSession->getValueTree().getOrCreateChildWithName (Tags::ui, nullptr);
                cc->applySessionState (ui.getProperty ("content").toString());
            }

            sibling<GuiService>()->stabilizeContent();
            resetChanges();
        }

        jassert (! hasSessionChanged());
    }
    else
    {
        didSomething = false;
    }

    if (didSomething)
    {
        if (auto* gc = sibling<GuiService>())
            gc->stabilizeContent();
        changeResetter->triggerAsyncUpdate();
    }
}

void SessionService::exportGraph (const Node& node, const File& targetFile)
{
    if (! node.hasNodeType (Tags::graph))
    {
        jassertfalse;
        return;
    }

    TemporaryFile tempFile (targetFile);
    if (node.writeToFile (tempFile.getFile()))
        tempFile.overwriteTargetFileWithTemporary();
}

void SessionService::importGraph (const File& file)
{
    openFile (file);
}

void SessionService::closeSession()
{
    DBG ("[SC] close session");
}

void SessionService::resetChanges (const bool resetDocumentFile)
{
    jassert (document);
    if (resetDocumentFile)
        document->setFile ({});
    document->setChangedFlag (false);
    jassert (! document->hasChangedSinceSaved());
}

void SessionService::saveSession (const bool saveAs, const bool askForFile, const bool showError)
{
    jassert (document && currentSession);
    auto result = FileBasedDocument::userCancelledSave;

    auto& gui = *sibling<GuiService>();

    if (auto* cc = gui.getContentComponent())
    {
        String state;
        cc->getSessionState (state);
        auto ui = currentSession->getValueTree().getOrCreateChildWithName (Tags::ui, nullptr);
        ui.setProperty ("content", state, nullptr);
    }

    if (saveAs)
    {
        result = document->saveAsInteractive (true);
    }
    else
    {
        result = document->save (askForFile, showError);
    }

    if (result == FileBasedDocument::userCancelledSave)
        return;

    if (result == FileBasedDocument::savedOk)
    {
        // ensure change messages are flushed so the changed flag doesn't reset
        currentSession->dispatchPendingMessages();
        document->setChangedFlag (false);
        jassert (! hasSessionChanged());
        if (auto* us = context().getSettings().getUserSettings())
            us->setValue (Settings::lastSessionKey, document->getFile().getFullPathName());
        if (saveAs)
        {
            services().addRecentFile (document->getFile());
            currentSession->getValueTree().setProperty (Tags::name,
                                                        document->getFile().getFileNameWithoutExtension(),
                                                        nullptr);
        }
    }
}

void SessionService::newSession()
{
    jassert (document && currentSession);
    // - 0 if the third button was pressed ('cancel')
    // - 1 if the first button was pressed ('yes')
    // - 2 if the middle button was pressed ('no')
    int res = 2;
    if (document->hasChangedSinceSaved())
        res = AlertWindow::showYesNoCancelBox (AlertWindow::InfoIcon, "Save Session?", "The current session has changes. Would you like to save it?", "Save Session", "Don't Save", "Cancel");
    if (res == 1)
        document->save (true, true);

    if (res == 1 || res == 2)
    {
        sibling<GuiService>()->closeAllPluginWindows();
        loadNewSessionData();
        refreshOtherControllers();
        sibling<GuiService>()->stabilizeContent();
        resetChanges (true);
    }
}

void SessionService::loadNewSessionData()
{
    currentSession->clear();
    const auto file = context().getSettings().getDefaultNewSessionFile();
    bool wasLoaded = false;

    if (file.existsAsFile())
    {
        ValueTree data;
        if (auto xml = XmlDocument::parse (file))
            data = ValueTree::fromXml (*xml);
        if (data.isValid() && data.hasType (Tags::session))
            wasLoaded = currentSession->loadData (data);
    }

    if (! wasLoaded)
    {
        currentSession->clear();
        currentSession->addGraph (
            Node::createDefaultGraph ("Graph"), true);
    }
}

void SessionService::refreshOtherControllers()
{
    sibling<EngineService>()->sessionReloaded();
    sibling<DeviceService>()->refresh();
    sibling<MappingService>()->learn (false);
    sibling<PresetService>()->refresh();
    sessionLoaded();
}

} // namespace element
