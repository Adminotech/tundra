/**
    For conditions of distribution and use, see copyright notice in LICENSE

    @file   FrameworkFwd.h
    @brief  Forward declarations and type defines for commonly used Framework classes.
    @todo   Create single TundraCoreFwd.h which fwd decls all TundraCore classes and types. */

#pragma once

#include "CoreTypes.h"

class Framework;
class StaticPluginRegistry;
class Application;
class RaycastResult;
class IRenderer;
class Profiler;
class ProfilerQObj;
class IModule;
class Color;
class Transform;
class Exception;
class FrameAPI;
class ConfigAPI;
class PluginAPI;
class IArgumentType;
typedef shared_ptr<IArgumentType> ArgumentTypePtr;
typedef QList<ArgumentTypePtr > ArgumentTypeList;

// The following are external to Framework
class UiAPI;
class InputAPI;
class AudioAPI;
class AssetAPI;
class ConsoleAPI;
class SceneAPI;
