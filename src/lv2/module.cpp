// Copyright 2014-2023 Kushview, LLC <info@kushview.net>
// SPDX-License-Identifier: GPL3-or-later

#include <lv2/ui/ui.h>
#include <lvtk/ext/idle.hpp>
#include <lv2/midi/midi.h>
#include <lvtk/ext/state.hpp>

#include "engine/portbuffer.hpp"
#include "lv2/module.hpp"
#include "lv2/workerfeature.hpp"

namespace element {

enum UIQuality
{
    UI_NO_SUPPORT = 0, // UI not supported
    UI_FULL_SUPPORT = 1, // UI directly embeddable (i.e. a juce component)
    UI_NATIVE_EMBED = 2 // Embeddable Native UI type
};

struct SortSupportedUIs
{
    // - a value of < 0 if the first comes before the second
    // - a value of 0 if the two objects are equivalent
    // - a value of > 0 if the second comes before the first
    int compareElements (const SupportedUI* first, const SupportedUI* second)
    {
        // noop
        return 0;
    }
};

namespace Callbacks {

inline unsigned uiSupported (const char* hostType, const char* uiType)
{
    if (strcmp (hostType, ELEMENT__JUCEUI) == 0)
    {
        if (strcmp (uiType, ELEMENT__JUCEUI) == 0)
            return UI_FULL_SUPPORT;
        else if (strcmp (uiType, LVTK_UI__NativeUI))
            return UI_NATIVE_EMBED;
        return 0;
    }

    return suil_ui_supported (hostType, uiType);
}

} // namespace Callbacks

class LV2Module::Private
{
public:
    Private (LV2Module& module)
        : owner (module) {}

    ~Private() {}

    LV2ModuleUI* createModuleUI (const SupportedUI& supportedUI)
    {
        auto* uiptr = new LV2ModuleUI (owner.getWorld(), owner);
        uiptr->ui = supportedUI.URI;
        uiptr->plugin = supportedUI.plugin;
        uiptr->containerType = supportedUI.container;
        uiptr->widgetType = supportedUI.widget;
        uiptr->bundlePath = supportedUI.bundle;
        uiptr->binaryPath = supportedUI.binary;
        uiptr->requireShow = supportedUI.useShowInterface;
        this->ui = uiptr;
        return uiptr;
    }

    void sendControlValues()
    {
        if (! ui && ! owner.onPortNotify)
            return;

        for (const auto* port : ports.getPorts())
        {
            if (PortType::Control != port->type)
                continue;

            auto* const buffer = buffers.getUnchecked (port->index);

            if (ui)
            {
                ui->portEvent ((uint32_t) port->index, sizeof (float), 0, buffer->getPortData());
            }

            if (owner.onPortNotify)
                owner.onPortNotify ((uint32_t) port->index, sizeof (float), 0, buffer->getPortData());
        }
    }

    static const void* getPortValue (const char* port_symbol, void* user_data, uint32_t* size, uint32_t* type)
    {
        LV2Module::Private* priv = static_cast<LV2Module::Private*> (user_data);
        int portIdx = -1;
        for (const auto* port : priv->ports.getPorts())
        {
            if (port->symbol == port_symbol && port->type == PortType::Control)
            {
                portIdx = port->index;
                break;
            }
        }

        if (portIdx >= 0)
        {
            if (auto* const buffer = priv->buffers[portIdx])
            {
                *size = sizeof (float);
                *type = priv->owner.map (LV2_ATOM__Float);
                return buffer->getPortData();
            }
        }

        *size = 0;
        *type = 0;

        return nullptr;
    }

    static void setPortValue (const char* port_symbol,
                              void* user_data,
                              const void* value,
                              uint32_t size,
                              uint32_t type)
    {
        auto* priv = (Private*) user_data;
        auto& plugin = priv->owner;
        lvtk::ignore (plugin);
        if (type != priv->owner.map (LV2_ATOM__Float))
            return;

        int portIdx = -1;
        const PortDescription* port = nullptr;
        for (const auto* p : priv->ports.getPorts())
        {
            port = p;
            if (port->symbol == port_symbol && port->type == PortType::Control)
            {
                portIdx = port->index;
                break;
            }
        }

        if (portIdx >= 0 && port)
        {
            if (auto* const buffer = priv->buffers[portIdx])
                buffer->setValue (*((float*) value));
        }
    }

private:
    friend class LV2Module;
    LV2Module& owner;
    PortList ports;
    ChannelConfig channels;

    String uri; ///< plugin URI
    String name; ///< Plugin name
    String author; ///< Plugin author name

    LV2ModuleUI::Ptr ui;

    HeapBlock<float> mins, maxes, defaults;
    OwnedArray<PortBuffer> buffers;

    LV2_Feature instanceFeature { LV2_INSTANCE_ACCESS_URI, nullptr };
};

LV2Module::LV2Module (World& world_, const void* plugin_)
    : instance (nullptr),
      plugin ((const LilvPlugin*) plugin_),
      world (world_),
      active (false),
      currentSampleRate (44100.0),
      numPorts (lilv_plugin_get_num_ports (plugin)),
      events (nullptr)
{
    priv = std::make_unique<Private> (*this);
    init();
}

LV2Module::~LV2Module()
{
    freeInstance();
    worker = nullptr;
}

void LV2Module::activatePorts()
{
    // noop
}

void LV2Module::init()
{
    events.reset (new RingBuffer (EL_LV2_RING_BUFFER_SIZE));
    evbufsize = jmax (evbufsize, static_cast<uint32> (EL_LV2_RING_BUFFER_SIZE));
    evbuf.realloc (evbufsize);
    evbuf.clear (evbufsize);

    notifications.reset (new RingBuffer (EL_LV2_RING_BUFFER_SIZE));
    ntbufsize = jmax (ntbufsize, static_cast<uint32> (EL_LV2_RING_BUFFER_SIZE));
    ntbuf.realloc (ntbufsize);
    ntbuf.clear (ntbufsize);

    // create and set default port values
    priv->mins.allocate (numPorts, true);
    priv->maxes.allocate (numPorts, true);
    priv->defaults.allocate (numPorts, true);

    lilv_plugin_get_port_ranges_float (plugin, priv->mins, priv->maxes, priv->defaults);

    // initialize each port
    for (uint32 p = 0; p < numPorts; ++p)
    {
        const LilvPort* port (lilv_plugin_get_port_by_index (plugin, p));

        // port type
        PortType type = PortType::Unknown;
        if (lilv_port_is_a (plugin, port, world.lv2_AudioPort))
            type = PortType::Audio;
        else if (lilv_port_is_a (plugin, port, world.lv2_AtomPort))
            type = PortType::Atom;
        else if (lilv_port_is_a (plugin, port, world.lv2_ControlPort))
            type = PortType::Control;
        else if (lilv_port_is_a (plugin, port, world.lv2_CVPort))
            type = PortType::CV;
        else if (lilv_port_is_a (plugin, port, world.lv2_EventPort))
            type = PortType::Event;

        const bool isInput (lilv_port_is_a (plugin, port, world.lv2_InputPort));
        LilvNode* nameNode = lilv_port_get_name (plugin, port);
        const String name = lilv_node_as_string (nameNode);
        lilv_node_free (nameNode);
        nameNode = nullptr;
        const String symbol = lilv_node_as_string (lilv_port_get_symbol (plugin, port));

        priv->ports.add (type, p, priv->ports.size (type, isInput), symbol, name, isInput);
        priv->channels.addPort (type, p, isInput);

        uint32 capacity = sizeof (float);
        uint32 dataType = 0;
        switch ((uint32) type.id())
        {
            case PortType::Control:
                capacity = sizeof (float);
                dataType = map (LV2_ATOM__Float);
                break;
            case PortType::Audio:
                capacity = sizeof (float);
                dataType = map (LV2_ATOM__Float);
                break;
            case PortType::Atom:
                capacity = EL_LV2_EVENT_BUFFER_SIZE;
                dataType = map (LV2_ATOM__Sequence);
                break;
            case PortType::Midi:
                capacity = sizeof (uint32);
                dataType = map (LV2_MIDI__MidiEvent);
                break;
            case PortType::Event:
                capacity = EL_LV2_EVENT_BUFFER_SIZE;
                dataType = map (LV2_EVENT__Event);
                break;
            case PortType::CV:
                capacity = sizeof (float);
                dataType = map (LV2_ATOM__Float);
                break;
        }

        PortBuffer* const buf = priv->buffers.add (
            new PortBuffer (isInput, type, dataType, capacity));

        if (type == PortType::Control)
            buf->setValue (priv->defaults[p]);
    }

    // // load related GUIs
#if 0
    if (auto* related = lilv_plugin_get_related (plugin, world.ui_UI))
    {
        LILV_FOREACH (nodes, iter, related)
        {
            const auto* res = lilv_nodes_get (related, iter);
            lilv_world_load_resource (world.getWorld(), res);
        }
        lilv_nodes_free (related);
    }
#endif
    // plugin URI
    priv->uri = String::fromUTF8 (lilv_node_as_string (lilv_plugin_get_uri (plugin)));

    // plugin name
    if (LilvNode* node = lilv_plugin_get_name (plugin))
    {
        priv->name = String::fromUTF8 (lilv_node_as_string (node));
        lilv_node_free (node);
    }

    // author name
    if (LilvNode* node = lilv_plugin_get_author_name (plugin))
    {
        priv->author = String::fromUTF8 (lilv_node_as_string (node));
        lilv_node_free (node);
    }
}

void LV2Module::loadDefaultState()
{
    if (instance == nullptr)
        return;

    auto* const map = (LV2_URID_Map*) world.getFeatures().getFeature (LV2_URID__map)->getFeature()->data;
    if (auto* uriNode = lilv_new_uri (world.getWorld(), priv->uri.toRawUTF8()))
    {
        if (auto* state = lilv_state_new_from_world (world.getWorld(), map, uriNode))
        {
            const LV2_Feature* const features[] = { nullptr };
            lilv_state_restore (state, instance, Private::setPortValue, priv.get(), LV2_STATE_IS_POD, features);
            lilv_state_free (state);
            priv->sendControlValues();
        }

        lilv_node_free (uriNode);
    }
}

String LV2Module::getStateString() const
{
    if (instance == nullptr)
        return String();

    auto* const map = (LV2_URID_Map*) world.getFeatures().getFeature (LV2_URID__map)->getFeature()->data;
    auto* const unmap = (LV2_URID_Unmap*) world.getFeatures().getFeature (LV2_URID__unmap)->getFeature()->data;
    const String descURI = "http://kushview.net/kv/state";

    String result;
    const LV2_Feature* const features[] = { nullptr };

    if (auto* state = lilv_state_new_from_instance (plugin, instance, map, 0, 0, 0, 0, Private::getPortValue, priv.get(),
                                                    LV2_STATE_IS_POD, // flags
                                                    features))
    {
        char* strState = lilv_state_to_string (world.getWorld(), map, unmap, state, descURI.toRawUTF8(), 0);
        result = String::fromUTF8 (strState);
        std::free (strState);

        lilv_state_free (state);
    }

    return result;
}

void LV2Module::setStateString (const String& stateStr)
{
    if (instance == nullptr)
        return;
    auto* const map = (LV2_URID_Map*) world.getFeatures().getFeature (LV2_URID__map)->getFeature()->data;
    auto* const unmap = (LV2_URID_Unmap*) world.getFeatures().getFeature (LV2_URID__unmap)->getFeature()->data;
    lvtk::ignore (unmap);
    if (auto* state = lilv_state_new_from_string (world.getWorld(), map, stateStr.toRawUTF8()))
    {
        const LV2_Feature* const features[] = { nullptr };
        lilv_state_restore (state, instance, Private::setPortValue, priv.get(), LV2_STATE_IS_POD, features);
        lilv_state_free (state);
        priv->sendControlValues();
    }
}

Result LV2Module::instantiate (double samplerate)
{
    freeInstance();
    jassert (instance == nullptr);
    currentSampleRate = samplerate;

    features.clearQuick();
    world.getFeatures (features);

    // check for a worker interface
    LilvNodes* nodes = lilv_plugin_get_extension_data (plugin);
    LILV_FOREACH (nodes, iter, nodes)
    {
        const LilvNode* node = lilv_nodes_get (nodes, iter);
        if (lilv_node_equals (node, world.work_interface))
        {
            worker = std::make_unique<WorkerFeature> (world.getWorkThread(), 1);
            features.add (worker->getFeature());
        }
    }
    lilv_nodes_free (nodes);
    nodes = nullptr;

    features.add (nullptr);
    instance = lilv_plugin_instantiate (plugin, samplerate, features.getRawDataPointer());
    if (instance == nullptr)
    {
        features.clearQuick();
        worker = nullptr;
        return Result::fail ("Could not instantiate plugin.");
    }

    if (const void* data = getExtensionData (LV2_WORKER__interface))
    {
        if (worker == nullptr)
            return Result::fail ("Could not get worker feature whereas extension data exists.");
        worker->setSize (2048);
        worker->setInterface (lilv_instance_get_handle (instance),
                              (LV2_Worker_Interface*) data);
    }
    else if (worker)
    {
        features.removeFirstMatchingValue (worker->getFeature());
        worker = nullptr;
    }

    loadDefaultState();
    startTimerHz (60);
    return Result::ok();
}

void LV2Module::activate()
{
    if (instance && ! active)
    {
        lilv_instance_activate (instance);
        activatePorts();
        active = true;
    }
}

void LV2Module::cleanup()
{
    if (instance != nullptr)
    {
        // TODO: Check if Lilv takes care of calling cleanup
    }
}

void LV2Module::deactivate()
{
    if (instance != nullptr && active)
    {
        lilv_instance_deactivate (instance);
        active = false;
    }
}

bool LV2Module::isActive() const
{
    return instance && active;
}

void LV2Module::freeInstance()
{
    stopTimer();
    if (instance != nullptr)
    {
        deactivate();
        worker = nullptr;
        auto* oldInstance = instance;
        instance = nullptr;
        lilv_instance_free (oldInstance);
    }
}

void LV2Module::setSampleRate (double newSampleRate)
{
    if (newSampleRate == currentSampleRate)
        return;

    if (instance != nullptr)
    {
        const bool wasActive = isActive();
        freeInstance();
        instantiate (newSampleRate);

        jassert (currentSampleRate == newSampleRate);

        if (wasActive)
            activate();
    }
}

void LV2Module::connectChannel (const PortType type, const int32 channel, void* data, const bool isInput)
{
    connectPort (priv->channels.getPort (type, channel, isInput), data);
}

void LV2Module::connectPort (uint32 port, void* data)
{
    lilv_instance_connect_port (instance, port, data);
}

String LV2Module::getURI() const { return priv->uri; }
String LV2Module::getName() const { return priv->name; }
String LV2Module::getAuthorName() const { return priv->author; }

const ChannelConfig& LV2Module::getChannelConfig() const
{
    return priv->channels;
}

String LV2Module::getClassLabel() const
{
    if (const LilvPluginClass* klass = lilv_plugin_get_class (plugin))
        if (const LilvNode* node = lilv_plugin_class_get_label (klass))
            return CharPointer_UTF8 (lilv_node_as_string (node));
    return String();
}

const void* LV2Module::getExtensionData (const String& uri) const
{
    return instance ? lilv_instance_get_extension_data (instance, uri.toUTF8())
                    : nullptr;
}

LV2Module::DataFunction LV2Module::getDataFunction() const noexcept
{
    if (! isLoaded())
        return nullptr;
    if (auto desc = lilv_instance_get_descriptor (instance))
        return desc->extension_data;
    return nullptr;
}

void* LV2Module::getHandle()
{
    return instance ? (void*) lilv_instance_get_handle (instance)
                    : nullptr;
}

uint32 LV2Module::getNumPorts() const { return numPorts; }

uint32 LV2Module::getNumPorts (PortType type, bool isInput) const
{
    return static_cast<uint32> (priv->ports.size (type, isInput));
}

const LilvPort* LV2Module::getPort (uint32 port) const
{
    return lilv_plugin_get_port_by_index (plugin, port);
}

const PortList& LV2Module::ports() const noexcept { return priv->ports; }

uint32 LV2Module::getMidiPort() const
{
    for (uint32 i = 0; i < getNumPorts(); ++i)
    {
        const LilvPort* port (getPort (i));
        if ((lilv_port_is_a (plugin, port, world.lv2_AtomPort) || lilv_port_is_a (plugin, port, world.lv2_EventPort)) && lilv_port_is_a (plugin, port, world.lv2_InputPort) && lilv_port_supports_event (plugin, port, world.midi_MidiEvent))
            return i;
    }

    return EL_INVALID_PORT;
}

uint32 LV2Module::getNotifyPort() const
{
    for (uint32 i = 0; i < numPorts; ++i)
    {
        const LilvPort* port (getPort (i));
        if (lilv_port_is_a (plugin, port, world.lv2_AtomPort) && lilv_port_is_a (plugin, port, world.lv2_OutputPort) && lilv_port_supports_event (plugin, port, world.midi_MidiEvent))
        {
            return i;
        }
    }

    return LV2UI_INVALID_PORT_INDEX;
}

const LilvPlugin* LV2Module::getPlugin() const { return plugin; }

const String LV2Module::getPortName (uint32 index) const
{
    auto port = priv->ports.getPort (index);
    return port.name;
}

void LV2Module::getPortRange (uint32 port, float& min, float& max, float& def) const
{
    if (port >= numPorts)
        return;

    min = priv->mins[port];
    max = priv->maxes[port];
    def = priv->defaults[port];
    if (std::isnan (def))
        def = min;
}

PortType LV2Module::getPortType (uint32 index) const
{
    auto desc = priv->ports.getPort (index);
    return desc.type >= PortType::Control && desc.type <= PortType::Unknown
               ? desc.type
               : PortType::Unknown;
}

ScalePoints LV2Module::getScalePoints (uint32 index) const
{
    ScalePoints sps;

    if (const auto* port = lilv_plugin_get_port_by_index (plugin, index))
    {
        if (auto* points = lilv_port_get_scale_points (plugin, port))
        {
            LILV_FOREACH (scale_points, iter, points)
            {
                const auto* point = lilv_scale_points_get (points, iter);
                sps.points.set (
                    String::fromUTF8 (lilv_node_as_string (lilv_scale_point_get_label (point))),
                    lilv_node_as_float (lilv_scale_point_get_value (point)));
            }

            lilv_scale_points_free (points);
        }
    }

    return sps;
}

bool LV2Module::isPortEnumerated (uint32 index) const
{
    if (const auto* const port = lilv_plugin_get_port_by_index (plugin, index))
        return lilv_port_has_property (plugin, port, world.lv2_enumeration);
    return false;
}

bool LV2Module::isLoaded() const { return instance != nullptr; }

static SupportedUI* createSupportedUI (const LilvPlugin* plugin, const LilvUI* ui)
{
    auto entry = new SupportedUI();
    entry->URI = lilv_node_as_uri (lilv_ui_get_uri (ui));
    entry->plugin = lilv_node_as_uri (lilv_plugin_get_uri (plugin));
    entry->bundle = lilv_file_uri_parse (
        lilv_node_as_uri (lilv_ui_get_bundle_uri (ui)), nullptr);
    entry->binary = lilv_file_uri_parse (
        lilv_node_as_uri (lilv_ui_get_binary_uri (ui)), nullptr);
    entry->useShowInterface = false;
    return entry;
}

bool LV2Module::hasEditor() const
{
    if (! supportedUIs.isEmpty())
        return true;

    LilvUIs* uis = lilv_plugin_get_uis (plugin);
    if (nullptr == uis)
        return false;

    auto& suplist = const_cast<OwnedArray<SupportedUI>&> (supportedUIs);

    LILV_FOREACH (uis, iter, uis)
    {
        const LilvUI* lui = lilv_uis_get (uis, iter);
        bool hasShow = false;
        bool hasIdle = false;

        // check for extension data
        {
            auto* uriNode = lilv_new_uri (world.getWorld(), lilv_node_as_uri (lilv_ui_get_uri (lui)));
            auto* extDataNode = lilv_new_uri (world.getWorld(), LV2_CORE__extensionData);
            auto* showNode = lilv_new_uri (world.getWorld(), LV2_UI__showInterface);
            auto* idleNode = lilv_new_uri (world.getWorld(), LV2_UI__idleInterface);

            if (auto* extNodes = lilv_world_find_nodes (world.getWorld(), uriNode, extDataNode, nullptr))
            {
                LILV_FOREACH (nodes, iter, extNodes)
                {
                    const auto* node = lilv_nodes_get (extNodes, iter);
                    if (lilv_node_equals (node, showNode))
                        hasShow = true;
                    else if (lilv_node_equals (node, idleNode))
                        hasIdle = true;
                }

                lilv_nodes_free (extNodes);
            }

            lilv_node_free (uriNode);
            lilv_node_free (extDataNode);
            lilv_node_free (showNode);
            lilv_node_free (idleNode);
        }

        // Check JUCE UI
        if (lilv_ui_is_a (lui, world.ui_JUCEUI))
        {
            auto* const s = suplist.add (createSupportedUI (plugin, lui));
            s->container = ELEMENT__JUCEUI;
            s->widget = ELEMENT__JUCEUI;
            continue;
        }

        const LilvNode* uitype = nullptr;

        // check if native UI
        if (lilv_ui_is_supported (lui, suil_ui_supported, world.getNativeWidgetType(), &uitype))
        {
            if (uitype != nullptr && lilv_node_is_uri (uitype))
            {
                auto* const s = suplist.add (createSupportedUI (plugin, lui));
                s->container = LVTK_UI__NativeUI;
                s->widget = String::fromUTF8 (lilv_node_as_uri (uitype));
                continue;
            }
        }

        // no UI this far, check show interface
        if (hasShow)
        {
            auto* const s = suplist.add (createSupportedUI (plugin, lui));
            s->useShowInterface = true;
            s->container = LV2_UI__showInterface;
            s->widget = LV2_UI__showInterface;
        }

        if (hasIdle)
        {
            lvtk::ignore (hasIdle);
        }
    }

    lilv_uis_free (uis);

    SortSupportedUIs sorter;
    suplist.sort (sorter, true);

#if JUCE_DEBUG
    for (const auto* const sui : supportedUIs)
    {
        DBG ("[element] lv2: supported ui: " << sui->URI);
        DBG ("[element] lv2: container: " << sui->container);
        DBG ("[element] lv2: widget: " << sui->widget);
        DBG ("[element] lv2: show: " << (int) sui->useShowInterface);
    }
#endif

    return ! supportedUIs.isEmpty();
}

void LV2Module::clearEditor()
{
    if (priv->ui)
    {
        auto ui = priv->ui;
        priv->ui = nullptr;
        ui = nullptr;
    }
}

PortBuffer* LV2Module::getPortBuffer (uint32 port) const
{
    jassert (port < numPorts);
    return priv->buffers.getUnchecked (port);
}

uint32 LV2Module::getPortIndex (const String& symbol) const
{
    for (const auto* port : priv->ports.getPorts())
        if (port->symbol == symbol)
            return static_cast<uint32> (port->index);
    return LV2UI_INVALID_PORT_INDEX;
}

LV2ModuleUI* LV2Module::createEditor()
{
    if (priv->ui)
        return priv->ui.get();

    LV2ModuleUI* instance = nullptr;

    for (const auto* const u : supportedUIs)
    {
        if (u->container == LVTK_UI__NativeUI)
            instance = priv->createModuleUI (*u);
        else if (u->useShowInterface)
            instance = priv->createModuleUI (*u);
        if (instance != nullptr)
            break;
    }

    if (instance != nullptr)
    {
        jassert (instance == priv->ui.get());
    }

    return instance;
}

void LV2Module::sendPortEvents()
{
    priv->sendControlValues();
}

bool LV2Module::isPortInput (uint32 index) const
{
    return lilv_port_is_a (plugin, getPort (index), world.lv2_InputPort);
}

bool LV2Module::isPortOutput (uint32 index) const
{
    return lilv_port_is_a (plugin, getPort (index), world.lv2_OutputPort);
}

void LV2Module::timerCallback()
{
    PortEvent ev;

    static const uint32 pnsize = sizeof (PortEvent);

    for (;;)
    {
        if (! notifications->canRead (pnsize))
            break;

        notifications->read (ev, false);
        if (ev.size > 0 && notifications->canRead (pnsize + ev.size))
        {
            notifications->advance (pnsize, false);
            notifications->read (ntbuf, ev.size, true);

            if (ev.protocol == 0)
            {
                if (auto ui = priv->ui)
                    ui->portEvent (ev.index, ev.size, ev.protocol, ntbuf.getData());
                if (onPortNotify)
                    onPortNotify (ev.index, ev.size, ev.protocol, ntbuf.getData());
            }
        }
    }
}

void LV2Module::referAudioReplacing (AudioSampleBuffer& audio, AudioSampleBuffer& cv)
{
    // Audio
    for (int c = 0; c < priv->channels.getNumAudioInputs(); ++c)
        priv->buffers.getUnchecked ((int) priv->channels.getPort (
                                        PortType::Audio, c, true))
            ->referTo (audio.getWritePointer (c));

    for (int c = 0; c < priv->channels.getNumAudioOutputs(); ++c)
        priv->buffers.getUnchecked ((int) priv->channels.getPort (
                                        PortType::Audio, c, false))
            ->referTo (audio.getWritePointer (c));

    // CV
    for (int c = 0; c < priv->channels.getNumCVInputs(); ++c)
        priv->buffers.getUnchecked ((int) priv->channels.getPort (
                                        PortType::CV, c, true))
            ->referTo (cv.getWritePointer (c));

    for (int c = 0; c < priv->channels.getNumCVOutputs(); ++c)
        priv->buffers.getUnchecked ((int) priv->channels.getPort (
                                        PortType::CV, c, false))
            ->referTo (cv.getWritePointer (c));
}

void LV2Module::run (uint32 nframes)
{
    PortEvent ev;

    static const uint32 pesize = sizeof (PortEvent);

    for (;;)
    {
        if (! events->canRead (pesize))
            break;
        events->read (ev, false);
        if (ev.size > 0 && events->canRead (pesize + ev.size))
        {
            events->advance (pesize, false);
            events->read (evbuf, ev.size, true);

            // DBG ("[element] lv2: read bytes: " << (int) ev.size);

            if (ev.protocol == 0)
            {
                auto* buffer = priv->buffers.getUnchecked (ev.index);
                const float value = *((float*) evbuf.getData());
                if (buffer->getValue() != value)
                {
                    buffer->setValue (value);
                    if (notifications->canWrite (pesize + ev.size))
                    {
                        notifications->write (ev);
                        notifications->write (evbuf.getData(), ev.size);
                    }
                }
            }
        }
    }

    for (int i = priv->buffers.size(); --i >= 0;)
    {
        auto buffer = priv->buffers.getUnchecked (i);
        if (buffer->isSequence() && ! buffer->isInput())
            buffer->reset();
        connectPort (static_cast<uint32> (i), buffer->getPortData());
    }

    if (worker)
        worker->processWorkResponses();

    lilv_instance_run (instance, nframes);

    if (worker)
        worker->endRun();
}

uint32 LV2Module::map (const String& uri) const
{
    // FIXME: const in SymbolMap::map/unmap
    return (const_cast<World*> (&world))->map (uri);
}

void LV2Module::write (uint32 port, uint32 size, uint32 protocol, const void* buffer)
{
    PortEvent event;
    zerostruct (event);
    event.index = port;
    event.size = size;
    event.protocol = protocol;

    if (events->canWrite (sizeof (PortEvent) + size))
    {
        events->write (event);
        events->write (buffer, event.size);
    }
    else
    {
        DBG ("lv2 plugin write buffer full.");
    }
}

} // namespace element
