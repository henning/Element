// Copyright 2023 Kushview, LLC <info@kushview.net>
// SPDX-License-Identifier: GPL3-or-later

#pragma once

#include <element/juce/core.hpp>
#include <element/appinfo.hpp>
#include <element/audioengine.hpp>
#include <element/session.hpp>

namespace element {

class Services;
class CommandManager;
class DeviceManager;
class MappingEngine;
class MidiEngine;
class ScriptingEngine;
class Log;
class PluginManager;
class PresetManager;
class Settings;

class Context {
public:
    explicit Context (const juce::String& commandLine = juce::String());
    virtual ~Context();

    Settings& getSettings();
    Log& logger();
    Services& services();
    AudioEnginePtr audio() const;
    void setEngine (AudioEnginePtr engine);
    MidiEngine& midi();
    MappingEngine& mapping();
    DeviceManager& devices();
    PluginManager& plugins();
    PresetManager& presets();
    ScriptingEngine& scripting();
    SessionPtr session();
    CommandManager& getCommandManager();
    const std::string& getAppName() const { return appName; }

    //=========================================================================
    void openModule (const std::string& path);
    void loadModules();
    void addModulePath (const std::string& path);
    void discoverModules();

private:
    friend class Application;
    std::string appName;
    class Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace element
