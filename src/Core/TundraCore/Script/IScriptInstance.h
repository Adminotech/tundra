/**
    For conditions of distribution and use, see copyright notice in LICENSE

    @file   IScriptInterface.h
    @brief  Interface for different script instances, e.g. Javascript of Python. */

#pragma once

#include "TundraCoreApi.h"

#include <QObject>
#include <QMap>
#include <QString>

/// Interface for different script instances, e.g. Javascript or Python.
class TUNDRACORE_API IScriptInstance : public QObject
{
    Q_OBJECT

public:
    /// Default constuctor.
    IScriptInstance() : trusted_(false) {}

    /// Destructor.
    virtual ~IScriptInstance() {}

    /// Loads this script instance.
    virtual void Load() = 0;

    /// Unloads this script instance.
    virtual void Unload() = 0;

    /// Starts this script instance.
    virtual void Run() = 0;

    /// Return whether the script has been run.
    virtual bool IsEvaluated() const = 0;

public slots:
    /// Dumps engine information into a string. Used for debugging/profiling.
    virtual QMap<QString, uint> DumpEngineInformation() = 0;

protected:
    /// Whether this instance executed trusted code or not. 
    /** By default everything loaded remotely (with e.g. http) is untrusted,
        and not exposed anything with system access.
        With qt/javascript means that can not load qt dlls to get qt networking etc.,
        and with python loading remote code is not allowed at all (cpython always has system access). */
    bool trusted_;
};
