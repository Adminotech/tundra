// For conditions of distribution and use, see copyright notice in LICENSE

#include "StableHeaders.h"
#include "DebugOperatorNew.h"
#include "AssetModule.h"
#include "LocalAssetProvider.h"
#include "HttpAssetProvider.h"
#include "HttpAssetStorage.h"
#include "Framework.h"
#include "Profiler.h"
#include "CoreException.h"
#include "AssetAPI.h"
#include "LocalAssetStorage.h"
#include "ConsoleAPI.h"
#include "Application.h"
#include "CoreTypes.h"
#include "KristalliProtocolModule.h"
#include "TundraLogicModule.h"
#include "TundraMessages.h"
#include "Client.h"
#include "Server.h"
#include "UserConnectedResponseData.h"
#include "UserConnection.h"
#include "MsgAssetDeleted.h"
#include "MsgAssetDiscovery.h"

#include <kNetBuildConfig.h>
#include <kNet/MessageConnection.h>

#include <QDir>

#include "StaticPluginRegistry.h"

#include "MemoryLeakCheck.h"

AssetModule::AssetModule()
:IModule("Asset")
{
}

AssetModule::~AssetModule()
{
}

void AssetModule::Load()
{
    shared_ptr<HttpAssetProvider> http = MAKE_SHARED(HttpAssetProvider, Fw());
    Fw()->Asset()->RegisterAssetProvider(http);

    shared_ptr<LocalAssetProvider> local = MAKE_SHARED(LocalAssetProvider, Fw());
    Fw()->Asset()->RegisterAssetProvider(local);

    QString systemAssetDir = Application::InstallationDirectory() + "data/assets";
    AssetStoragePtr storage = local->AddStorageDirectory(systemAssetDir, "System", true, false);
//    AssetStoragePtr storage = local->AddStorageDirectory(systemAssetDir, "System", true, QFileInfo(systemAssetDir).isWritable());
    storage->SetReplicated(false); // If we are a server, don't pass this storage to the client.
}

void AssetModule::Initialize()
{
    shared_ptr<LocalAssetProvider> local = Fw()->Asset()->AssetProvider<LocalAssetProvider>();

    /// @todo This belongs to JavascriptModule.
    QString jsAssetDir = Application::InstallationDirectory() + "jsmodules";
    AssetStoragePtr storage = local->AddStorageDirectory(jsAssetDir, "Javascript", true, false);
    storage->SetReplicated(false); // If we are a server, don't pass this storage to the client.

    /// @todo This belongs to OgreRenderingModule.
    QString ogreAssetDir = Application::InstallationDirectory() + "media";
    storage = local->AddStorageDirectory(ogreAssetDir, "Ogre Media", true, false);
    storage->SetReplicated(false); // If we are a server, don't pass this storage to the client.

    Fw()->RegisterDynamicObject("assetModule", this);

    Fw()->Console()->RegisterCommand(
        "requestAsset", "Request asset from server. Usage: requestAsset(assetRef, assetType)",
        this, SLOT(ConsoleRequestAsset(const QString &, const QString &)));

    Fw()->Console()->RegisterCommand(
        "addAssetStorage", "Usage: addAssetStorage(storageString), f.ex.: addAssetStorage(name=MyAssets;type=HttpAssetStorage;src=http://www.myserver.com/;default;)", 
        this, SLOT(AddAssetStorage(const QString &)));

    Fw()->Console()->RegisterCommand(
        "listAssetStorages", "Serializes all currently registered asset storages to the console output log.", 
        this, SLOT(ListAssetStorages()));

    Fw()->Console()->RegisterCommand(
        "refreshHttpStorages", "Refreshes known assetrefs for all http asset storages", 
        this, SLOT(ConsoleRefreshHttpStorages()));

    Fw()->Console()->RegisterCommand(
        "dumpAssetTransfers", "Dumps debugging information of current asset transfers to console", 
        this, SLOT(ConsoleDumpAssetTransfers()));

    Fw()->Console()->RegisterCommand(
        "dumpAssets", "Lists all assets known to the Asset API", 
        this, SLOT(ConsoleDumpAssets()));
    
    ProcessCommandLineOptions();

    TundraLogic::Server *server = Fw()->Module<TundraLogicModule>()->GetServer().get();
    connect(server, SIGNAL(UserConnected(u32, UserConnection *, UserConnectedResponseData *)), this,
        SLOT(ServerNewUserConnected(u32, UserConnection *, UserConnectedResponseData *)));

    TundraLogic::Client *client = Fw()->Module<TundraLogicModule>()->GetClient().get();
    connect(client, SIGNAL(Connected(UserConnectedResponseData *)), this, SLOT(ClientConnectedToServer(UserConnectedResponseData *)));
    connect(client, SIGNAL(Disconnected()), this, SLOT(ClientDisconnectedFromServer()));

    KristalliProtocolModule *kristalli = Fw()->Module<KristalliProtocolModule>();
    connect(kristalli, SIGNAL(NetworkMessageReceived(kNet::MessageConnection *, kNet::packet_id_t, kNet::message_id_t, const char *, size_t)), 
        this, SLOT(HandleKristalliMessage(kNet::MessageConnection*, kNet::packet_id_t, kNet::message_id_t, const char*, size_t)), Qt::UniqueConnection);

    // Connect to asset uploads & deletions from storage to be able to broadcast asset discovery & deletion messages
    connect(Fw()->Asset(), SIGNAL(AssetUploaded(const QString &)), this, SLOT(OnAssetUploaded(const QString &)));
    connect(Fw()->Asset(), SIGNAL(AssetDeletedFromStorage(const QString&)), this, SLOT(OnAssetDeleted(const QString&)));
}

void AssetModule::ProcessCommandLineOptions()
{
    const bool hasFile = Fw()->HasCommandLineParameter("--file");
    const bool hasStorage = Fw()->HasCommandLineParameter("--storage");
    const QStringList files = Fw()->CommandLineParameters("--file");
    const QStringList storages = Fw()->CommandLineParameters("--storage");
    if (hasFile && files.isEmpty())
        LogError("AssetModule: --file specified without a value.");
    if (hasStorage && storages.isEmpty())
        LogError("AssetModule: --storage specified without a value.");
    foreach(const QString &file, files)
    {
        AssetStoragePtr storage = Fw()->Asset()->DeserializeAssetStorageFromString(file.trimmed(), false);
        Fw()->Asset()->SetDefaultAssetStorage(storage);
    }
    foreach(const QString &storageName, storages)
    {
        AssetStoragePtr storage = Fw()->Asset()->DeserializeAssetStorageFromString(storageName.trimmed(), false);
        if (files.isEmpty()) // If "--file" was not specified, then use "--storage" as the default. (If both are specified, "--file" takes precedence over "--storage").
            Fw()->Asset()->SetDefaultAssetStorage(storage);
    }
    if (Fw()->HasCommandLineParameter("--defaultstorage"))
    {
        QStringList defaultStorages = Fw()->CommandLineParameters("--defaultstorage");
        if (defaultStorages.size() == 1)
        {
            AssetStoragePtr defaultStorage = Fw()->Asset()->AssetStorageByName(defaultStorages[0]);
            if (!defaultStorage)
                LogError("Cannot set storage \"" + defaultStorages[0] + "\" as the default storage, since it doesn't exist!");
            else
                Fw()->Asset()->SetDefaultAssetStorage(defaultStorage);
        }
        else
            LogError("Parameter --defaultstorage may be specified exactly once, and must contain a single value!");
    }
}

void AssetModule::ConsoleRefreshHttpStorages()
{
    RefreshHttpStorages();
}

void AssetModule::ConsoleRequestAsset(const QString &assetRef, const QString &assetType)
{
    AssetTransferPtr transfer = Fw()->Asset()->RequestAsset(assetRef, assetType, true);
}

void AssetModule::AddAssetStorage(const QString &storageString)
{
    AssetStoragePtr storage = Fw()->Asset()->DeserializeAssetStorageFromString(storageString, false);
}

void AssetModule::ListAssetStorages()
{
    LogInfo("Registered storages: ");
    foreach(const AssetStoragePtr &storage, Fw()->Asset()->AssetStorages())
    {
        QString storageString = storage->SerializeToString();
        if (Fw()->Asset()->DefaultAssetStorage() == storage)
            storageString += ";default";
        LogInfo(storageString);
    }
}

void AssetModule::LoadAllLocalAssetsWithSuffix(const QString &suffix, const QString &assetType)
{
    foreach(const AssetStoragePtr &s, Fw()->Asset()->AssetStorages())
    {
        LocalAssetStorage *storage = dynamic_cast<LocalAssetStorage*>(s.get());
        if (storage)
            storage->LoadAllAssetsOfType(Fw()->Asset(), suffix, assetType);
    }
}

void AssetModule::RefreshHttpStorages()
{
    foreach(const AssetStoragePtr &s, Fw()->Asset()->AssetStorages())
    {
        HttpAssetStorage *storage = dynamic_cast<HttpAssetStorage*>(s.get());
        if (storage)
            storage->RefreshAssetRefs();
    }
}

void AssetModule::ServerNewUserConnected(u32 /*connectionID*/, UserConnection *connection, UserConnectedResponseData *responseData)
{
    QDomDocument &doc = responseData->responseData;
    QDomElement assetRoot = doc.createElement("asset");
    doc.appendChild(assetRoot);
    
    // Did we get a new user from the same computer the server is running at?
    bool isLocalhostConnection = false;
    KNetUserConnection* kNetConn = dynamic_cast<KNetUserConnection*>(connection);
    if (kNetConn && kNetConn->connection)
    {
        isLocalhostConnection = (kNetConn->connection->RemoteEndPoint().IPToString() == "127.0.0.1" || 
            kNetConn->connection->LocalEndPoint().IPToString() == kNetConn->connection->RemoteEndPoint().IPToString());
    }

    // Serialize all storages to the client. If the client is from the same computer than the server, we can also serialize the LocalAssetStorages.
    std::vector<AssetStoragePtr> storages = Fw()->Asset()->AssetStorages();
    for(size_t i = 0; i < storages.size(); ++i)
    {
        bool isLocalStorage = (dynamic_cast<LocalAssetStorage*>(storages[i].get()) != 0);
        if (storages[i]->IsReplicated() && (!isLocalStorage || isLocalhostConnection))
        {
            QDomElement storage = doc.createElement("storage");
            storage.setAttribute("data", storages[i]->SerializeToString(!isLocalhostConnection));
            assetRoot.appendChild(storage);
        }
    }

    // Specify which storage to use as default.
    AssetStoragePtr defaultStorage = Fw()->Asset()->DefaultAssetStorage();
    bool defaultStorageIsLocal = (dynamic_cast<LocalAssetStorage*>(defaultStorage.get()) != 0);
    if (defaultStorage && (!defaultStorageIsLocal || isLocalhostConnection))
    {
        QDomElement storage = doc.createElement("defaultStorage");
        storage.setAttribute("name", defaultStorage->Name());
        assetRoot.appendChild(storage);
        if (!defaultStorage->IsReplicated())
            LogWarning("Server specified the client to use the storage \"" + defaultStorage->Name() + "\" as default, but it is not a replicated storage!");
    }

    // Fill the same data as JSON for web clients
    QVariantMap storageData;
    storageData["default"] = true;
    storageData["name"] = defaultStorage->Name();
    storageData["type"] = defaultStorage->Type();
    storageData["src"] = defaultStorage->BaseURL();
    responseData->responseDataJson["storage"] = storageData;
}

void AssetModule::DetermineStorageTrustStatus(AssetStoragePtr storage)
{
    // If the --trustserverstorages command line parameter is set, we trust each storage exactly the way the server does.
    ///\todo Make the a end-user option at runtime/connection time to specify per-server instance whether --trustserverstorages is in effect.
    if (!Fw()->HasCommandLineParameter("--trustserverstorages"))
    {
        ///\todo Read from ConfigAPI whether to set false/ask/true here.
        ///\todo If the trust state is 'ask', show a *non-modal* notification -> config dialog if the user wants to trust content from this source.
        storage->SetTrustState(IAssetStorage::StorageAskTrust);
    }
}

void AssetModule::ClientConnectedToServer(UserConnectedResponseData *responseData)
{
    QDomDocument &doc = responseData->responseData;
    QDomElement assetRoot = doc.firstChildElement("asset");
    if (!assetRoot.isNull())
    {
        for (QDomElement storage = assetRoot.firstChildElement("storage"); !storage.isNull(); 
            storage = storage.nextSiblingElement("storage"))
        {
            QString storageData = storage.attribute("data");
            bool connectedToRemoteServer = true; // If false, we connected to localhost.
            ///\todo Determine here whether we connected to localhost, and if so, set connectedToRemoteServer = false.
            AssetStoragePtr assetStorage = Fw()->Asset()->DeserializeAssetStorageFromString(storageData, connectedToRemoteServer);

            // Remember that this storage was received from the server, so we can later stop using it when we disconnect (and possibly reconnect to another server).
            if (assetStorage)
            {
                assetStorage->SetReplicated(true); // We got this from the server.
                if (connectedToRemoteServer) // If connected to localhost, we always trust the same storages the server is trusting, so don't need to call DetermineStorageTrustStatus.
                    DetermineStorageTrustStatus(assetStorage);
                storagesReceivedFromServer.push_back(assetStorage);
            }
        }

        QDomElement defaultStorage = assetRoot.firstChildElement("defaultStorage");
        if (!defaultStorage.isNull())
        {
            QString defaultStorageName = defaultStorage.attribute("name");
            AssetStoragePtr defaultStoragePtr = Fw()->Asset()->AssetStorageByName(defaultStorageName);
            if (defaultStoragePtr)
                Fw()->Asset()->SetDefaultAssetStorage(defaultStoragePtr);
        }
    }
}

void AssetModule::ClientDisconnectedFromServer()
{
    for(size_t i = 0; i < storagesReceivedFromServer.size(); ++i)
    {
        AssetStoragePtr storage = storagesReceivedFromServer[i].lock();
        if (storage)
            Fw()->Asset()->RemoveAssetStorage(storage->Name());
    }
    storagesReceivedFromServer.clear();
}

void AssetModule::HandleKristalliMessage(kNet::MessageConnection* source, kNet::packet_id_t, kNet::message_id_t id, const char* data, size_t numBytes)
{
    switch (id)
    {
    case cAssetDiscoveryMessage:
        {
            MsgAssetDiscovery msg(data, numBytes);
            HandleAssetDiscovery(source, msg);
        }
        break;
    case cAssetDeletedMessage:
        {
            MsgAssetDeleted msg(data, numBytes);
            HandleAssetDeleted(source, msg);
        }
        break;
    }
}

void AssetModule::HandleAssetDiscovery(kNet::MessageConnection* source, MsgAssetDiscovery& msg)
{
    QString assetRef = QString::fromStdString(BufferToString(msg.assetRef));
    QString assetType = QString::fromStdString(BufferToString(msg.assetType));
    
    // Check for possible malicious discovery message and ignore it. Otherwise let AssetAPI handle
    if (!ShouldReplicateAssetDiscovery(assetRef))
        return;
    
    // If we are server, the message had to come from a client, and we replicate it to everyone except the sender
    TundraLogicModule* tundra = Fw()->Module<TundraLogicModule>();
    KristalliProtocolModule *kristalli = Fw()->Module<KristalliProtocolModule>();
    if (tundra->IsServer())
        foreach(UserConnectionPtr userConn, kristalli->UserConnections())
        {
            KNetUserConnection* kNetConn = dynamic_cast<KNetUserConnection*>(userConn.get());
            if (kNetConn->connection != source)
                userConn->Send(msg);
        }

    // Then let assetAPI handle locally
    Fw()->Asset()->HandleAssetDiscovery(assetRef, assetType);
}

void AssetModule::HandleAssetDeleted(kNet::MessageConnection* source, MsgAssetDeleted& msg)
{
    QString assetRef = QString::fromStdString(BufferToString(msg.assetRef));
    
    // Check for possible malicious delete message and ignore it. Otherwise let AssetAPI handle
    if (!ShouldReplicateAssetDiscovery(assetRef))
        return;
    
    // If we are server, the message had to come from a client, and we replicate it to everyone except the sender
    TundraLogicModule* tundra = Fw()->Module<TundraLogicModule>();
    KristalliProtocolModule *kristalli = Fw()->Module<KristalliProtocolModule>();
    if (tundra->IsServer())
        foreach(UserConnectionPtr userConn, kristalli->UserConnections())
        {
            KNetUserConnection* kNetConn = dynamic_cast<KNetUserConnection*>(userConn.get());
            if (kNetConn->connection != source)
                userConn->Send(msg);
        }

    // Then let assetAPI handle locally
    Fw()->Asset()->HandleAssetDeleted(assetRef);
}

void AssetModule::OnAssetUploaded(const QString& assetRef)
{
    // Check whether the asset upload needs to be replicated
    if (!ShouldReplicateAssetDiscovery(assetRef))
        return;
    
    TundraLogicModule* tundra = Fw()->Module<TundraLogicModule>();
    KristalliProtocolModule *kristalli = Fw()->Module<KristalliProtocolModule>();

    MsgAssetDiscovery msg;
    msg.assetRef = StringToBuffer(assetRef.toStdString()); /// @bug Convert to UTF-8 instead!
    /// \todo Would preferably need the assettype as well
    
    // If we are server, send to everyone
    if (tundra->IsServer())
    {
        foreach(UserConnectionPtr userConn, kristalli->UserConnections())
            userConn->Send(msg);
    }
    // If we are client, send to server
    else
    {
        kNet::MessageConnection* connection = tundra->GetClient()->MessageConnection();
        if (connection)
            connection->Send(msg);
    }
}

void AssetModule::OnAssetDeleted(const QString& assetRef)
{
    // Check whether the asset delete needs to be replicated
    if (!ShouldReplicateAssetDiscovery(assetRef))
        return;
    
    TundraLogicModule* tundra = Fw()->Module<TundraLogicModule>();
    KristalliProtocolModule *kristalli = Fw()->Module<KristalliProtocolModule>();

    MsgAssetDeleted msg;
    msg.assetRef = StringToBuffer(assetRef.toStdString()); /// @bug Convert to UTF-8 instead!

    if (tundra->IsServer()) // We are server, send to everyone
    {
        foreach(UserConnectionPtr userConn, kristalli->UserConnections())
            userConn->Send(msg);
    }
    else // We are client, send to server
    {
        kNet::MessageConnection* connection = tundra->GetClient()->MessageConnection();
        if (connection)
            connection->Send(msg);
    }
}

void AssetModule::ConsoleDumpAssetTransfers()
{
    AssetAPI* asset = Fw()->Asset();
    LogInfo("Current transfers:");
    const AssetTransferMap& currentTransfers = asset->CurrentTransfers();
    for(AssetTransferMap::const_iterator i = currentTransfers.begin(); i != currentTransfers.end(); ++i)
    {
        AssetPtr assetPtr = asset->FindAsset(i->first);
        unsigned numPendingDependencies = assetPtr ? asset->NumPendingDependencies(assetPtr) : 0;
        if (numPendingDependencies > 0)
        {
            LogInfo(i->first + ", " + QString::number(numPendingDependencies) + " pending dependencies");
            std::vector<AssetReference> refs = assetPtr->FindReferences();
            for(size_t i = 0; i < refs.size(); ++i)
                LogInfo("   Depends on \"" + refs[i].ref + "\", of type \"" + refs[i].type + "\"");
        }
        else
            LogInfo(i->first);
    }

    LogInfo("Ready asset transfers:");
    const std::vector<AssetTransferPtr> &readyTransfers = asset->DebugGetReadyTransfers();
    for(unsigned i = 0; i < readyTransfers.size(); ++i)
        LogInfo(readyTransfers[i]->source.ref);

    /*
    const AssetAPI::AssetDependenciesMap &dependencies = asset->DebugGetAssetDependencies();
    LogInfo("Asset dependencies:");
    for (AssetAPI::AssetDependenciesMap::const_iterator i = dependencies.begin(); i != dependencies.end(); ++i)
        LogInfo("\"" + i->first + "\" -> \"" + i->second + "\"");
    */
}

void AssetModule::ConsoleDumpAssets()
{
    LogInfo("Current assets:");
    const AssetMap& assets = Fw()->Asset()->Assets();
    for(AssetMap::const_iterator i = assets.begin(); i != assets.end(); ++i)
    {
        QString name = i->first;
        if (!i->second->IsLoaded())
            name += " (unloaded)";
        LogInfo(name);
    }
}

bool AssetModule::ShouldReplicateAssetDiscovery(const QString& assetRef)
{
    QString protocol;
    AssetAPI::AssetRefType type = AssetAPI::ParseAssetRef(assetRef, &protocol);
    if (type == AssetAPI::AssetRefInvalid || type == AssetAPI::AssetRefLocalPath || type == AssetAPI::AssetRefLocalUrl || type == AssetAPI::AssetRefRelativePath)
        return false;
    else
    {
        AssetPtr asset = Fw()->Asset()->FindAsset(assetRef);
        AssetStoragePtr storage = asset ? asset->AssetStorage() : AssetStoragePtr();
        // If the storage exists, simply check that it's replicated and it is an HttpAssetStorage
        /// \todo Evaluate whether asset discovery should be/needs to be supported for other assetstorages
        if (storage && storage->IsReplicated() && dynamic_cast<HttpAssetStorage*>(storage.get()) != 0)
            return true;
        // If the storage does not exist, check the protocol part of the ref.
        if (!storage)
        {
            if (protocol.compare("http", Qt::CaseInsensitive) == 0 || protocol.compare("https", Qt::CaseInsensitive) == 0)
                return true;
        }
    }
    
    return false;
}

extern "C"
{
#ifndef ANDROID
DLLEXPORT void TundraPluginMain(Framework *fw)
#else
DEFINE_STATIC_PLUGIN_MAIN(AssetModule)
#endif
{
    Framework::SetInstance(fw); // Inside this DLL, remember the pointer to the global framework object.
    fw->RegisterModule(new AssetModule());
}
}
