// For conditions of distribution and use, see copyright notice in LICENSE

#include "StableHeaders.h"
#include "MumblePlugin.h"
#include "MumbleNetwork.h"
#include "MumbleNetworkHandler.h"
#include "MumbleData.h"
#include "AudioProcessor.h"

#include "Mumble.pb.h"

#include "Framework.h"
#include "Profiler.h"
#include "Application.h"
#include "AudioAPI.h"
#include "ConsoleAPI.h"
#include "ConfigAPI.h"
#include "LoggingFunctions.h"

#include "IRenderer.h"
#include "Scene.h"
#include "Entity.h"
#include "EC_Placeable.h"
#include "EC_SoundListener.h"

#include "JavascriptModule.h"
#include "MumbleScriptTypeDefines.h"

using namespace MumbleNetwork;
using namespace MumbleAudio;

MumblePlugin::MumblePlugin() :
    IModule("MumblePlugin"),
    LC("[MumblePlugin]: "),
    network_(0),
    audio_(0)
{
    state.Reset();
}

MumblePlugin::~MumblePlugin()
{
    QSslSocket::setDefaultCaCertificates(QList<QSslCertificate>()); // Hides a qt memory leak
}

void MumblePlugin::Initialize()
{
    RegisterMumblePluginMetaTypes();

    // Register custom QObject and enum meta types to created script engines.
    JavascriptModule *javascriptModule = framework_->GetModule<JavascriptModule>();
    if (javascriptModule)
        connect(javascriptModule, SIGNAL(ScriptEngineCreated(QScriptEngine*)), SLOT(OnScriptEngineCreated(QScriptEngine*)));
    else
        LogWarning(LC + "JavascriptModule not present, MumblePlugin usage from scripts will be limited!");

    framework_->RegisterDynamicObject("mumble", this);

    qobjTimerId_ = startTimer(15);

    /// @todo Remove everything below, was used for early development
    framework_->Console()->RegisterCommand("mumbleconnect", "", this, SLOT(DebugConnect()));
    framework_->Console()->RegisterCommand("mumbledisconnect", "", this, SLOT(Disconnect()));
    framework_->Console()->RegisterCommand("mumblejoin", "", this, SLOT(JoinChannel(QString)));
    framework_->Console()->RegisterCommand("mumbleselfmute", "", this, SLOT(DebugMute()));
    framework_->Console()->RegisterCommand("mumbleselfunmute", "", this, SLOT(DebugUnMute()));
    framework_->Console()->RegisterCommand("mumblemute", "", this, SLOT(DebugMute(QString )));
    framework_->Console()->RegisterCommand("mumbledeaf", "", this, SLOT(DebugDeaf()));
    framework_->Console()->RegisterCommand("mumbleundeaf", "", this, SLOT(DebugUnDeaf()));
    framework_->Console()->RegisterCommand("mumblewizard", "", this, SLOT(RunAudioWizard()));
    DebugConnect(); 
}

void MumblePlugin::Uninitialize()
{
    killTimer(qobjTimerId_);
    Disconnect("Client exiting.");
}

void MumblePlugin::timerEvent(QTimerEvent *event)
{
    if (event->timerId() != qobjTimerId_)
        return;

    PROFILE(MumblePlugin_Update)

    // Audio processing
    if (audio_ && state.serverSynced && state.connectionState == MumbleConnected)
    {
        // Output audio
        if (!state.outputAudioMuted)
        {
            PROFILE(MumblePlugin_Update_ProcessOutputAudio)
            VoicePacketInfo packetInfo(audio_->ProcessOutputAudio());
            ELIFORP(MumblePlugin_Update_ProcessOutputAudio)
            if (!packetInfo.encodedFrames.isEmpty())
            {
                PROFILE(MumblePlugin_Update_ProcessOutputNetwork)
                if (network_)
                {
                    packetInfo.isLoopBack = state.outputAudioLoopBack;
                    if (!packetInfo.isLoopBack && audioWizard)
                        packetInfo.isLoopBack = true;
                    if (state.outputPositional)
                    {
                        // Sets packetInfo.isPositional false if no active EC_SoundListener is found.
                        // If found updates packetInfo and our MumbleUser position.
                        UpdatePositionalInfo(packetInfo); 
                    }
                    network_->SendVoicePacket(packetInfo);
                }
                else
                    LogError(LC + "Network ptr is null while sending out voice data!");
                ELIFORP(MumblePlugin_Update_ProcessOutputNetwork)
            }

            if (audioWizard)
                audioWizard->SetLevels(audio_->levelPeakMic, audio_->isSpeech);
        }
        else
            audio_->ClearOutputAudio();

        PROFILE(MumblePlugin_Update_ProcessInputAudio)
        // Input audio
        if (!state.inputAudioMuted)
        {
            MumbleChannel *c = ChannelForUser(state.sessionId);
            audio_->PlayInputAudio(c != 0 ? c->MutedUserIds() : QList<uint>());
        }
        else
            audio_->ClearInputAudio();
        ELIFORP(MumblePlugin_Update_ProcessInputAudio)
    }
    else if (audio_)
    {
        audio_->ClearInputAudio();
        audio_->ClearOutputAudio();
    }

    ELIFORP(MumblePlugin_Update)
}

void MumblePlugin::Connect(QString address, int port, QString username, QString password, QString fullChannelName, bool outputAudioMuted, bool inputAudioMuted)
{    
    Disconnect();

    state.address = address;
    state.port = (ushort)port;
    state.username = username;
    state.fullChannelName = fullChannelName;
    state.outputAudioMuted = outputAudioMuted;
    state.inputAudioMuted = inputAudioMuted;

    LogInfo(LC + "Connecting to " + state.FullHost() + " as \"" + state.username + "\"");

    audio_ = new MumbleAudio::AudioProcessor(framework_, LoadSettings());
    audio_->moveToThread(audio_);

    network_ = new MumbleNetworkHandler(state.address, state.port, state.username, password);
    network_->codecBitStreamVersion = audio_->CodecBitStreamVersion();
    network_->moveToThread(network_);

    // Handle signals from network thread to the main thread.
    connect(network_, SIGNAL(Connected(QString, int, QString)), SLOT(OnConnected(QString, int, QString)), Qt::QueuedConnection);
    connect(network_, SIGNAL(Disconnected(QString)), SLOT(OnDisconnected(QString)), Qt::QueuedConnection);
    connect(network_, SIGNAL(StateChange(MumbleNetwork::ConnectionState)), SLOT(OnStateChange(MumbleNetwork::ConnectionState)), Qt::QueuedConnection);
    connect(network_, SIGNAL(ServerSynced(uint)), SLOT(OnServerSynced(uint)), Qt::QueuedConnection);
    connect(network_, SIGNAL(NetworkModeChange(MumbleNetwork::NetworkMode, QString)), SLOT(OnNetworkModeChange(MumbleNetwork::NetworkMode, QString)), Qt::QueuedConnection);
    
    connect(network_, SIGNAL(ConnectionRejected(MumbleNetwork::RejectReason, QString)), SLOT(OnConnectionRejected(MumbleNetwork::RejectReason, QString)), Qt::QueuedConnection);
    connect(network_, SIGNAL(PermissionDenied(MumbleNetwork::PermissionDeniedType, MumbleNetwork::ACLPermission, uint, uint, QString)), 
        SLOT(OnPermissionDenied(MumbleNetwork::PermissionDeniedType, MumbleNetwork::ACLPermission, uint, uint, QString)), Qt::QueuedConnection);

    connect(network_, SIGNAL(TextMessageReceived(bool, QList<uint>, uint, QString)), SLOT(OnTextMessageReceived(bool, QList<uint>, uint, QString)), Qt::QueuedConnection);
    connect(network_, SIGNAL(ChannelUpdate(uint, uint, QString, QString)), SLOT(OnChannelUpdate(uint, uint, QString, QString)), Qt::QueuedConnection);
    connect(network_, SIGNAL(ChannelRemoved(uint)), SLOT(OnChannelRemoved(uint)), Qt::QueuedConnection);
    connect(network_, SIGNAL(UserLeft(uint, uint, bool, bool, QString)), SLOT(OnUserLeft(uint, uint, bool, bool, QString)), Qt::QueuedConnection);
    connect(network_, SIGNAL(UserUpdate(uint, uint, QString, QString, QString, bool, bool, bool)), 
        SLOT(OnUserUpdate(uint, uint, QString, QString, QString, bool, bool, bool)), Qt::QueuedConnection);

    // Handle audio signals from network thread to audio thread.
    connect(network_, SIGNAL(AudioReceived(uint, QList<QByteArray>)), audio_, SLOT(OnAudioReceived(uint, QList<QByteArray>)), Qt::QueuedConnection);
    
    audio_->start();
    network_->start();
}

void MumblePlugin::Disconnect(QString reason)
{
    foreach(MumbleUser *pu, pendingUsers_)
        SAFE_DELETE(pu);
    pendingUsers_.clear();
    
    // Note that this frees all channel users as well.
    foreach(MumbleChannel *c, channels_)
        SAFE_DELETE(c);
    channels_.clear();

    if (audioWizard)
        delete audioWizard;

    if (audio_ && audio_->isRunning())
    {
        audio_->exit();
        audio_->wait();
    }
    SAFE_DELETE(audio_);

    state.serverSynced = false;
    if (network_ && network_->isRunning())
    {
        network_->exit();
        network_->wait();
        
        if (!reason.isEmpty())
            LogInfo(LC + "Disconnected from " + state.FullHost() + ": " + reason);
        else
            LogInfo(LC + "Disconnected from " + state.FullHost());
        emit Disconnected(reason);

        QCoreApplication::instance()->processEvents();
    }
    SAFE_DELETE(network_);

    state.Reset();
}

bool MumblePlugin::SendTextMessage(const QString &message)
{
    if (!state.serverSynced || state.connectionState != MumbleConnected || !network_)
    {
        LogError(LC + "Cannot send text message, not connected to a server");
        return false;
    }

    MumbleUser *me = Me();
    if (me)
    {
        MumbleChannel *channel = me->Channel();
        if (channel)
        {
            MumbleProto::TextMessage messageText;
            messageText.add_channel_id(channel->id);
            messageText.set_message(utf8(message));
            network_->SendTCP(TextMessage, messageText);
            return true;
        }
        else
            LogError(LC + "Could not find our current channel from the current state to send the text message.");
    }
    else
        LogError(LC + "Could not find our user from the current state to send the text message.");

    return false;
}

bool MumblePlugin::SendTextMessage(uint userId, const QString &message)
{
    if (!state.serverSynced || state.connectionState != MumbleConnected || !network_)
    {
        LogError(LC + "Cannot send text message to user with id " + QString::number(userId) + ", not connected to a server");
        return false;
    }

    MumbleUser *target = User(userId);
    if (target)
    {
        if (target->id != state.sessionId)
        {
            MumbleProto::TextMessage messageText;
            messageText.add_session(target->id);
            messageText.set_message(utf8(message));
            network_->SendTCP(TextMessage, messageText);
            return true;
        }
        else
            LogError(LC + "Cannot sent text message to own user id " + QString::number(userId));
    }
    else
        LogError(LC + "Could not find user with id " + QString::number(userId) + "from the server to send the text message.");
        
    return false;
}

bool MumblePlugin::JoinChannel(QString fullName)
{
    if (state.connectionState != MumbleConnected || !network_)
    {
        LogError(LC + "Could not find channel with full name \"" + fullName + "\" not connected to a server.");
        return false;
    }

    MumbleChannel *channel = Channel(fullName);
    if (channel)
        return JoinChannel(channel->id);
    else
    {
        QString reason = "Channel with full name \"" + fullName + "\" does not exist, cannot join.";
        emit JoinChannelFailed(reason);
        LogError(LC + reason);
        return false;
    }
}

bool MumblePlugin::JoinChannel(uint id)
{
    if (state.connectionState != MumbleConnected || !network_)
    {
        LogError(LC + "Could not find channel with id\"" + QString::number(id) + "\" for join operation.");
        return false;
    }

    MumbleChannel *channel = Channel(id);
    if (channel)
    {
        MumbleProto::UserState message;
        message.set_session(state.sessionId);
        message.set_channel_id(id);

        network_->SendTCP(UserState, message);
        return true;
    }
    else
    {
        QString reason = "Channel with id \"" + QString::number(id) + "\" does not exist, cannot join.";
        emit JoinChannelFailed(reason);
        LogError(LC + reason);
        return false;
    }
}

MumbleChannel* MumblePlugin::Channel(uint id)
{
    MumbleChannel *channel = 0;
    foreach(MumbleChannel *iter, channels_)
    {
        if (iter->id == id)
        {
            channel = iter;
            break;
        }
    }
    return channel;
}

MumbleChannel* MumblePlugin::Channel(QString fullName)
{
    MumbleChannel *channel = 0;
    foreach(MumbleChannel *iter, channels_)
    {
        if (QString::compare(iter->fullName, fullName, Qt::CaseSensitive) == 0)
        {
            channel = iter;
            break;
        }
    }
    return channel;
}

MumbleChannel *MumblePlugin::ChannelForUser(uint userId)
{
    MumbleChannel *channel = 0;
    foreach(MumbleChannel *iter, channels_)
    {
        if (iter->User(userId))
        {
            channel = iter;
            break;
        }
    }
    return channel;
}

MumbleUser* MumblePlugin::User(uint userId)
{
    MumbleUser *user = 0;
    foreach(MumbleChannel *iter, channels_)
    {
        user = iter->User(userId);
        if (user)
            break;
    }
    return user;
}

MumbleUser* MumblePlugin::Me()
{
    if (!state.serverSynced)
        return 0;
    return User(state.sessionId);
}

void MumblePlugin::SetMuted(uint userId, bool muted)
{
    if (!state.serverSynced || !network_)
        return;
    if (userId == state.sessionId)
        return;

    MumbleUser *user = User(userId);
    if (user)
    {
        if (user->isMuted == muted)
            return;
        
        user->isMuted = muted;
        user->EmitMuted();
        emit UserMuted(user, user->isMuted);
    }
    else
        LogError(LC + QString("Cannot mute user with id %1, no such user!").arg(userId));
}

void MumblePlugin::Mute(uint userId)
{
    SetMuted(userId, true);
}

void MumblePlugin::UnMute(uint userId)
{
    SetMuted(userId, false);
}

MumbleNetwork::ConnectionState MumblePlugin::State()
{
    return state.connectionState;
}

MumbleNetwork::NetworkMode MumblePlugin::NetworkMode()
{
    if (state.connectionState == MumbleConnected)
        return state.networkMode;
    else 
        return MumbleNetwork::NetworkModeNotSet;
}

void MumblePlugin::SetOutputAudioMuted(bool outputAudioMuted)
{
    if (state.serverSynced)
    {
        MumbleUser *me = User(state.sessionId);
        if (!me)
        {
            LogError(LC + "Could not find own user ptr to set audio output muted state!");
            return;
        }

        if (audio_)
            audio_->SetOutputAudioMuted(outputAudioMuted);

        if (network_)
        {
            // Avoid unwanted network traffic and check value actually changed.
            if (me->isSelfMuted != outputAudioMuted)
            {
                MumbleProto::UserState message;
                message.set_session(state.sessionId);
                message.set_self_deaf(state.inputAudioMuted);
                message.set_self_mute(outputAudioMuted);
                network_->SendTCP(UserState, message);
            }
        }
    }

    state.outputAudioMuted = outputAudioMuted;
}

void MumblePlugin::SetOutputAudioLoopBack(bool loopBack)
{
    state.outputAudioLoopBack = loopBack;
}

void MumblePlugin::SetOutputAudioPositional(bool positional)
{
    state.outputPositional = positional;
}

void MumblePlugin::SetInputAudioMuted(bool inputAudioMuted)
{
    if (state.serverSynced)
    {
        MumbleUser *me = User(state.sessionId);
        if (!me)
        {
            LogError(LC + "Could not find own user ptr to set audio input muted state!");
            return;
        }

        if (audio_)
            audio_->SetInputAudioMuted(inputAudioMuted);
        
        if (network_)
        {
            // Avoid unwanted network traffic and check value actually changed.
            if (me->isSelfDeaf != inputAudioMuted)
            {
                MumbleProto::UserState message;
                message.set_session(state.sessionId);
                message.set_self_deaf(inputAudioMuted);
                message.set_self_mute(state.outputAudioMuted);
                network_->SendTCP(UserState, message);
            }
        }
    }

    state.inputAudioMuted = inputAudioMuted;
}

void MumblePlugin::RunAudioWizard()
{
    if (audioWizard && audioWizard->isVisible())
    {
        QApplication::setActiveWindow(audioWizard);
        return;
    }

    if (audioWizard)
        delete audioWizard;

    if (!state.serverSynced)
    {
        LogError(LC + "Audio wizard can only be shown while connected to a server!");
        return;
    }
    if (!audio_)
    {
        LogError(LC + "Audio wizard can't be shown, audio thread null!");
        return;
    }

    audioWizard = new AudioWizard(audio_->GetSettings());
    connect(audioWizard, SIGNAL(SettingsChanged(MumbleAudio::AudioSettings, bool)), SLOT(OnAudioSettingChanged(MumbleAudio::AudioSettings, bool)));
}

void MumblePlugin::OnConnected(QString address, int port, QString username)
{
    emit Connected(address, port, username);
}

void MumblePlugin::OnDisconnected(QString reason)
{
    Disconnect(reason);
}

void MumblePlugin::OnStateChange(MumbleNetwork::ConnectionState newState)
{
    // Don't signal same state multiple times
    if (state.connectionState == newState)
        return;
    
    if (newState == MumbleConnecting)
        LogInfo(LC + "State changed to \"MumbleConnecting\"");
    else if (newState == MumbleConnected)
        LogInfo(LC + "State changed to \"MumbleConnected\"");
    else if (newState == MumbleDisconnected)
        LogInfo(LC + "State changed to \"MumbleDisconnected\"");

    MumbleNetwork::ConnectionState oldState = state.connectionState;
    state.connectionState = newState;
    emit StateChange(state.connectionState, oldState);
}

void MumblePlugin::OnNetworkModeChange(MumbleNetwork::NetworkMode mode, QString reason)
{
    LogInfo(LC + "Network mode change: " + reason);
    state.networkMode = mode;
    emit NetworkModeChange(state.networkMode, reason);
}

void MumblePlugin::OnConnectionRejected(MumbleNetwork::RejectReason reasonType, QString reasonMessage)
{
    emit ConnectionRejected(reasonType, reasonMessage);
    Disconnect(reasonMessage);
}

void MumblePlugin::OnPermissionDenied(MumbleNetwork::PermissionDeniedType denyReason, MumbleNetwork::ACLPermission permission, uint channelId, uint targetUserId, QString reason)
{
    if (denyReason == PermissionDeniedPermission)
    {
        MumbleChannel *channel = Channel(channelId);
        if (channel)
        {
            if (targetUserId == state.sessionId)
            {
                LogWarning(LC + QString("You were denied %1 privileges in %2").arg(MumbleNetwork::PermissionName(permission)).arg(channel->fullName));
                if (reason.isEmpty())
                    reason = QString("You were denied %1 privileges in %2").arg(MumbleNetwork::PermissionName(permission)).arg(channel->fullName);
            }
            else
            {
                MumbleUser *user = User(targetUserId);
                if (user)
                {    
                    LogWarning(LC + QString("%1 was denied %2 privileges in %3").arg(user->name).arg(MumbleNetwork::PermissionName(permission)).arg(channel->fullName));
                    if (reason.isEmpty())
                        reason = QString("You were denied %1 privileges in %2").arg(MumbleNetwork::PermissionName(permission)).arg(channel->fullName);
                }
            }
        }
    }
    else if (denyReason == PermissionDeniedSuperUser)
    {
        LogError(LC + "Permission denied: Cannot modify SuperUser.");
        if (reason.isEmpty())
            reason = "Cannot modify SuperUser.";
    }
    else if (denyReason == PermissionDeniedTextTooLong)
    {
        LogError(LC + "Permission denied: Text message too long.");
        if (reason.isEmpty())
            reason = "Text message too long.";
    }
    else if (denyReason == PermissionDeniedChannelFull)
    {
        LogError(LC + "Channel is full!");
        if (reason.isEmpty())
            reason = "Channel is full!";
    }

    emit PermissionDenied(denyReason, permission, channelId, targetUserId, reason);
}

void MumblePlugin::OnTextMessageReceived(bool isPrivate, QList<uint> channelIds, uint senderId, QString message)
{
    MumbleUser *sender = User(senderId);
    if (!sender)
        return;

    if (isPrivate)
    {
        qDebug() << "Private from" << sender->name << "message:" << message;
        emit PrivateTextMessageReceived(sender, message);
    }
    else
    {
        MumbleUser *me = Me();
        if (me)
        {
            foreach(uint channelId, channelIds)
            {
                if (me->channelId == channelId)
                {
                    qDebug() << "Channel msg from" << sender->name << "message:" << message;
                    emit ChannelTextMessageReceived(sender, message);
                    break;
                }
            }
        }
    }
}

void MumblePlugin::OnChannelUpdate(uint id, uint parentId, QString name, QString description)
{
    MumbleChannel *channel = Channel(id);
    bool isNew = channel == 0;
    if (!channel)
    {
        channel = new MumbleChannel(this);
        channels_.push_back(channel);
    }
    channel->id = id;
    channel->parentId = parentId;
    channel->name = name;
    channel->description = description;

    // Resolve full channel name. Murmur will always send the
    // channel tree in a order that this can be done.
    channel->fullName = channel->name;
    uint resolveId = channel->parentId;
    while (id != resolveId)
    {
        MumbleChannel *resolveChannel = Channel(resolveId);
        if (!resolveChannel)
            break;
        channel->fullName.prepend(resolveChannel->name + "/");
        if (resolveId == 0) // Root hit
            break;
        resolveId = resolveChannel->parentId;
    }

    if (isNew)
    {
        qDebug() << "{NEW CHANNEL}   "  << channel->toString().toStdString().c_str();
        emit ChannelCreated(channel);
        emit ChannelsChanged(channels_);
    }
    else
    {
        qDebug() << "{CHANNEL UPDATE}"  << channel->toString().toStdString().c_str();
        emit ChannelUpdated(channel);
    }

    qDebug() << "";
}

void MumblePlugin::OnChannelRemoved(uint id)
{
    // Clean out children and dead channels
    Q_FOREVER
    {
        int childIndex = -1;
        for(int i=0; i<channels_.length(); i++)
        {
            if (channels_.at(i) == 0 || channels_.at(i)->parentId == id)
            {
                childIndex = i;
                break;
            }
        }
        if (childIndex != -1)
        {
            MumbleChannel *childChannel = channels_[childIndex];
            if (childChannel)
            {
                uint childId = childChannel->id;
                SAFE_DELETE(childChannel);
                emit ChannelRemoved(childId);
            }
            channels_.removeAt(childIndex);
        }
        else
            break;
    }

    // Remove main channel with id
    int index = -1;
    for(int i=0; i<channels_.length(); i++)
    {
        if (channels_.at(i)->id == id)
        {
            index = i;
            break;
        }
    }
    if (index != -1)
    {
        MumbleChannel *channel = channels_[index];   
        qDebug() << "{CHANNEL REMOVE}"  << channel->toString().toStdString().c_str();
        SAFE_DELETE(channel);
        channels_.removeAt(index);
        
        emit ChannelRemoved(id);
        emit ChannelsChanged(channels_);

        qDebug() << "";
    }
}

void MumblePlugin::OnUserUpdate(uint id, uint channelId, QString name, QString comment, QString hash, bool selfMuted, bool selfDeaf, bool isMe)
{
    bool channelChange = false;
    bool mutedChange = false;
    bool selfMutedChange = false;
    bool selfDeafChange = false;

    MumbleUser *user = User(id);
    bool isNew = (user == 0);
    
    // Created new user
    if (isNew)
    {
        channelChange = true;
        mutedChange = true;
        selfMutedChange = true;
        selfDeafChange = true;
        
        user = new MumbleUser(this);
        user->id = id;
        user->channelId = channelId;
        user->name = name;
        user->comment = comment;
        user->hash = hash;
        user->isSelfMuted = selfMuted;
        user->isSelfDeaf = selfDeaf;
        user->isMe = isMe;
    }
    // Existing user update
    else if (user)
    {
        // Detect changed properties for existing user
        if (user->channelId != channelId)
        {
            // Handle previous channel user removal and signaling
            MumbleChannel *previousChannel = user->Channel();
            if (previousChannel)
            {
                if (previousChannel->RemoveUser(user->id))
                {
                    qDebug() << "- previousChannel->EmitUserLeft" << user->id << user->name;
                    qDebug() << "- previousChannel->EmitUsersChanged" << previousChannel->users.size();
                    previousChannel->EmitUserLeft(user->id);
                    previousChannel->EmitUsersChanged();
                }
            }
            user->channelId = channelId;
            channelChange = true;
        }
        if (user->isSelfMuted != selfMuted)
        {
            user->isSelfMuted = selfMuted;
            selfMutedChange = true;
        }
        if (user->isSelfDeaf != selfDeaf)
        {
            user->isSelfDeaf = selfDeaf;
            selfDeafChange = true;
        }
    }
    else
    {
        LogError(LC + "Error on MumbleUser creation/update for id " + QString::number(id));
        return;
    }
    
    // Make sure we have the channel
    MumbleChannel *channel = Channel(user->channelId);
    if (!channel)
    {
        LogError(LC + "User creation/update detected unknown channel, aborting operation.");
        SAFE_DELETE(user);
        return;
    }

    // Not yet synced, push to pending
    if (!state.serverSynced)
    {
        pendingUsers_.push_back(user);
        return;
    }

    // Emit user created/updated
    if (isNew)
    {
        if (user->isMe)
        {
            qDebug() << "#### ME CREATED" << user->id << user->name;
            emit MeCreated(user);
        }

        qDebug() << "{CREATED}" << user->toString().toStdString().c_str();
        emit UserCreated(user);
    }
    else
    {
        qDebug() << "{UPDATED}" << user->toString().toStdString().c_str();
        emit UserUpdated(user);
    }

    // Emit user property changes
    if (channelChange)
    {
        if (channel->AddUser(user))
        {
            if (user->isMe)
            {
                qDebug() << "#### ME JOINED " << channel->id << channel->fullName;
                state.fullChannelName = channel->fullName;
                emit JoinedChannel(channel);
            }

            qDebug() << "  user->EmitChannelChanged()";
            user->EmitChannelChanged(channel);
            qDebug() << "- channel->EmitUserJoined" << user->id << user->name;
            qDebug() << "- channel->EmitUsersChanged" << channel->users.size();
            channel->EmitUserJoined(user);
            channel->EmitUsersChanged();
        }
    }
    if (mutedChange)
    {
        qDebug() << "  user->EmitMuted()";
        user->EmitMuted();
        qDebug() << "# UserMuted" << user->id << user->isMuted;
        emit UserMuted(user, user->isMuted);
    }
    if (selfMutedChange)
    {
        qDebug() << "  user->EmitSelfMuted()";
        user->EmitSelfMuted();
        qDebug() << "# UserSelfMuted" << user->id << user->isSelfMuted;
        emit UserSelfMuted(user, user->isSelfMuted);
    }
    if (selfDeafChange)
    {
        qDebug() << "  user->EmitSelfDeaf()";
        user->EmitSelfDeaf();
        qDebug() << "# UserSelfDeaf" << user->id << user->isSelfDeaf;
        emit UserSelfDeaf(user, user->isSelfDeaf);
    }

    qDebug() << "";
}

void MumblePlugin::OnUserLeft(uint id, uint actorId, bool banned, bool kicked, QString reason)
{
    if (!state.serverSynced)
    {
        int index = -1;
        for(int i=0; i<pendingUsers_.count(); i++)
        {
            if (pendingUsers_.at(i)->id == id)
            {
                index = i;
                break;
            }
        }
        if (index != -1)
            pendingUsers_.removeAt(index);
        return;
    }

    MumbleChannel *channel = ChannelForUser(id);
    if (channel)
    {
        MumbleUser *user = channel->User(id);
        if (user)
        {
            if (channel->RemoveUser(id))
                channel->EmitUsersChanged();
            SAFE_DELETE(user);
        }
    }
    else
        LogError(LC + "Could not find channel for disconnected user " + QString::number(id));

    if (audio_)
        audio_->ClearInputAudio(id);
}

void MumblePlugin::OnServerSynced(uint sessionId)
{
    qDebug() << "";

    state.serverSynced = true;
    state.sessionId = sessionId;

    QString pendingChannelJoin = state.fullChannelName;

    // Find and process own user ptr first, this is done because
    // MeCreated and JoinedChannel signals are fired inside OnUserUpdate.
    // We want to have these signals fire first so you can start to listen to
    // the channel signals (user joined/left) of your own channel and get full information with them.
    int meIndex = 0;
    for(int i=0; i<pendingUsers_.length(); i++)
    {
        if (pendingUsers_.at(i)->id == state.sessionId)
        {
            OnUserUpdate(pendingUsers_.at(i)->id, pendingUsers_.at(i)->channelId, pendingUsers_.at(i)->name, pendingUsers_.at(i)->comment, 
                pendingUsers_.at(i)->hash, pendingUsers_.at(i)->isSelfMuted, pendingUsers_.at(i)->isSelfDeaf, state.sessionId == pendingUsers_.at(i)->id);
            meIndex = i;
            SAFE_DELETE(pendingUsers_[i]);
            break;
        }
    }
    pendingUsers_.removeAt(meIndex);

    // Iterate and create/join other users on the server
    foreach(MumbleUser *pu, pendingUsers_)
    {
        if (!pu)
        {
            LogError(LC + "Error handlng pending users after server syncronized, was null!");
            continue;
        }
        OnUserUpdate(pu->id, pu->channelId, pu->name, pu->comment, pu->hash, pu->isSelfMuted, pu->isSelfDeaf, state.sessionId == pu->id);
        SAFE_DELETE(pu);
    }
    pendingUsers_.clear();

    MumbleUser *me = User(state.sessionId);
    if (!me)
    {
        LogError(LC + "Could not find own user ptr after connected!");
        return;
    }

    MumbleChannel *myChannel = Channel(me->channelId);
    if (!myChannel)
    {
        LogError(LC + "Could not find own channel ptr after connected!");
        return;
    }
    if (state.fullChannelName != myChannel->fullName)
        LogError(LC + "Current channel mismatch after connected!");
    
    // Join potential pending channel
    if (pendingChannelJoin != myChannel->fullName)
        JoinChannel(pendingChannelJoin);
    
    // Send and setup audio state
    if (audio_)
    {
        audio_->SetInputAudioMuted(state.inputAudioMuted);
        audio_->SetOutputAudioMuted(state.outputAudioMuted);
    }
    else 
        LogError(LC + "Audio thread null after connected!");

    if (network_)
    {
        if (me->isSelfDeaf != state.inputAudioMuted || me->isSelfMuted != state.outputAudioMuted)
        {
            MumbleProto::UserState message;
            message.set_session(state.sessionId);
            message.set_self_deaf(state.inputAudioMuted);
            message.set_self_mute(state.outputAudioMuted);
            network_->SendTCP(UserState, message);
        }
    }
    else
        LogError(LC + "Network thread null after connected!");
}

void MumblePlugin::OnScriptEngineCreated(QScriptEngine *engine)
{
    RegisterMumblePluginMetaTypes(engine);
}

void MumblePlugin::UpdatePositionalInfo(VoicePacketInfo &packetInfo)
{
    packetInfo.isPositional = false;

    if (!state.serverSynced)
        return;

    MumbleUser *me = User(state.sessionId);
    if (!me)
    {
        LogError(LC + "Cannot update own MumbleUser positional information, ptr is null!");
        return;
    }
    me->isPositional = false;

    if (!framework_ || !framework_->Renderer())
        return;
    Scene *scene = framework_->Renderer()->MainCameraScene();
    if (!scene)
        return;

    Entity *activeListener = 0;
    EntityList listenerEnts = scene->GetEntitiesWithComponent(EC_SoundListener::TypeNameStatic());
    foreach(EntityPtr listenerEnt, listenerEnts)
    {
        EC_SoundListener *listener = listenerEnt->GetComponent<EC_SoundListener>().get();
        if (listener && listener->ParentEntity() && listener->active.Get())
        {
            activeListener = listener->ParentEntity();
            break;
        }
    }

    if (activeListener)
    {
        EC_Placeable *placeable = activeListener->GetComponent<EC_Placeable>().get();
        if (placeable)
        {
            float3 worldPos = placeable->WorldPosition();
            me->pos = worldPos;
            me->isPositional = true;
            packetInfo.pos = worldPos;
            packetInfo.isPositional = true;
        }
    }        
}

void MumblePlugin::OnAudioSettingChanged(MumbleAudio::AudioSettings settings, bool saveConfig)
{
    if (audio_)
    {
        if (saveConfig)
            SaveSettings(settings);
        audio_->ApplySettings(settings);
    }
    else
        LogError(LC + "Audio wizard can't be shown, audio thread null!");
}

MumbleAudio::AudioSettings MumblePlugin::LoadSettings()
{
    ConfigAPI *config = framework_->Config();
    if (!config)
    {
        LogError(LC + "ConfigAPI null in LoadSettings(), returning default config!");
        return MumbleAudio::AudioSettings();
    }

    MumbleAudio::AudioSettings settings;
    
    ConfigData data("mumbleplugin", "output");
    if (config->HasValue(data, "quality"))
        settings.quality = (AudioQuality)config->Get(data, "quality").toInt();
    if (config->HasValue(data, "transmitmode"))
        settings.transmitMode = (TransmitMode)config->Get(data, "transmitmode").toInt();
    if (config->HasValue(data, "supression"))
        settings.suppression = config->Get(data, "supression").toInt();
    if (config->HasValue(data, "amplification"))
        settings.amplification = config->Get(data, "amplification").toInt();
    if (config->HasValue(data, "VADmin"))
        settings.VADmin = config->Get(data, "VADmin").toFloat();
    if (config->HasValue(data, "VADmax"))
        settings.VADmax = config->Get(data, "VADmax").toFloat();

    return settings;
}

void MumblePlugin::SaveSettings(MumbleAudio::AudioSettings settings)
{
    ConfigAPI *config = framework_->Config();
    if (!config)
    {
        LogError(LC + "ConfigAPI null in SaveSettings()!");
        return;
    }

    ConfigData data("mumbleplugin", "output");
    config->Set(data, "quality", (int)settings.quality);
    config->Set(data, "transmitmode", (int)settings.transmitMode);
    config->Set(data, "supression", settings.suppression);
    config->Set(data, "amplification", settings.amplification);
    config->Set(data, "VADmin", settings.VADmin);
    config->Set(data, "VADmax", settings.VADmax);
}

void MumblePlugin::DebugMute(QString userIdStr)
{
    uint id = userIdStr.toInt();
    MumbleUser *u = User(id);
    if (u)
        SetMuted(u->id, !u->isMuted);
}

extern "C"
{
    DLLEXPORT void TundraPluginMain(Framework *fw)
    {
        Framework::SetInstance(fw); // Inside this DLL, remember the pointer to the global framework object.
        IModule *module = new MumblePlugin();
        fw->RegisterModule(module);
    }
}
