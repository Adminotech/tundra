// For conditions of distribution and use, see copyright notice in LICENSE

#include "StableHeaders.h"
#include "DebugOperatorNew.h"

#include "Entity.h"
#include "Scene/Scene.h"
#include "EC_Name.h"
#include "SceneAPI.h"

#include "Framework.h"
#include "IComponent.h"
#include "CoreStringUtils.h"
#include "LoggingFunctions.h"
#include "Profiler.h"

#include <QDomDocument>

#include <kNet/DataSerializer.h>
#include <kNet/DataDeserializer.h>

#include "MemoryLeakCheck.h"

Entity::Entity(Framework* framework, entity_id_t id, bool temporary, Scene* scene) :
    framework_(framework),
    id_(id),
    scene_(scene),
    temporary_(temporary)
{
    connect(this, SIGNAL(TemporaryStateToggled(Entity *, AttributeChange::Type)), scene_, SIGNAL(EntityTemporaryStateToggled(Entity *, AttributeChange::Type)));
}

Entity::~Entity()
{
    // If components still alive, they become free-floating
    for (ComponentMap::const_iterator i = components_.begin(); i != components_.end(); ++i)
        i->second->SetParentEntity(0);
   
    components_.clear();
    qDeleteAll(actions_);
}

void Entity::ChangeComponentId(component_id_t old_id, component_id_t new_id)
{
    if (old_id == new_id)
        return;
    
    ComponentPtr old_comp = ComponentById(old_id);
    if (!old_comp)
        return;
    
    if (ComponentById(new_id))
    {
        LogWarning("Purged component " + QString::number(new_id) + " to make room for a ChangeComponentId request. This should not happen.");
        RemoveComponentById(new_id, AttributeChange::LocalOnly);
    }
    
    old_comp->SetNewId(new_id);
    components_.erase(old_id);
    components_[new_id] = old_comp;
}

void Entity::AddComponent(const ComponentPtr &component, AttributeChange::Type change)
{
    AddComponent(0, component, change);
}

void Entity::AddComponent(component_id_t id, const ComponentPtr &component, AttributeChange::Type change)
{
    // Must exist and be free
    if (component && component->ParentEntity() == 0)
    {
        if (!id)
        {
            bool authority = true;
            if (scene_)
                authority = scene_->IsAuthority();
            // Loop until we find a free ID
            for (;;)
            {
                if (authority)
                    id = component->IsReplicated() ? idGenerator_.AllocateReplicated() : idGenerator_.AllocateLocal();
                else
                    id = component->IsReplicated() ? idGenerator_.AllocateUnacked() : idGenerator_.AllocateLocal();
                if (components_.find(id) == components_.end())
                    break;
            }
        }
        else
        {
            component->SetReplicated(id < UniqueIdGenerator::FIRST_LOCAL_ID);
            // If component ID is specified manually, but it already exists, it is an error. Do not add the component in that case.
            if (components_.find(id) != components_.end())
            {
                LogError("Can not add component: a component with id " + QString::number(id) + " already exists in entity " + ToString());
                return;
            }
            // Whenever a manual replicated ID is assigned, reset the ID generator to the highest value to avoid unnecessary free ID probing in the future
            if (id < UniqueIdGenerator::FIRST_LOCAL_ID)
                idGenerator_.ResetReplicatedId(std::max(id, idGenerator_.id));
        }

        // Register the dynamic property conveniency accessor for the component
        QString componentTypeName = IComponent::EnsureTypeNameWithoutPrefix(component->TypeName());
        componentTypeName.replace(0, 1, componentTypeName[0].toLower());
        // We already have 'name' property in Entity, so ignore "EC_Name" ("name") here.
        if (componentTypeName != "name" && !property(componentTypeName.toStdString().c_str()).isValid())
        {
            QVariant var = QVariant::fromValue<QObject*>(component.get());
            setProperty(componentTypeName.toStdString().c_str(), var); // 'lowerCamelCaseVersion'
            setProperty(componentTypeName.toLower().toStdString().c_str(), var); // old and ugly 'alllowercaseversion'
        }
        
        component->SetNewId(id);
        component->SetParentEntity(this);
        components_[id] = component;
        
        if (change != AttributeChange::Disconnected)
            emit ComponentAdded(component.get(), change == AttributeChange::Default ? component->UpdateMode() : change);
        if (scene_)
            scene_->EmitComponentAdded(this, component.get(), change);
    }
}

void Entity::RemoveComponent(const ComponentPtr &component, AttributeChange::Type change)
{
    if (component)
    {
        for(ComponentMap::iterator it = components_.begin(); it != components_.end(); ++it)
            if (it->second == component)
            {
                RemoveComponent(it, change);
                return;
            }

        LogWarning(QString("Entity::RemoveComponent: Failed to find %1 \"%2\" from %3.")
            .arg(component->TypeName()).arg(component->Name()).arg(ToString()));
    }
}

void Entity::RemoveAllComponents(AttributeChange::Type change)
{
    while(!components_.empty())
    {
        if (components_.begin()->first != components_.begin()->second->Id())
            LogWarning("Component ID mismatch on RemoveAllComponents: map key " + QString::number(components_.begin()->first) + " component ID " + QString::number(components_.begin()->second->Id()));
        RemoveComponent(components_.begin(), change);
    }
}

void Entity::RemoveComponent(ComponentMap::iterator iter, AttributeChange::Type change)
{
    const ComponentPtr& component = iter->second;

    // Unregister/invalidate dynamic property accessor
    QString componentTypeName = IComponent::EnsureTypeNameWithoutPrefix(component->TypeName());
    componentTypeName.replace(0, 1, componentTypeName[0].toLower());
    std::string propertyName = componentTypeName.toStdString();
   // 'lowerCamelCaseVersion'
    if (property(propertyName.c_str()).isValid())
    {
        //Make sure that QObject is inherited by the IComponent and that the name is matching in case there are many of same type of components in entity.
        QObject *obj = property(propertyName.c_str()).value<QObject*>();
        if (qobject_cast<IComponent*>(obj) && static_cast<IComponent*>(obj)->Name() == component->Name())
            setProperty(propertyName.c_str(), QVariant());
    }
    // old and ugly 'alllowercaseversion'
    propertyName = componentTypeName.toLower().toStdString();
    if (property(propertyName.c_str()).isValid())
    {
        QObject *obj = property(propertyName.c_str()).value<QObject*>();
        if (qobject_cast<IComponent*>(obj) && static_cast<IComponent*>(obj)->Name() == component->Name())
            setProperty(propertyName.c_str(), QVariant());
    }

    if (change != AttributeChange::Disconnected)
        emit ComponentRemoved(iter->second.get(), change == AttributeChange::Default ? component->UpdateMode() : change);
    if (scene_)
        scene_->EmitComponentRemoved(this, iter->second.get(), change);

    iter->second->SetParentEntity(0);
    components_.erase(iter);
}


void Entity::RemoveComponentById(component_id_t id, AttributeChange::Type change)
{
    ComponentPtr comp = ComponentById(id);
    if (comp)
        RemoveComponent(comp, change);
}

size_t Entity::RemoveComponents(const QString &typeName, AttributeChange::Type change)
{
    QList<component_id_t> removeIds;
    for(ComponentMap::const_iterator it = components_.begin(); it != components_.end(); ++it)
        if (it->second->TypeName().compare(typeName) == 0)
            removeIds << it->first;
            
    foreach(component_id_t id, removeIds)
        RemoveComponentById(id, change);
    
    return removeIds.size();
}

size_t Entity::RemoveComponents(u32 typeId, AttributeChange::Type change)
{
    QList<component_id_t> removeIds;
    for(ComponentMap::const_iterator it = components_.begin(); it != components_.end(); ++it)
        if (it->second->TypeId() == typeId)
            removeIds << it->first;

    foreach(component_id_t id, removeIds)
        RemoveComponentById(id, change);

    return removeIds.size();
}

void Entity::RemoveComponentRaw(QObject* comp)
{
    LogWarning("Entity::RemoveComponentRaw: This function is deprecated and will be removed. Use RemoveComponent or RemoveComponentById instead.");
    IComponent* compPtr = qobject_cast<IComponent*>(comp);
    if (compPtr)
        RemoveComponent(compPtr->shared_from_this());
}

ComponentPtr Entity::GetOrCreateComponent(const QString &type_name, AttributeChange::Type change, bool replicated)
{
    ComponentPtr existing = Component(type_name);
    if (existing)
        return existing;

    return CreateComponent(type_name, change, replicated);
}

ComponentPtr Entity::GetOrCreateComponent(const QString &type_name, const QString &name, AttributeChange::Type change, bool replicated)
{
    ComponentPtr existing = Component(type_name, name);
    if (existing)
        return existing;

    return CreateComponent(type_name, name, change, replicated);
}

ComponentPtr Entity::GetOrCreateComponent(u32 typeId, AttributeChange::Type change, bool replicated)
{
    ComponentPtr existing = Component(typeId);
    if (existing)
        return existing;

    return CreateComponent(typeId, change, replicated);
}

ComponentPtr Entity::GetOrCreateComponent(u32 typeId, const QString &name, AttributeChange::Type change, bool replicated)
{
    ComponentPtr new_comp = Component(typeId, name);
    if (new_comp)
        return new_comp;

    return CreateComponent(typeId, name, change, replicated);
}

ComponentPtr Entity::GetOrCreateLocalComponent(const QString& type_name)
{
    return GetOrCreateComponent(type_name, AttributeChange::LocalOnly, false);
}

ComponentPtr Entity::GetOrCreateLocalComponent(const QString& type_name, const QString& name)
{
    return GetOrCreateComponent(type_name, name, AttributeChange::LocalOnly, false);
}

ComponentPtr Entity::CreateComponent(const QString &type_name, AttributeChange::Type change, bool replicated)
{
    ComponentPtr new_comp = framework_->Scene()->CreateComponentByName(scene_, type_name);
    if (!new_comp)
    {
        LogError("Failed to create a component of type \"" + type_name + "\" to " + ToString());
        return ComponentPtr();
    }

    // If changemode is default, and new component requests to not be replicated by default, honor that
    if (change != AttributeChange::Default || new_comp->IsReplicated())
        new_comp->SetReplicated(replicated);
    
    AddComponent(new_comp, change);
    return new_comp;
}

ComponentPtr Entity::CreateComponent(const QString &type_name, const QString &name, AttributeChange::Type change, bool replicated)
{
    ComponentPtr new_comp = framework_->Scene()->CreateComponentByName(scene_, type_name, name);
    if (!new_comp)
    {
        LogError("Failed to create a component of type \"" + type_name + "\" and name \"" + name + "\" to " + ToString());
        return ComponentPtr();
    }

    // If changemode is default, and new component requests to not be replicated by default, honor that
    if (change != AttributeChange::Default || new_comp->IsReplicated())
        new_comp->SetReplicated(replicated);
    
    AddComponent(new_comp, change);
    return new_comp;
}

ComponentPtr Entity::CreateComponent(u32 typeId, AttributeChange::Type change, bool replicated)
{
    ComponentPtr new_comp = framework_->Scene()->CreateComponentById(scene_, typeId);
    if (!new_comp)
    {
        LogError("Failed to create a component of type id " + QString::number(typeId) + " to " + ToString());
        return ComponentPtr();
    }

    // If changemode is default, and new component requests to not be replicated by default, honor that
    if (change != AttributeChange::Default || new_comp->IsReplicated())
        new_comp->SetReplicated(replicated);
    
    AddComponent(new_comp, change);
    return new_comp;
}

ComponentPtr Entity::CreateComponent(u32 typeId, const QString &name, AttributeChange::Type change, bool replicated)
{
    ComponentPtr new_comp = framework_->Scene()->CreateComponentById(scene_, typeId, name);
    if (!new_comp)
    {
        LogError("Failed to create a component of type id " + QString::number(typeId) + " and name \"" + name + "\" to " + ToString());
        return ComponentPtr();
    }

    // If changemode is default, and new component requests to not be replicated by default, honor that
    if (change != AttributeChange::Default || new_comp->IsReplicated())
        new_comp->SetReplicated(replicated);
    
    AddComponent(new_comp, change);
    return new_comp;
}

ComponentPtr Entity::CreateComponentWithId(component_id_t compId, u32 typeId, const QString &name, AttributeChange::Type change)
{
    ComponentPtr new_comp = framework_->Scene()->CreateComponentById(scene_, typeId, name);
    if (!new_comp)
    {
        LogError("Failed to create a component of type id " + QString::number(typeId) + " and name \"" + name + "\" to " + ToString());
        return ComponentPtr();
    }

    // If this overload is called with id 0, it must come from SyncManager (server). In that case make sure we do not allow the component to be created as local
    if (!compId)
        new_comp->SetReplicated(true);

    AddComponent(compId, new_comp, change);
    return new_comp;
}

ComponentPtr Entity::CreateLocalComponent(const QString &type_name)
{
    return CreateComponent(type_name, AttributeChange::LocalOnly, false);
}

ComponentPtr Entity::CreateLocalComponent(const QString &type_name, const QString &name)
{
    return CreateComponent(type_name, name, AttributeChange::LocalOnly, false);
}

ComponentPtr Entity::ComponentById(component_id_t id) const
{
    ComponentMap::const_iterator i = components_.find(id);
    return (i != components_.end() ? i->second : ComponentPtr());
}

ComponentPtr Entity::Component(const QString &typeName) const
{
    const QString ecTypeName = IComponent::EnsureTypeNameWithPrefix(typeName);
    for (ComponentMap::const_iterator i = components_.begin(); i != components_.end(); ++i)
        if (i->second->TypeName() == ecTypeName)
            return i->second;

    return ComponentPtr();
}

ComponentPtr Entity::Component(u32 typeId) const
{
    for (ComponentMap::const_iterator i = components_.begin(); i != components_.end(); ++i)
        if (i->second->TypeId() == typeId)
            return i->second;

    return ComponentPtr();
}

Entity::ComponentVector Entity::ComponentsOfType(const QString &typeName) const
{
    return ComponentsOfType(framework_->Scene()->GetComponentTypeId(typeName));
}

Entity::ComponentVector Entity::ComponentsOfType(u32 typeId) const
{
    ComponentVector ret;
    for (ComponentMap::const_iterator i = components_.begin(); i != components_.end(); ++i)
        if (i->second->TypeId() == typeId)
            ret.push_back(i->second);
    return ret;
}

ComponentPtr Entity::Component(const QString &type_name, const QString& name) const
{
    const QString ecTypeName = IComponent::EnsureTypeNameWithPrefix(type_name);
    for (ComponentMap::const_iterator i = components_.begin(); i != components_.end(); ++i)
        if (i->second->TypeName() == ecTypeName && i->second->Name() == name)
            return i->second;

    return ComponentPtr();
}

ComponentPtr Entity::Component(u32 typeId, const QString& name) const
{
    for (ComponentMap::const_iterator i = components_.begin(); i != components_.end(); ++i)
        if (i->second->TypeId() == typeId && i->second->Name() == name)
            return i->second;

    return ComponentPtr();
}

QObjectList Entity::GetComponentsRaw(const QString &type_name) const
{
    LogWarning("Entity::GetComponentsRaw: This function is deprecated and will be removed. Use GetComponents or Components instead.");
    QObjectList ret;
    if (type_name.isNull())
        for (ComponentMap::const_iterator i = components_.begin(); i != components_.end(); ++i)
            ret.push_back(i->second.get());
    else
        for (ComponentMap::const_iterator i = components_.begin(); i != components_.end(); ++i)
            if (i->second->TypeName() == type_name)
                ret.push_back(i->second.get());
    return ret;
}

void Entity::SerializeToBinary(kNet::DataSerializer &dst, bool serializeTemporary, bool serializeLocal, bool serializeChildren) const
{
    dst.Add<u32>(Id());
    dst.Add<u8>(IsReplicated() ? 1 : 0);
    std::vector<ComponentPtr> serializable;
    std::vector<EntityPtr> serializableChildren;
    for (ComponentMap::const_iterator i = components_.begin(); i != components_.end(); ++i)
        if (i->second->ShouldBeSerialized(serializeTemporary, serializeLocal))
            serializable.push_back(i->second);

    if (serializeChildren)
    {
        foreach(const EntityWeakPtr &childWeak, children_)
        {
            const EntityPtr child = childWeak.lock();
            if (child && child->ShouldBeSerialized(serializeTemporary, serializeLocal, serializeChildren))
                serializableChildren.push_back(child);
        }
    }

    /// \hack Retain binary compatibility with earlier scene format, at the cost of max. 65535 components or child entities
    if (serializable.size() > 0xffff)
        LogError("Entity::SerializeToBinary: entity contains more than 65535 components, binary save will be erroneous");
    if (serializableChildren.size() > 0xffff)
        LogError("Entity::SerializeToBinary: entity contains more than 65535 child entities, binary save will be erroneous");

    dst.Add<u32>(serializable.size() | (serializableChildren.size() << 16));
    foreach(const ComponentPtr &comp, serializable)
    {
        dst.Add<u32>(comp->TypeId()); ///\todo VLE this!
        dst.AddString(comp->Name().toStdString());
        dst.Add<u8>(comp->IsReplicated() ? 1 : 0);

        // Write each component to a separate buffer, then write out its size first, so we can skip unknown components
        QByteArray comp_bytes;
        // Assume 64KB max per component for now
        comp_bytes.resize(64 * 1024);
        kNet::DataSerializer comp_dest(comp_bytes.data(), comp_bytes.size());
        comp->SerializeToBinary(comp_dest);
        comp_bytes.resize((int)comp_dest.BytesFilled());

        dst.Add<u32>(comp_bytes.size());
        dst.AddArray<u8>((const u8*)comp_bytes.data(), comp_bytes.size());
    }

    // Serialize child entities
    if (serializeChildren)
    {
        foreach(const EntityPtr child, serializableChildren)
            child->SerializeToBinary(dst, serializeTemporary, true);
    }
}

/* Disabled for now, since have to decide how entityID conflicts are handled.
void Entity::DeserializeFromBinary(kNet::DataDeserializer &src, AttributeChange::Type change)
{
}*/

void Entity::SerializeToXML(QDomDocument &doc, QDomElement &base_element, bool serializeTemporary, bool serializeLocal, bool serializeChildren) const
{
    QDomElement entity_elem = doc.createElement("entity");
    entity_elem.setAttribute("id", QString::number(Id()));
    entity_elem.setAttribute("sync", BoolToString(IsReplicated()));
    if (serializeTemporary)
        entity_elem.setAttribute("temporary", BoolToString(IsTemporary()));

    for (ComponentMap::const_iterator i = components_.begin(); i != components_.end(); ++i)
        if (i->second->ShouldBeSerialized(serializeTemporary, serializeLocal))
            i->second->SerializeTo(doc, entity_elem, serializeTemporary);

    // Serialize child entities
    if (serializeChildren)
    {
        for(ChildEntityVector::const_iterator i = children_.begin(); i != children_.end(); ++i)
        {
            const EntityPtr child = i->lock();
            if (child && child->ShouldBeSerialized(serializeTemporary, serializeLocal, serializeChildren))
                child->SerializeToXML(doc, entity_elem, serializeTemporary, serializeLocal);
        }
    }

    if (!base_element.isNull())
        base_element.appendChild(entity_elem);
    else
        doc.appendChild(entity_elem);
}

/* Disabled for now, since have to decide how entityID conflicts are handled.
void Entity::DeserializeFromXML(QDomElement& element, AttributeChange::Type change)
{
}*/

QString Entity::SerializeToXMLString(bool serializeTemporary, bool serializeLocal, bool serializeChildren, bool createSceneElement) const
{
    if (createSceneElement)
    {
        QDomDocument sceneDoc("Scene");
        QDomElement sceneElem = sceneDoc.createElement("scene");
        SerializeToXML(sceneDoc, sceneElem, serializeTemporary, serializeLocal, serializeChildren);
        sceneDoc.appendChild(sceneElem);
        return sceneDoc.toString();
    }
    else
    {
        QDomDocument entity_doc("Entity");
        QDomElement null_elem;
        SerializeToXML(entity_doc, null_elem, serializeTemporary, serializeLocal, serializeChildren);
        return entity_doc.toString();
    }
}

/* Disabled for now, since have to decide how entityID conflicts are handled.
bool Entity::DeserializeFromXMLString(const QString &src, AttributeChange::Type change)
{
    QDomDocument entityDocument("Entity");
    if (!entityDocument.setContent(src))
    {
        LogError("Parsing entity XML from text failed.");
        return false;
    }

    return CreateContentFromXml(entityDocument, replaceOnConflict, change);
}*/

EntityPtr Entity::Clone(bool local, bool temporary, const QString &cloneName, AttributeChange::Type changeType) const
{
    QDomDocument doc("Scene");
    QDomElement sceneElem = doc.createElement("scene");
    QDomElement entityElem = doc.createElement("entity");
    entityElem.setAttribute("sync", BoolToString(!local));
    entityElem.setAttribute("id", local ? scene_->NextFreeIdLocal() : scene_->NextFreeId());
    // Set the temporary status in advance so it's valid when Scene::CreateContentFromXml signals changes in the scene
    entityElem.setAttribute("temporary", BoolToString(temporary));
    // Setting of a new name for the clone is a bit clumsy, but this is the best way to do it currently.
    const bool setNameForClone = !cloneName.isEmpty();
    bool cloneNameWritten = false;
    for(ComponentMap::const_iterator i = components_.begin(); i != components_.end(); ++i)
    {
        i->second->SerializeTo(doc, entityElem);
        if (setNameForClone && !cloneNameWritten && i->second->TypeId() == EC_Name::ComponentTypeId)
        {
            // Now that we've serialized the Name component, overwrite value of the "name" attribute.
            QDomElement nameComponentElem = entityElem.lastChildElement();
            nameComponentElem.firstChildElement().setAttribute("value", cloneName);
            cloneNameWritten = true;
        }
    }
    // Serialize child entities
    for(ChildEntityVector::const_iterator i = children_.begin(); i != children_.end(); ++i)
        if (!i->expired())
            i->lock()->SerializeToXML(doc, entityElem, true, true);

    sceneElem.appendChild(entityElem);
    doc.appendChild(sceneElem);

    QList<Entity *> newEntities = scene_->CreateContentFromXml(doc, true, changeType);
    // Set same parent for the new entity as the original has
    if (newEntities.size())
        newEntities[0]->SetParent(Parent(), changeType);

    return (!newEntities.isEmpty() && newEntities.first() ? newEntities.first()->shared_from_this() : EntityPtr());
}

void Entity::SetName(const QString &name)
{
    shared_ptr<EC_Name> comp = GetOrCreateComponent<EC_Name>();
    assert(comp);
    comp->name.Set(name, AttributeChange::Default);
}

QString Entity::Name() const
{
    shared_ptr<EC_Name> name = Component<EC_Name>();
    return name ? name->name.Get() : "";
}

void Entity::SetDescription(const QString &desc)
{
    shared_ptr<EC_Name> comp = GetOrCreateComponent<EC_Name>();
    assert(comp);
    comp->description.Set(desc, AttributeChange::Default);
}

QString Entity::Description() const
{
    shared_ptr<EC_Name> name = Component<EC_Name>();
    return name ? name->description.Get() : "";
}

void Entity::SetGroup(const QString &groupName)
{
    shared_ptr<EC_Name> comp = GetOrCreateComponent<EC_Name>();
    comp->group.Set(groupName, AttributeChange::Default);
}

QString Entity::Group() const
{
    shared_ptr<EC_Name> comp = Component<EC_Name>();
    return comp ? comp->group.Get() : "";
}

EntityAction *Entity::Action(const QString &name)
{
    foreach(EntityAction *action, actions_)
        if (action->Name().compare(name, Qt::CaseInsensitive) == 0)
            return action;

    EntityAction *action = new EntityAction(name);
    actions_.insert(name, action);
    return action;
}

void Entity::RemoveAction(const QString &name)
{
    ActionMap::iterator iter = actions_.find(name);
    if (iter != actions_.end())
    {
        (*iter)->deleteLater();
        actions_.erase(iter);
    }
}

void Entity::ConnectAction(const QString &name, const QObject *receiver, const char *member)
{
    EntityAction *action = Action(name);
    assert(action);
    connect(action, SIGNAL(Triggered(QString, QString, QString, QStringList)), receiver, member, Qt::UniqueConnection);
}

void Entity::Exec(EntityAction::ExecTypeField type, const QString &action, const QString &p1, const QString &p2, const QString &p3)
{
    Exec(type, action, QStringList(QStringList() << p1 << p2 << p3));
}

void Entity::Exec(EntityAction::ExecTypeField type, const QString &action, const QStringList &params)
{
    PROFILE(Entity_ExecEntityAction);
    
    EntityAction *act = Action(action);
    if ((type & EntityAction::Local) != 0)
    {
        if (params.size() == 0)
            act->Trigger();
        else if (params.size() == 1)
            act->Trigger(params[0]);
        else if (params.size() == 2)
            act->Trigger(params[0], params[1]);
        else if (params.size() == 3)
            act->Trigger(params[0], params[1], params[2]);
        else if (params.size() >= 4)
            act->Trigger(params[0], params[1], params[2], params.mid(3));
    }

    if (ParentScene())
        ParentScene()->EmitActionTriggered(this, action, params, type);
}

void Entity::Exec(EntityAction::ExecTypeField type, const QString &action, const QVariantList &params)
{
    QStringList stringParams;
    foreach(QVariant var, params)
        stringParams << var.toString();
    Exec(type, action, stringParams);
}

void Entity::EmitEntityRemoved(AttributeChange::Type change)
{
    if (change == AttributeChange::Disconnected)
        return;
    if (change == AttributeChange::Default)
        change = AttributeChange::Replicate;
    emit EntityRemoved(this, change);
}

void Entity::EmitEnterView(IComponent* camera)
{
    emit EnterView(camera);
}

void Entity::EmitLeaveView(IComponent* camera)
{
    emit LeaveView(camera);
}

void Entity::SetTemporary(bool enable, AttributeChange::Type change)
{
    if (enable != temporary_)
    {
        temporary_ = enable;
        if (change == AttributeChange::Default)
            change = AttributeChange::Replicate;
        if (change != AttributeChange::Disconnected)
            emit TemporaryStateToggled(this, change);
    }
}

QString Entity::ToString() const
{
    QString name = Name();
    if (name.trimmed().isEmpty())
        return QString("Entity ID ") + QString::number(Id());
    else
        return QString("Entity \"") + name + "\" (ID: " + QString::number(Id()) + ")";
}

QObjectList Entity::ComponentsList() const
{
    LogWarning("Entity::ComponentsList: this function is deprecated and will be removed. Use Entity::Components instead");
    QObjectList compList;
    for (ComponentMap::const_iterator i = components_.begin(); i != components_.end(); ++i)
        if (i->second.get())
            compList << i->second.get();
    return compList;
}

void Entity::AddChild(EntityPtr child, AttributeChange::Type change)
{
    if (!child)
    {
        LogWarning("Entity::AddChild: null child entity specified");
        return;
    }

    child->SetParent(this->shared_from_this(), change);
}

void Entity::RemoveChild(EntityPtr child, AttributeChange::Type change)
{
    if (child->Parent().get() != this)
    {
        LogWarning("Entity::RemoveChild: the specified entity is not parented to this entity");
        return;
    }

    // RemoveEntity does a silent SetParent(0) to remove the child from the parent's child vector
    if (scene_)
        scene_->RemoveEntity(child->Id(), change);
    else
        LogError("Entity::RemoveChild: null parent scene, can not remove the entity from scene");
}

void Entity::RemoveAllChildren(AttributeChange::Type change)
{
    while (children_.size())
        RemoveChild(children_[0].lock(), change);
}

void Entity::DetachChild(EntityPtr child, AttributeChange::Type change)
{
    if (!child)
    {
        LogWarning("Entity::DetachChild: null child entity specified");
        return;
    }
    if (child->Parent().get() != this)
    {
        LogWarning("Entity::DetachChild: the specified entity is not parented to this entity");
        return;
    }

    child->SetParent(EntityPtr(), change);
}

void Entity::SetParent(EntityPtr parent, AttributeChange::Type change)
{
    EntityPtr oldParent = parent_.lock();
    if (oldParent == parent)
        return; // Nothing to do

    // Prevent self assignment
    if (parent.get() == this)
    {
        LogError("Entity::SetParent: self parenting attempted.");
        return;
    }

    // Prevent cyclic assignment
    if (parent)
    {
        Entity* parentCheck = parent.get();
        while (parentCheck)
        {
            if (parentCheck == this)
            {
                LogError("Entity::SetParent: Cyclic parenting attempted.");
                return;
            }
            parentCheck = parentCheck->Parent().get();
        }
    }

    // Remove from old parent's child vector
    if (oldParent)
    {
        ChildEntityVector& children = oldParent->children_;
        for (size_t i = 0; i < children.size(); ++i)
        {
            if (children[i].lock().get() == this)
            {
                children.erase(children.begin() + i);
                break;
            }
        }
    }

    // Add to new parent's child vector
    if (parent)
        parent->children_.push_back(shared_from_this());

    parent_ = parent;

    // Emit change signals
    if (change != AttributeChange::Disconnected)
    {
        if (change == AttributeChange::Default)
            change = IsLocal() ? AttributeChange::LocalOnly : AttributeChange::Replicate;
        emit ParentChanged(this, parent.get(), change);
        if (scene_)
            scene_->EmitEntityParentChanged(this, parent.get(), change);
    }
}

EntityPtr Entity::CreateChild(entity_id_t id, const QStringList &components, AttributeChange::Type change, bool replicated, bool componentsReplicated, bool temporary)
{
    if (!scene_)
    {
        LogError("Entity::CreateChild: not attached to a scene, can not create a child entity");
        return EntityPtr();
    }

    EntityPtr child = scene_->CreateEntity(id, components, change, replicated, componentsReplicated, temporary);
    // Set the parent silently to match entity creation signaling, which is only done at the end of the frame
    if (child)
        child->SetParent(this->shared_from_this(), AttributeChange::Disconnected);
    return child;
}

EntityPtr Entity::CreateLocalChild(const QStringList &components, AttributeChange::Type change, bool componentsReplicated, bool temporary)
{
    if (!scene_)
    {
        LogError("Entity::CreateLocalChild: not attached to a scene, can not create a child entity");
        return EntityPtr();
    }

    EntityPtr child = scene_->CreateLocalEntity(components, change, componentsReplicated, temporary);
    if (child)
        child->SetParent(this->shared_from_this(), change);
    return child;
}

EntityPtr Entity::Child(size_t index) const
{
    return index < children_.size() ? children_[index].lock() : EntityPtr();
}

EntityPtr Entity::ChildByName(const QString& name, bool recursive) const
{
    for (size_t i = 0; i < children_.size(); ++i)
    {
        // Safeguard in case the entity has become null without us knowing
        EntityPtr child = children_[i].lock();
        if (child)
        {
            if (QString::compare(name, child->Name(), Qt::CaseInsensitive) == 0)
                return child;
            if (recursive)
            {
                EntityPtr childResult = child->ChildByName(name, true);
                if (childResult)
                    return childResult;
            }
        }
    }

    return EntityPtr();
}

EntityList Entity::Children(bool recursive) const
{
    EntityList ret;
    CollectChildren(ret, recursive);
    return ret;
}

bool Entity::ShouldBeSerialized(bool serializeTemporary, bool serializeLocal, bool serializeChildren) const
{
    if (IsTemporary() && !serializeTemporary)
        return false;
    if (IsLocal() && !serializeLocal)
        return false;
    if (Parent() && !serializeChildren)
        return false;
    return true;
}

void Entity::CollectChildren(EntityList& children, bool recursive) const
{
    for (size_t i = 0; i < children_.size(); ++i)
    {
        // Safeguard in case the entity has become null without us knowing
        EntityPtr child = children_[i].lock();
        if (child)
        {
            children.push_back(child);
            if (recursive)
                child->CollectChildren(children, true);
        }
    }
}
