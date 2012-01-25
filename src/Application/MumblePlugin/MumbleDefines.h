// For conditions of distribution and use, see copyright notice in LICENSE

#pragma once

#include "MumbleNetwork.h"

#include <QString>
#include <deque>
#include <map>

class SoundBuffer;

struct MumblePluginState
{
    bool serverSynced;
    uint sessionId;
    MumbleNetwork::ConnectionState connectionState;
    MumbleNetwork::NetworkMode networkMode;
    QString username;
    QString address;
    ushort port;

    bool outputAudioMuted;
    bool inputAudioMuted;

    QString fullChannelName;

    void Reset()
    {
        serverSynced = false;
        sessionId = 0;
        connectionState = MumbleNetwork::MumbleDisconnected;
        networkMode = MumbleNetwork::MumbleTCPMode;
        username = "";
        address = "";
        port = 0;
        outputAudioMuted = true;
        inputAudioMuted = true;
        fullChannelName = "";
    }

    QString FullHost() 
    { 
        if (address.isEmpty() || port == 0) 
            return ""; 
        else
            return QString("%1:%2").arg(address).arg(port); 
    }

    QString PortToString() 
    { 
        if (port == 0) 
            return ""; 
        else 
            return QString::number(port); 
    }
};

namespace MumbleAudio
{
    enum AudioQuality
    {
        QualityNotSet,
        QualityLow,
        QualityBalanced,
        QualityUltra
    };

    static int MUMBLE_AUDIO_SAMPLE_RATE = 48000;
    static int MUMBLE_AUDIO_SAMPLE_WIDTH = 16;
    static int MUMBLE_AUDIO_SAMPLES_IN_FRAME = 480;

    static int MUMBLE_AUDIO_QUALITY_LOW = 16000;
    static int MUMBLE_AUDIO_QUALITY_BALANCED = 40000;
    static int MUMBLE_AUDIO_QUALITY_ULTRA = 72000;
    static int MUMBLE_AUDIO_FRAMES_PER_PACKET_LOW = 6; // mumble original 6
    static int MUMBLE_AUDIO_FRAMES_PER_PACKET_BALANCED = 5; // mumble original 2
    static int MUMBLE_AUDIO_FRAMES_PER_PACKET_ULTRA = 1; // mumble original 1

    typedef std::deque<SoundBuffer> AudioFrameDeque;
    typedef std::map<uint, AudioFrameDeque > AudioFrameMap;
}

#ifdef Q_OS_WIN
#define MUMBLE_STACKVAR(type, varname, count) type *varname=reinterpret_cast<type *>(_alloca(sizeof(type) * (count)))
#else
#define MUMBLE_STACKVAR(type, varname, count) type varname[count]
#endif
