/**
    For conditions of distribution and use, see copyright notice in LICENSE

    @file   SceneTreeWidgetItems.h
    @brief  Tree widget -related classes used in @c SceneTreeWidget and @c AssetTreeWidget. */

#include "StableHeaders.h"
#include "DebugOperatorNew.h"

#include "SceneTreeWidgetItems.h"
#include "SceneTreeWidget.h"
#include "SceneStructureWindow.h"

#include "Entity.h"
#include "UniqueIdGenerator.h"
#include "AssetReference.h"
#include "IAsset.h"
#include "IAssetBundle.h"
#include "IAssetStorage.h"
#include "AssetAPI.h"
#include "LoggingFunctions.h"
#include "Profiler.h"

#include "MemoryLeakCheck.h"

namespace
{
    const QString localText = QApplication::translate("SceneTreeItem", "Local");
    const QString temporaryText = QApplication::translate("SceneTreeItem", "Temporary");
    const QString localOnlyText = QApplication::translate("ComponentItem", "UpdateMode:LocalOnly");
    const QString disconnectedText = QApplication::translate("ComponentItem", "UpdateMode:Disconnected");
}

// EntityGroupItem

EntityGroupItem::EntityGroupItem(const QString &groupName) :
    name(groupName)
{
    setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);
    setTextColor(0, Qt::black);
    setTextColor(1, QColor(68,68,68));

    setText(0, "Group: " + name);
}

EntityGroupItem::~EntityGroupItem()
{
    // Don't leave children free-floating.
    if (treeWidget())
    {
        foreach(QTreeWidgetItem *child, takeChildren())
            treeWidget()->addTopLevelItem(child);
    }
}

void EntityGroupItem::UpdateText()
{
    setText(0, "Group: " + name);
    setText(1, QString("%1 %2").arg(entityItems.size()).arg(entityItems.size() > 1 ? "Entities" : "Entity"));

    // Groups should be hidde if it has no child node. This can happen when group items are moved to parent entities.
    // In this case the group is still defined in the Entity but Entity parenting takes priority in the tree.
    bool visible = (childCount() > 0);
    if (isHidden() == visible)
        setHidden(!visible);
}

void EntityGroupItem::AddEntityItems(QList<EntityItem*> eItems, bool checkParenting, bool addAsChild, bool updateText)
{
    foreach(EntityItem *eItem, eItems)
        AddEntityItem(eItem, checkParenting, addAsChild, false);

    if (updateText)
        UpdateText();
}

void EntityGroupItem::AddEntityItem(EntityItem *eItem, bool checkParenting, bool addAsChild, bool updateText)
{
    if (entityItems.contains(eItem))
    {
        // Update visibility
        if (updateText)
            UpdateText();
        return;
    }

    if (checkParenting)
    {
        EntityGroupItem *parentItem = eItem->Parent();
        if (!parentItem && treeWidget())
        {
            // Currently a top-level item, remove from top-level.
            int topLevelIndex = treeWidget()->indexOfTopLevelItem(eItem);
            if (topLevelIndex != -1)
                treeWidget()->takeTopLevelItem(topLevelIndex);
        }
        else if (parentItem && parentItem != this)
            eItem->Parent()->RemoveEntityItem(eItem);
    }

    if (addAsChild)
        addChild(eItem);
    entityItems << eItem;

    if (updateText)
        UpdateText();
}

void EntityGroupItem::ClearEntityItems(bool updateText)
{
    entityItems.clear();
    if (updateText)
        UpdateText();
}

bool EntityGroupItem::ContainsAllItems(const QList<EntityItem*> &items)
{
    foreach(EntityItem *item, items)
    {
        if (!entityItems.contains(item))
            return false;
    }
    return true;
}

void EntityGroupItem::RemoveEntityItem(EntityItem *item, bool updateText)
{
    if (!entityItems.contains(item))
        return;

    EntityGroupItem *parentItem = item->Parent();
    removeChild(item);

    if (parentItem == this)
        treeWidget()->addTopLevelItem(item);

    entityItems.removeAll(item);

    if (updateText)
        UpdateText();
}

void EntityGroupItem::RemoveAndReparentEntityItem(EntityItem *item, QTreeWidgetItem *newParent, bool updateText)
{
    if (entityItems.contains(item))
    {
        removeChild(item);
        entityItems.removeAll(item);
    }
    if (newParent)
    {
        EntityGroupItem *newGroup = dynamic_cast<EntityGroupItem*>(newParent);
        EntityItem *newEntity = dynamic_cast<EntityItem*>(newParent);
        if (newGroup)
            newGroup->AddEntityItem(item, false, true, updateText);
        else if (newEntity)
        {
            newEntity->addChild(item);
            newEntity->UpdateText();
        }
        else
            newParent->addChild(item);
    }
    if (updateText)
        UpdateText();
}

bool EntityGroupItem::operator <(const QTreeWidgetItem &rhs) const
{
    PROFILE(EntityGroupItem_OperatorLessThan)

    // Entities never go before groups, even when sorting by name.
    const EntityGroupItem *group = dynamic_cast<const EntityGroupItem*>(&rhs);
    if (!group)
        return false;
    // note This is deliberately >= 0 to sort groups alphabetically in the default view (decending).
    return name.localeAwareCompare(group->GroupName()) >= 0;
}

// EntityItem

EntityItem::EntityItem(const EntityPtr &entity, EntityGroupItem *parentItem) :
    QTreeWidgetItem(parentItem),
    ptr(entity),
    id(entity->Id())
{
    setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable | Qt::ItemIsDragEnabled);

    if (parentItem)
        parentItem->AddEntityItem(this);

    SetText(entity.get());
}

EntityItem::~EntityItem()
{
    if (Parent())
        Parent()->RemoveEntityItem(this);
}

void EntityItem::Acked(const EntityPtr &entity)
{
    ptr = entity;
    id = entity->Id();

    UpdateText();
}

void EntityItem::UpdateText()
{
    SetText(ptr.lock().get());
}

void EntityItem::SetText(::Entity *entity)
{
    if (!entity)
    {
        setText(0, "Error: Invalid Entity");
        return;
    }
    if (ptr.lock().get() != entity)
        LogWarning("EntityItem::SetText: the entity given is different than the entity this item represents.");

    QString entName = entity->Name();
    QString name = QString("%1 %2").arg(entity->Id()).arg(entName.isEmpty() ? "(no name)" : entName);

    const bool local = entity->IsLocal();
    const bool temp = entity->IsTemporary();

    QString desc = "";
    QColor color = Qt::black;
    if (local)
    {
        color = Qt::blue;
        desc.append(localText);
    }
    if (temp)
    {
        color = Qt::red;
        if (!desc.isEmpty())
            desc.append(" ");
        desc.append(temporaryText);
    }
    QColor descColor = color;

    // If no local/temp description, show number of children.
    size_t numChildren = entity->NumChildren();
    if (numChildren > 0)
    {
        desc = (numChildren > 1 ? QString("%1 Children").arg(numChildren) : "1 Child");
        if (descColor == Qt::black)
            descColor = QColor(68,68,68);
    }

    if (text(0).compare(name, Qt::CaseSensitive) != 0)
        setText(0, name);
    if (text(1).compare(desc, Qt::CaseSensitive) != 0)
        setText(1, desc);

    if (textColor(0) != color)
        setTextColor(0, color);
    if (!desc.isEmpty() && textColor(1) != descColor)
        setTextColor(1, descColor);
}

EntityGroupItem *EntityItem::Parent() const
{
    return dynamic_cast<EntityGroupItem *>(parent());
}

EntityPtr EntityItem::Entity() const
{
    return ptr.lock();
}

entity_id_t EntityItem::Id() const
{
    return id;
}

bool EntityItem::operator <(const QTreeWidgetItem &other) const
{
    PROFILE(EntityItem_OperatorLessThan)

    // Entities never go before groups, even when sorting by name.
    const EntityGroupItem *group = dynamic_cast<const EntityGroupItem*>(&other);
    if (group)
        return true;

    SceneTreeWidget *tree = qobject_cast<SceneTreeWidget*>(treeWidget());
    if (!tree)
        return false;

    // We can only compare two EntityItems. However this can be a Component/AttributeItem
    // when Entity has children. In this case, those alway come before EntityItem so return true.
    const EntityItem *otherItem = dynamic_cast<const EntityItem*>(&other);
    if (!otherItem || !otherItem->Entity() || ptr.expired())
        return true;

    const SceneStructureWindow::SortCriteria criteria = tree->SortingCriteria();

    if (criteria == SceneStructureWindow::SortByType || criteria == SceneStructureWindow::SortById)
    {
        entity_id_t id_other = otherItem->Id();
        if (criteria == SceneStructureWindow::SortById)
            return id < id_other;
        else
        {
            bool replicated = (id < UniqueIdGenerator::FIRST_LOCAL_ID);
            bool replicated_other = (id_other < UniqueIdGenerator::FIRST_LOCAL_ID);
            bool temp = Entity()->IsTemporary();
            bool temp_other = otherItem->Entity()->IsTemporary();

            if ((replicated && !replicated_other) || (!temp && temp_other))
                return false;
            else if ((!replicated && replicated_other) || (temp && !temp_other))
                return true;
            // Both replicated, local or temp.
            else
                return id < id_other;
        }
    }
    else if (criteria == SceneStructureWindow::SortByName)
    {
        // Fast route for strings. Its more expensive to fetch EC_Name
        // and the name attribute from both.
        const QStringList text_me = text(0).split(" ");
        const QStringList text_other = other.text(0).split(" ");
        if (text_me.size() > 1 && text_other.size() > 1)
            return (text_me[1].localeAwareCompare(text_other[1]) < 0);
        else
            return false;
    }
    else
        return QTreeWidgetItem::operator <(other);
}

// ComponentItem

ComponentItem::ComponentItem(const ComponentPtr &comp, EntityItem *parent) :
    QTreeWidgetItem(parent),
    parentItem(parent),
    ptr(comp),
    typeId(comp->TypeId()),
    typeName(comp->TypeName()),
    name(comp->Name())
{
    setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);
    SetText(comp.get());
}

void ComponentItem::SetText(IComponent *comp)
{
    if (ptr.lock().get() != comp)
        LogWarning("ComponentItem::SetText: the component given is different than the component this item represents.");

    QString compType = IComponent::EnsureTypeNameWithoutPrefix(comp->TypeName());
    QString name = QString("%1 %2").arg(compType).arg(comp->Name());

    const bool parentLocal = comp->ParentEntity() && comp->ParentEntity()->IsLocal();
    const bool parentTemp = comp->ParentEntity() && comp->ParentEntity()->IsTemporary();
    const bool local = comp->IsLocal();
    const bool temp = comp->IsTemporary();

    QString desc = "";
    QColor color = Qt::black;
    if (local)
    {
        color = Qt::blue;
        if (parentLocal != local)
            desc.append(localText);
    }
    if (temp)
    {
        color = Qt::red;
        if (parentTemp != temp)
        {
            if (!desc.isEmpty())
                desc.append(" ");
            desc.append(temporaryText); 
        }            
    }
    if (comp->UpdateMode() == AttributeChange::LocalOnly)
    {
        if (!desc.isEmpty())
            desc.append(" ");
        desc.append(localOnlyText);
    }
    else if (comp->UpdateMode() == AttributeChange::Disconnected)
    {
        if (!desc.isEmpty())
            desc.append(" ");
        desc.append(disconnectedText);
    }
    if (color == Qt::red && desc == localText)
        color = Qt::blue;

    if (text(0).compare(name, Qt::CaseSensitive) != 0)
        setText(0, name);
    if (text(1).compare(desc, Qt::CaseSensitive) != 0)
        setText(1, desc);

    if (textColor(0) != color)
        setTextColor(0, color);
    if (!desc.isEmpty() && textColor(1) != color)
        setTextColor(1, color);
}

ComponentPtr ComponentItem::Component() const
{
    return ptr.lock();
}

EntityItem *ComponentItem::Parent() const
{
    return parentItem;
}

// AttributeItem

AttributeItem::AttributeItem(IAttribute *attr, QTreeWidgetItem *parent) :
    QTreeWidgetItem(parent),
    ptr(attr->Owner()->shared_from_this(), attr),
    index(-1)
{
    Update(attr);
}

void AttributeItem::Update(IAttribute *attr)
{
    if (attr != ptr.Get())
    {
        LogError("AttributeItem::Update: trying to update item with wrong attribute.");
        return;
    }

    type = attr->TypeName();
    name = attr->Name();
    id = attr->Id();
    value = attr->ToString();

    if (index > -1 && attr->TypeId() == cAttributeAssetReferenceList)
    {
        const AssetReferenceList &refs = static_cast<Attribute<AssetReferenceList> *>(attr)->Get();
        if (index < refs.Size())
            value = refs[index].ref;
        id = id + "[" + QString::number(index) + "]";
    }

    setText(0, QString("%1: %2").arg(id).arg(value));
}

// AssetRefItem

AssetRefItem::AssetRefItem(IAttribute *attr, QTreeWidgetItem *parent) :
    AttributeItem(attr, parent)
{
}

AssetRefItem::AssetRefItem(IAttribute *attr, int assetRefIndex, QTreeWidgetItem *parent) :
    AttributeItem(attr, parent)
{
    // Override the regular AssetReferenceList value "ref1;ref2;etc." with a single ref.
    index = assetRefIndex;
    Update(attr);
}

// SceneTreeWidgetSelection

bool SceneTreeWidgetSelection::IsEmpty() const
{
    return groups.isEmpty() && entities.isEmpty() && components.isEmpty() && assets.isEmpty();
}

bool SceneTreeWidgetSelection::HasGroups() const
{
    return !groups.isEmpty();
}

bool SceneTreeWidgetSelection::HasGroupsOnly() const
{
    return !groups.isEmpty() && entities.isEmpty() && components.isEmpty() && assets.isEmpty();
}

bool SceneTreeWidgetSelection::HasEntities() const
{
    return !entities.isEmpty();
}

bool SceneTreeWidgetSelection::HasEntitiesOnly() const
{
    return groups.isEmpty() && !entities.isEmpty() && components.isEmpty() && assets.isEmpty();
}

bool SceneTreeWidgetSelection::HasComponents() const
{
    return !components.isEmpty();
}

bool SceneTreeWidgetSelection::HasComponentsOnly() const
{
    return groups.isEmpty() && entities.isEmpty() && !components.isEmpty() && assets.isEmpty();
}

bool SceneTreeWidgetSelection::HasAssets() const
{
    return !assets.isEmpty();
}

bool SceneTreeWidgetSelection::HasAssetsOnly() const
{
    return groups.isEmpty() && entities.isEmpty() && components.isEmpty() && !assets.isEmpty();
}

int SceneTreeWidgetSelection::NumGroupChildren() const
{
    int num = 0;
    foreach(EntityGroupItem *group, groups)
        num += group->entityItems.size();
    return num;
}

QList<entity_id_t> SceneTreeWidgetSelection::EntityIds() const
{
    QSet<entity_id_t> ids;
    foreach(EntityGroupItem *g, groups)
        foreach(EntityItem *e, g->entityItems)
            ids.insert(e->Id());
    foreach(EntityItem *e, entities)
        ids.insert(e->Id());
    foreach(ComponentItem *c, components)
        ids.insert(c->Parent()->Id());
    return ids.toList();
}

// AssetItem

AssetItem::AssetItem(const AssetPtr &asset, QTreeWidgetItem *parent) :
    QTreeWidgetItem(parent),
    assetPtr(asset)
{
    SetText(asset.get());
}

AssetPtr AssetItem::Asset() const
{
    return assetPtr.lock();
}

void AssetItem::SetText(IAsset *asset)
{
    if (assetPtr.lock().get() != asset)
        LogWarning("AssetItem::SetText: the asset given is different than the asset this item represents.");

    QString name;
    QString subAssetName;
    AssetAPI::ParseAssetRef(asset->Name(), 0, 0, 0, 0, &name, 0, 0, &subAssetName);

    if (!subAssetName.isEmpty())
        name = subAssetName;
    else
    {
        // Find the topmost item.
        QTreeWidgetItem *p = parent();
        while (p != 0 && p->parent() != 0)
            p = p->parent();

        // If this item is in a storage item, trim away the base url to leave only the relative path from it.
        AssetStorageItem *storageItem = dynamic_cast<AssetStorageItem*>(p);
        if (storageItem && storageItem->Storage().get())
        {
            QString storageUrl = storageItem->Storage()->BaseURL();
            if (asset->Name().startsWith(storageUrl))
                name = asset->Name().right(asset->Name().length()-storageUrl.length());
            else
                name = asset->Name(); // Use full url ref as this asset does not belong to the top level parent storage.
        }
        else if (!subAssetName.isEmpty())
            name = subAssetName;
    }

    // "File missing" red
    // "No disk source" red
    // "Read-only" 
    // "Memory-only" red
    // "Unloaded " gray

    QString unloadedText = QApplication::translate("AssetItem", "Unloaded");
    QString fileMissingText = QApplication::translate("AssetItem", "File missing");
    QString noDiskSourceText = QApplication::translate("AssetItem", "No disk source");
    //QString readOnlyText = QApplication::translate("AssetItem", "Read-only");
    QString memoryOnlyText = QApplication::translate("AssetItem", "Memory-only");

    bool unloaded = !asset->IsLoaded();
    bool fileMissing = !asset->DiskSource().isEmpty() && asset->DiskSourceType() == IAsset::Original && !QFile::exists(asset->DiskSource());
    bool memoryOnly = asset->DiskSource().isEmpty() && !asset->GetAssetStorage() && asset->DiskSourceType() == IAsset::Programmatic;
    bool diskSourceMissing = asset->DiskSource().isEmpty();
    bool isModified = asset->IsModified();

    ///\todo Enable when source type is set properly for AssetCreated signal (see the bug in AssetAPI::CreateNewAsset).
//    if (!asset->DiskSource().isEmpty() && asset->DiskSourceType() == IAsset::Programmatic)
//        LogWarning("AssetItem::SetText: Encountered asset (" + asset->Name() +
//            ") which is programmatic but has also disk source " + asset->DiskSource().isEmpty() + ".");

/*
    LogInfo(QString("unloaded")+(unloaded?"1":"0"));
    LogInfo(QString("fileMissing")+(fileMissing?"1":"0"));
    LogInfo(QString("memoryOnly")+(memoryOnly?"1":"0"));
    LogInfo(QString("diskSourceMissing:")+(diskSourceMissing?"1":"0"));
    LogInfo(QString("isModified:")+(isModified?"1":"0"));
*/
    QString info;
    if (fileMissing)
    {
        setTextColor(0, QColor(Qt::red));
        info.append(fileMissingText);
    }
    if (!memoryOnly && diskSourceMissing)
    {
        setTextColor(0, QColor(Qt::red));
        if (!info.isEmpty())
            info.append(" ");
        info.append(noDiskSourceText);
    }
    if (!memoryOnly && unloaded)
    {
        setTextColor(0, QColor(Qt::gray));
        if (!info.isEmpty())
            info.append(" ");
        info.append(unloadedText);
    }
    if (memoryOnly)
    {
        setTextColor(0, QColor(Qt::darkCyan));
        if (!info.isEmpty())
            info.append(" ");
        info.append(memoryOnlyText);
    }

    if (isModified)
        name.append("*");
    if (!info.isEmpty())
    {
        info.prepend(" (");
        info.append(")");
        setText(0, name + info);
    }
    else
    {
        setTextColor(0, QColor(Qt::black));
        setText(0, name);
    }
}

// AssetStorageItem

AssetStorageItem::AssetStorageItem(const AssetStoragePtr &storage, QTreeWidgetItem *parent) :
    QTreeWidgetItem(parent),
    assetStorage(storage)
{
    setText(0, storage->ToString() + (!storage->Writable() ?  QApplication::translate("AssetStorageItem", " (Read-only)") : QString()));
}

AssetStoragePtr AssetStorageItem::Storage() const
{
    return assetStorage.lock();
}

// AssetBundleItem

AssetBundleItem::AssetBundleItem(const AssetBundlePtr &bundle, QTreeWidgetItem *parent) :
    QTreeWidgetItem(parent),
    assetBundle(bundle)
{
    QString name;
    AssetAPI::ParseAssetRef(bundle->Name(), 0, 0, 0, 0, &name);

    // Find the topmost item.
    QTreeWidgetItem *p = this->parent();
    while (p != 0 && p->parent() != 0)
        p = p->parent();

    // If this item is in a storage item, trim away the base url to leave only the relative path from it.
    AssetStorageItem *storageItem = dynamic_cast<AssetStorageItem*>(p);
    if (storageItem && storageItem->Storage().get())
    {
        QString storageUrl = storageItem->Storage()->BaseURL();
        if (bundle->Name().startsWith(storageUrl))
            name = bundle->Name().right(bundle->Name().length()-storageUrl.length());
    }

    int subAssetCount = bundle->SubAssetCount();
    if (subAssetCount != -1)
        name = QString("%1 (%2 assets)").arg(name).arg(subAssetCount);
    setText(0, name);
}

bool AssetBundleItem::Contains(const QString &assetRef) const
{
    // We could also query the bundle for this, but for some bundle types this might take a lot
    // of time. So lets do a starts with string check. This should not produce misses if AssetAPI has
    // parsed the asset ref correctly to the bundle and the asset itself.
    return (!assetBundle.expired() && assetRef.startsWith(assetBundle.lock()->Name(), Qt::CaseInsensitive));
}

AssetBundlePtr AssetBundleItem::AssetBundle() const
{
    return assetBundle.lock();
}

AssetStoragePtr AssetBundleItem::Storage() const
{
    if (assetBundle.expired())
        return AssetStoragePtr();
    return assetBundle.lock()->AssetStorage();
}

// AssetSelection

bool AssetTreeWidgetSelection::IsEmpty() const
{
    return assets.isEmpty() && storages.isEmpty();
}

bool AssetTreeWidgetSelection::HasAssets() const
{
    return !assets.isEmpty();
}

bool AssetTreeWidgetSelection::HasStorages() const
{
    return !storages.isEmpty();
}
