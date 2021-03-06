// For conditions of distribution and use, see copyright notice in LICENSE

#pragma once

#include "TundraCoreApi.h"
#include "IAsset.h"

/// Contains data of a script file loaded to the system.
class TUNDRACORE_API ScriptAsset : public IAsset
{
    Q_OBJECT

public:
    ScriptAsset(AssetAPI *owner, const QString &type_, const QString &name_) :
        IAsset(owner, type_, name_)
    {
    }

    ~ScriptAsset();
    
    /// Load script asset from memory
    virtual bool DeserializeFromData(const u8 *data, size_t numBytes, bool allowAsynchronous);

    /// Load script asset into memory
    virtual bool SerializeTo(std::vector<u8> &dst, const QString &serializationParameters) const;

    /// Return found asset references inside the script
    virtual std::vector<AssetReference> FindReferences() const { return references; }

    /// The asset references specified by this asset are specified in the above scriptContent data,
    /// but we cache them here on loading to quicken the access if they're needed several times.
    /// This also performs validation-on-load.
    std::vector<AssetReference> references;

    QString scriptContent;

    bool IsLoaded() const;

private:
    /// Unload script asset
    virtual void DoUnload();

private slots:
    /// Parse internal references from script
    void ParseReferences();
};
Q_DECLARE_METATYPE(ScriptAsset*)

typedef shared_ptr<ScriptAsset> ScriptAssetPtr;
Q_DECLARE_METATYPE(ScriptAssetPtr)
