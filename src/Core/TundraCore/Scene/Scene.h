// For conditions of distribution and use, see copyright notice in LICENSE

#pragma once

#include "TundraCoreApi.h"
#include "CoreDefines.h"
#include "SceneFwd.h"
#include "AttributeChangeType.h"
#include "EntityAction.h"
#include "UniqueIdGenerator.h"
#include "Math/float3.h"
#include "SceneDesc.h"
#include "Entity.h"

#include <QObject>
#include <QVariant>

#include <map>

class Framework;
/// @todo Not nice: UserConnection is a class from TundraProtocolModule, so Scene core API "depends" on it currently.
/// Maybe have some kind of UserConnection interface class defined in Framework and use that instead.
class UserConnection;
class QDomDocument;

/// A collection of entities which form an observable world.
/** Acts as a factory for all entities.
    Has subsystem-specific worlds, such as rendering and physics, as dynamic properties.

    To create, access and remove scenes, see SceneAPI.

    \ingroup Scene_group */
class TUNDRACORE_API Scene : public QObject, public enable_shared_from_this<Scene>
{
    Q_OBJECT
    Q_PROPERTY(QString name READ Name) /**< @copydoc Name */
    Q_PROPERTY(bool viewEnabled READ ViewEnabled) /**< @copydoc ViewEnabled */
    Q_PROPERTY(bool authority READ IsAuthority) /**< @copydoc IsAuthority */
    Q_PROPERTY(EntityMap entities READ Entities) /**< @copydoc Entities */
    Q_PROPERTY(float3 up READ UpVector) /**< @copydoc UpVector */
    Q_PROPERTY(float3 right READ RightVector) /**< @copydoc RightVector */
    Q_PROPERTY(float3 forward READ ForwardVector) /**< @copydoc ForwardVector */

public:
    ~Scene();

    typedef std::map<entity_id_t, EntityPtr> EntityMap; ///< Maps entities to their unique IDs.
    typedef EntityMap::iterator iterator; ///< entity iterator, see begin() and end()
    typedef EntityMap::const_iterator const_iterator; ///< const entity iterator. see begin() and end()
    typedef QHash<entity_id_t, entity_id_t> EntityIdMap; ///< Used to map entity ID changes (oldId, newId).

    /// Returns name of the scene.
    const QString &Name() const { return name_; }

    /// Returns iterator to the beginning of the entities.
    iterator begin() { return iterator(entities_.begin()); }

    /// Returns iterator to the end of the entities.
    iterator end() { return iterator(entities_.end()); }

    /// Returns constant iterator to the beginning of the entities.
    const_iterator begin() const { return const_iterator(entities_.begin()); }

    /// Returns constant iterator to the end of the entities.
    const_iterator end() const { return const_iterator(entities_.end()); }

    /// Returns entity map for introspection purposes
    const EntityMap &Entities() const { return entities_; }

    /// Returns true if the two scenes have the same name
    bool operator == (const Scene &other) const { return Name() == other.Name(); }

    /// Returns true if the two scenes have different names
    bool operator != (const Scene &other) const { return !(*this == other); }

    /// Order by scene name
    bool operator < (const Scene &other) const { return Name() < other.Name(); }

    /// Return a subsystem world (OgreWorld, PhysicsWorld)
    template <class T>
    shared_ptr<T> Subsystem() const;

    /// Forcibly changes id of an existing entity. If there already is an entity with the new id, it will be purged
    /** @note Called by scenesync. This will not trigger any signals
        @param old_id Old id of the existing entity
        @param new_id New id to set */
    void ChangeEntityId(entity_id_t old_id, entity_id_t new_id);

    /// Starts an attribute interpolation
    /** @param attr Attribute inside a static-structured component.
        @param endvalue Same kind of attribute holding the endpoint value. You must dynamically allocate this yourself, but Scene
               will always take care of deleting it.
        @param length Time length
        @return true if successful (attribute must be in interpolated mode (set in metadata), must be in component, component 
                must be static-structured, component must be in an entity which is in a scene, scene must be us) */
    bool StartAttributeInterpolation(IAttribute* attr, IAttribute* endvalue, float length);

    /// Ends an attribute interpolation. The last set value will remain.
    /** @param attr Attribute inside a static-structured component.
        @return true if an interpolation existed */
    bool EndAttributeInterpolation(IAttribute* attr);

    /// Ends all attribute interpolations
    void EndAllAttributeInterpolations();

    /// Processes all running attribute interpolations. LocalOnly change will be used.
    /** @param frametime Time step */
    void UpdateAttributeInterpolations(float frametime);

    /// See if scene is currently performing interpolations, to differentiate between interpolative & non-interpolative attribute changes.
    bool IsInterpolating() const { return interpolating_; }

    /// Returns Framework
    Framework *GetFramework() const { return framework_; }

    /// Inspects file and returns a scene description structure from the contents of XML file.
    /** @param filename File name.
        @param resolveAssets If SceneDesc::assets map should be populated.
        Resolving assets under certain storage setups can perform poorly.
        You should only set true if really interested in all the assets and their disk paths. */
    SceneDesc CreateSceneDescFromXml(const QString &filename, bool resolveAssets = true) const;
    /// @overload
    /** @param data XML data to be processed.
        @param sceneDesc Initialized SceneDesc with filename prepared.
        @param resolveAssets If SceneDesc::assets map should be populated.
        Resolving assets under certain storage setups can perform poorly.
        You should only set true if really interested in all the assets and their disk paths. */
    SceneDesc CreateSceneDescFromXml(QByteArray &data, SceneDesc &sceneDesc, bool resolveAssets = true) const;

    /// Inspects file and returns a scene description structure from the contents of binary file.
    /** @param filename File name.
        @param resolveAssets If SceneDesc::assets map should be populated.
        Resolving assets under certain storage setups can perform poorly.
        You should only set true if really interested in all the assets and their disk paths. */
    SceneDesc CreateSceneDescFromBinary(const QString &filename, bool resolveAssets = true) const;
    /// @overload
    /** @param data Binary data to be processed.
        @param resolveAssets If SceneDesc::assets map should be populated.
        Resolving assets under certain storage setups can perform poorly.
        You should only set true if really interested in all the assets and their disk paths. */
    SceneDesc CreateSceneDescFromBinary(QByteArray &data, SceneDesc &sceneDesc, bool resolveAssets = true) const;

    /// Inspects .js file content for dependencies and adds them to sceneDesc.assets
    ///@todo This function is a duplicate copy of void ScriptAsset::ParseReferences(). Delete this code. -jj.
    /** @param filePath. Path to the file that is opened for inspection.
        @param SceneDesc. Scene description struct ref, found asset dependencies will be added here.
        @todo Make one implementation of this to a common place that EC_Script and Scene can use.
        @note If the way we introduce js dependencies (!ref: and engine.IncludeFile()) changes, this function needs to change too. */
    void SearchScriptAssetDependencies(const QString &filePath, SceneDesc &sceneDesc) const;

    /// Creates scene content from scene description.
    /** @param desc Scene description.
        @param useEntityIDsFromFile If true, the created entities will use the Entity IDs from the original file.
                  If the scene contains any previous entities with conflicting IDs, those are removed. If false, the entity IDs from the files are ignored,
                  and new IDs are generated for the created entities.
        @param change Change type that will be used, when removing the old scene, and deserializing the new
        @return List of created entities.
        @todo Return list of EntityPtrs instead of raw pointers. Could also consider EntityList ,though QList[] has the nice operator [] accessor. */
    QList<Entity *> CreateContentFromSceneDesc(const SceneDesc &desc, bool useEntityIDsFromFile, AttributeChange::Type change);

    /// Emits notification of an attribute changing. Called by IComponent.
    /** @param comp Component pointer
        @param attribute Attribute pointer
        @param change Change signaling mode */
    void EmitAttributeChanged(IComponent* comp, IAttribute* attribute, AttributeChange::Type change);

    /// Emits notification of an attribute having been created. Called by IComponent's with dynamic structure
    /** @param comp Component pointer
        @param attribute Attribute pointer
        @param change Change signaling mode */
    void EmitAttributeAdded(IComponent* comp, IAttribute* attribute, AttributeChange::Type change);

    /// Emits notification of an attribute about to be deleted. Called by IComponent's with dynamic structure
    /** @param comp Component pointer
        @param attribute Attribute pointer
        @param change Change signaling mode */
     void EmitAttributeRemoved(IComponent* comp, IAttribute* attribute, AttributeChange::Type change);

    /// Emits a notification of a component being added to entity. Called by the entity
    /** @param entity Entity pointer
        @param comp Component pointer
        @param change Change signaling mode */
    void EmitComponentAdded(Entity* entity, IComponent* comp, AttributeChange::Type change);

    /// Emits a notification of a component being removed from entity. Called by the entity
    /** @param entity Entity pointer
        @param comp Component pointer
        @param change Change signaling mode
        @note This is emitted before just before the component is removed. */
    void EmitComponentRemoved(Entity* entity, IComponent* comp, AttributeChange::Type change);

    /// Emits a notification of an entity being removed.
    /** @note the entity pointer will be invalid shortly after!
        @param entity Entity pointer
        @param change Change signaling mode */
    void EmitEntityRemoved(Entity* entity, AttributeChange::Type change);

    /// Emits a notification of an entity action being triggered.
    /** @param entity Entity pointer
        @param action Name of the action
        @param params Parameters
        @param type Execution type. */
    void EmitActionTriggered(Entity *entity, const QString &action, const QStringList &params, EntityAction::ExecTypeField type);

    /// Emits a notification of an entity creation acked by the server, and the entity ID changing as a result. Called by SyncManager
    void EmitEntityAcked(Entity* entity, entity_id_t oldId);

    /// Emits a notification of a component creation acked by the server, and the component ID changing as a result. Called by SyncManager
    void EmitComponentAcked(IComponent* component, component_id_t oldId);

    /// Returns all components of type T (and additionally with specific name) in the scene.
    template <typename T>
    std::vector<shared_ptr<T> > Components(const QString &name = "") const;

    /// Returns list of entities with a specific component present.
    /** @param name Name of the component, optional.
        @note O(n) */
    template <typename T>
    EntityList EntitiesWithComponent(const QString &name = "") const;

    /// @cond PRIVATE
    /// Do not directly allocate new scenes using operator new, but use the factory-based SceneAPI::CreateScene functions instead.
    /** @param name Name of the scene.
        @param fw Parent framework.
        @param viewEnabled Whether the scene is view enabled.
        @param authority Whether the scene has authority i.e. a single user or server scene, false for network client scenes */
    Scene(const QString &name, Framework *fw, bool viewEnabled, bool authority);
    /// @endcond

    /// @cond PRIVATE
    // DEPRECATED
    template <class T> shared_ptr<T> GetWorld() const { return Subsystem<T>(); } /**< @deprecated Use Subsystem instead. @todo Remove. */
    /// @endcond

    /// @cond PRIVATE
    // Not publicly documented as ideally Scene should not know about Placeable until its defined in TundraCore.
    /// Fix parent Entity ids that are set to EC_Placeable::parentRef.
    /** If @c printStats is true, a summary of the execution is printed once done.
        @return Number of fixed parent refs. */
    uint FixPlaceableParentIds(const QList<Entity *> &entities, const EntityIdMap &oldToNewIds, AttributeChange::Type change, bool printStats = false) const;
    uint FixPlaceableParentIds(const QList<EntityWeakPtr> &entities, const EntityIdMap &oldToNewIds, AttributeChange::Type change, bool printStats = false) const; /**< @overload */
    /// @endcond

public slots:
    /// Creates new entity that contains the specified components.
    /** Entities should never be created directly, but instead created with this function.

        To create an empty entity, omit the components parameter.

        @param id Id of the new entity. Specify 0 to use the next free (replicated) ID, see also NextFreeId and NextFreeIdLocal.
        @param components Optional list of component names ("EC_" prefix can be omitted) the entity will use. If omitted or the list is empty, creates an empty entity.
        @param change Notification/network replication mode
        @param replicated Whether entity is replicated. Default true.
        @param componentsReplicated Whether components will be replicated, true by default.
        @param temporary Will the entity be temporary i.e. it is no serialized to disk by default, false by default.
        @sa CreateLocalEntity, CreateLocalTemporaryEntity, CreateTemporaryEntity*/
    EntityPtr CreateEntity(entity_id_t id = 0, const QStringList &components = QStringList(),
        AttributeChange::Type change = AttributeChange::Default, bool replicated = true,
        bool componentsReplicated = true, bool temporary = false);

    /// Creates new local entity that contains the specified components
    /** Entities should never be created directly, but instead created with this function.

        To create an empty entity omit components parameter.

        @param components Optional list of component names ("EC_" prefix can be omitted) the entity will use. If omitted or the list is empty, creates an empty entity.
        @param change Notification/network replication mode
        @param componentsReplicated Whether components will be replicated, false by default, but components of local entities are not replicated so this has no effect.
        @param temporary Will the entity be temporary i.e. it is no serialized to disk by default.
        @sa CreateEntity, CreateLocalTemporaryEntity, CreateTemporaryEntity */
    EntityPtr CreateLocalEntity(const QStringList &components = QStringList(),
        AttributeChange::Type change = AttributeChange::Default, bool componentsReplicated = false, bool temporary = false);

    /// Convenience function for creating a temporary entity.
    /** @sa CreateEntity, CreateLocalEntity, CreateLocalTemporaryEntity */
    EntityPtr CreateTemporaryEntity(const QStringList &components = QStringList(),
        AttributeChange::Type change = AttributeChange::Default, bool componentsReplicated = true);

    /// Convenience function for creating a local temporary entity.
    /** @sa CreateEntity, CreateLocalEntity, CreateTemporaryEntity */
    EntityPtr CreateLocalTemporaryEntity(const QStringList &components = QStringList(),
        AttributeChange::Type change = AttributeChange::Default);

    /// Returns scene up vector. For now it is a compile-time constant
    /** @sa RightVector,.ForwardVector */
    float3 UpVector() const;

    /// Returns scene right vector. For now it is a compile-time constant
    /** @sa UpVector, ForwardVector */
    float3 RightVector() const;

    /// Returns scene forward vector. For now it is a compile-time constant
    /** @sa UpVector, RightVector */
    float3 ForwardVector() const;

    /// Is scene view enabled (i.e. rendering-related components actually create stuff).
    /** @todo Exposed as Q_PROPERTY, doesn't need to be a slot. */
    bool ViewEnabled() const { return viewEnabled_; }

    /// Is scene authoritative i.e. a server or standalone scene
    /** @todo Exposed as Q_PROPERTY, doesn't need to be a slot. */
    bool IsAuthority() const { return authority_; }

    /// Returns entity with the specified id
    /** @note Returns a shared pointer, but it is preferable to use a weak pointer, EntityWeakPtr,
        to avoid dangling references that prevent entities from being properly destroyed.
        @note O(log n)
        @sa EntityByName*/
    EntityPtr EntityById(entity_id_t id) const;

    /// Returns entity with the specified name.
    /** @note The name of the entity is stored in a Name component. If this component is not present in the entity, it has no name.
        @note Returns a shared pointer, but it is preferable to use a weak pointer, EntityWeakPtr,
              to avoid dangling references that prevent entities from being properly destroyed.
        @note @note O(n)
        @sa EntityById, FindEntities, FindEntitiesContaining */
    EntityPtr EntityByName(const QString &name) const;

    /// Returns whether name is unique within the scene, i.e. is only encountered once, or not at all.
    /** @note O(n) */
    bool IsUniqueName(const QString& name) const;

    /// Returns true if entity with the specified id exists in this scene, false otherwise
    /** @note O(log n) */
    bool HasEntity(entity_id_t id) const { return (entities_.find(id) != entities_.end()); }

    /// Removes entity with specified id
    /** The entity may not get deleted if dangling references to a pointer to the entity exists.
        @param id Id of the entity to remove
        @param change Origin of change regards to network replication.
        @return Was the entity found and removed. */
    bool RemoveEntity(entity_id_t id, AttributeChange::Type change = AttributeChange::Default);

    /// Removes all entities
    /** The entities may not get deleted if dangling references to a pointer to them exist.
        @param signal Whether to send signals of each delete. */
    void RemoveAllEntities(bool signal = true, AttributeChange::Type change = AttributeChange::Default);

    /// Gets and allocates the next free entity id.
    /** @sa NextFreeIdLocal */
    entity_id_t NextFreeId();

    /// Gets and allocates the next free entity id.
    /** @sa NextFreeId */
    entity_id_t NextFreeIdLocal();

    /// Returns list of entities with a specific component present.
    /** @param typeId Type ID of the component
        @param name Name of the component, optional.
        @note O(n) */
    EntityList EntitiesWithComponent(u32 typeId, const QString &name = "") const;
    /// @overload
    /** @param typeName typeName Type name of the component.
        @note The overload taking type ID is more efficient than this overload. */
    EntityList EntitiesWithComponent(const QString &typeName, const QString &name = "") const;

    /// Returns list of entities that belong to the group 'groupName'
    /** @param groupName The name of the group to be queried */
    EntityList EntitiesOfGroup(const QString &groupName) const;

    /// Returns all components of specific type (and additionally with specific name) in the scene.
    /*  @param typeId Component type ID.
        @param name Arbitrary name of the component (optional). */
    Entity::ComponentVector Components(u32 typeId, const QString &name = "") const;
    /// overload
    /** @param typeName Component type name.
        @note The overload taking type ID is more efficient than this overload. */
    Entity::ComponentVector Components(const QString &typeName, const QString &name = "") const;

    /// Performs a regular expression matching through the entities, and returns a list of the matched entities.
    /** @param pattern Regular expression to be matched.
        @note Wildcards can be escaped with '\' character.
        @sa FindEntitiesContaining */
    EntityList FindEntities(const QRegExp &pattern) const;
    EntityList FindEntities(const QString &pattern) const; /**< @overload @param pattern String pattern with wildcards. */

    /// Performs a search through the entities, and returns a list of all the entities that contain @c substring in their Entity name.
    /** @param substring String to be searched.
        @param sensitivity Case sensitivity for the string matching. */
    EntityList FindEntitiesContaining(const QString &substring, Qt::CaseSensitivity sensitivity = Qt::CaseSensitive) const;

    /// Performs a search through the entities, and returns a list of all the entities where Entity name matches @c name.
    /** @param name Entity name to match.
        @param sensitivity Case sensitivity for the string matching. */
    EntityList FindEntitiesByName(const QString &name, Qt::CaseSensitivity sensitivity = Qt::CaseSensitive) const;

    /// Return root-level entities, i.e. those that have no parent.
    EntityList RootLevelEntities() const;

    /// Loads the scene from XML.
    /** @param filename File name
        @param clearScene Do we want to clear the existing scene.
        @param useEntityIDsFromFile If true, the created entities will use the Entity IDs from the original file. 
                  If the scene contains any previous entities with conflicting IDs, those are removed. If false, the entity IDs from the files are ignored,
                  and new IDs are generated for the created entities.
        @param change Change type that will be used, when removing the old scene, and deserializing the new
        @return List of created entities.
        @todo Return list of EntityPtrs instead of raw pointers. Could also consider EntityList ,though QList[] has the nice operator [] accessor. */
    QList<Entity *> LoadSceneXML(const QString& filename, bool clearScene, bool useEntityIDsFromFile, AttributeChange::Type change);

    /// Returns scene content as an XML string.
    /** @param serializeTemporary Are temporary entities wanted to be included.
        @param serializeLocal Are local entities wanted to be included.
        @return The scene XML as a byte array string. */
    QByteArray SerializeToXmlString(bool serializeTemporary, bool serializeLocal) const;

    /// Saves the scene to XML.
    /** @param filename File name
        @param saveTemporary Are temporary entities wanted to be included.
        @param saveLocal Are local entities wanted to be included.
        @return true if successful */
    bool SaveSceneXML(const QString& filename, bool saveTemporary, bool saveLocal) const;

    /// Loads the scene from a binary file.
    /** @param filename File name
        @param clearScene Do we want to clear the existing scene.
        @param useEntityIDsFromFile If true, the created entities will use the Entity IDs from the original file. 
                  If the scene contains any previous entities with conflicting IDs, those are removed. If false, the entity IDs from the files are ignored,
                  and new IDs are generated for the created entities.
        @param change Change type that will be used, when removing the old scene, and deserializing the new
        @return List of created entities.
        @todo Return list of EntityPtrs instead of raw pointers. Could also consider EntityList ,though QList[] has the nice operator [] accessor. */
    QList<Entity *> LoadSceneBinary(const QString& filename, bool clearScene, bool useEntityIDsFromFile, AttributeChange::Type change);

    /// Save the scene to binary
    /** @param filename File name
        @param saveTemporary Are temporary entities wanted to be included.
        @param saveLocal Are local entities wanted to be included.
        @return true if successful */
    bool SaveSceneBinary(const QString& filename, bool saveTemporary, bool saveLocal) const;

    /// Creates scene content from XML.
    /** @param xml XML document as string.
        @param useEntityIDsFromFile If true, the created entities will use the Entity IDs from the original file.
                  If the scene contains any previous entities with conflicting IDs, those are removed. If false, the entity IDs from the files are ignored,
                  and new IDs are generated for the created entities.
        @param change Change type that will be used, when removing the old scene, and deserializing the new
        @return List of created entities.
        @todo Return list of EntityPtrs instead of raw pointers. Could also consider EntityList ,though QList[] has the nice operator [] accessor. */
    QList<Entity *> CreateContentFromXml(const QString &xml, bool useEntityIDsFromFile, AttributeChange::Type change);
    QList<Entity *> CreateContentFromXml(const QDomDocument &xml, bool useEntityIDsFromFile, AttributeChange::Type change); /**< @overload @param xml XML document. */

    /// Creates scene content from binary file.
    /** @param filename File name.
        @param useEntityIDsFromFile If true, the created entities will use the Entity IDs from the original file.
                  If the scene contains any previous entities with conflicting IDs, those are removed. If false, the entity IDs from the files are ignored,
                  and new IDs are generated for the created entities.
        @param change Change type that will be used, when removing the old scene, and deserializing the new
        @return List of created entities.
        @todo Return list of EntityPtrs instead of raw pointers. Could also consider EntityList ,though QList[] has the nice operator [] accessor. */
    QList<Entity *> CreateContentFromBinary(const QString &filename, bool useEntityIDsFromFile, AttributeChange::Type change);
    QList<Entity *> CreateContentFromBinary(const char *data, int numBytes, bool useEntityIDsFromFile, AttributeChange::Type change); /**< @overload @param data Data buffer @param numBytes Data size. */

    /// Returns @c ent parent Entity id.
    /** Check both Entity and EC_Placeble::parentRef parenting,
        Entity parenting takes precedence.
        @return Returns 0 if parent is not set or the parent ref is not a Entity id (but a entity name). */
    entity_id_t EntityParentId(const Entity *ent) const;

    /// Sorts @c entities by scene hierarchy and returns the sorted list.
    /** Takes into account both Entity::Parent and EC_Placeable::parentRef parenting,
        Entity-level parenting takes precedence. */
    QList<Entity*> SortEntities(const QList<Entity*> &entities) const;
    QList<EntityWeakPtr> SortEntities(const QList<EntityWeakPtr> &entities) const;
    EntityDescList SortEntities(const EntityDescList &entities) const;

    /// Checks whether editing an entity is allowed.
    /** Emits AboutToModifyEntity.
        @user entity Connection that is requesting permission to modify an entity.
        @param entity Entity that is requested to be modified. */
    bool AllowModifyEntity(UserConnection *user, Entity *entity);

    /// Emits a notification of an entity having been created
    /** Creates are also automatically signaled at the end of frame, so you do not necessarily need to call this.
        @param entity Entity pointer
        @param change Change signaling mode */
    void EmitEntityCreated(Entity *entity, AttributeChange::Type change = AttributeChange::Default);

    /// Emits a notification of entity reparenting
    /** @param entity Entity that is being reparented
        @param newParent New parent entity
        @param change Change signaling mode */
    void EmitEntityParentChanged(Entity *entity, Entity *newParent, AttributeChange::Type change = AttributeChange::Default);

    /// @cond PRIVATE
    // DEPRECATED function signatures
    EntityPtr GetEntity(entity_id_t id) const { return EntityById(id); } /**< @deprecated Use EntityById @todo Add warning print, remove in some distant future */
    EntityPtr GetEntityByName(const QString& name) const { return EntityByName(name); } /**< @deprecated Use EntityByName  @todo Add warning print, remove in some distant future */
    EntityList GetEntitiesWithComponent(const QString &typeName, const QString &name = "") const { return EntitiesWithComponent(typeName, name); } ///< @deprecated Use EntitiesWithComponent @todo Add warning print, remove in some distant future */
    EntityList GetAllEntities() const; /**< @deprecated @todo Add warning print, remove in some distant future */
    QVariantList GetEntityIdsWithComponent(const QString &typeName) const; /**< @deprecated Use EntitiesWithComponent instead @todo Remove. */
    Entity* GetEntityRaw(uint id) const { return GetEntity(id).get(); } /**< @deprecated Use EntityById @todo Remove */
    bool DeleteEntityById(uint id, AttributeChange::Type change = AttributeChange::Default) { return RemoveEntity((entity_id_t)id, change); } /**< @deprecated Use RemoveEntity @todo Remove */
    bool RemoveEntityRaw(int entityid, AttributeChange::Type change = AttributeChange::Default) { return RemoveEntity(entityid, change); } /**< @deprecated Use RemoveEntity @todo Remove */
    EntityMap Entities() /*non-const intentionally*/ { return entities_; } /**< @deprecated use const version Entities or 'entities' instead. @todo Add deprecation print. @todo Remove. */
    QByteArray GetEntityXml(Entity *entity) const; /**< @deprecated Use Entity::SerializeToXMLString. @todo Remove */
    QByteArray GetSceneXML(bool serializeTemporary, bool serializeLocal) const; /**< @deprecated Use SerializeToXmlString @todo Remove */
    /// @endcond

signals:
    /// Signal when an attribute of a component has changed
    /** Network synchronization managers should connect to this. */
    void AttributeChanged(IComponent* comp, IAttribute* attribute, AttributeChange::Type change);

    /// Signal when an attribute of a component has been added (dynamic structure components only)
    /** Network synchronization managers should connect to this. */
    void AttributeAdded(IComponent* comp, IAttribute* attribute, AttributeChange::Type change);

    /// Signal when an attribute of a component has been added (dynamic structure components only)
    /** Network synchronization managers should connect to this. */
    void AttributeRemoved(IComponent* comp, IAttribute* attribute, AttributeChange::Type change);

    /// Signal when a component is added to an entity and should possibly be replicated (if the change originates from local)
    /** Network synchronization managers should connect to this. */
    void ComponentAdded(Entity* entity, IComponent* comp, AttributeChange::Type change);

    /// Signal when a component is removed from an entity and should possibly be replicated (if the change originates from local)
    /** Network synchronization managers should connect to this */
    void ComponentRemoved(Entity* entity, IComponent* comp, AttributeChange::Type change);

    /// Signal when an entity created
    /** @note Entity::IsTemporary() information might not be accurate yet, as it depends on the method that was used to create the entity. */
    void EntityCreated(Entity* entity, AttributeChange::Type change);

    /// Signal when an entity deleted
    void EntityRemoved(Entity* entity, AttributeChange::Type change);

    /// A entity creation has been acked by the server and assigned a proper replicated ID
    void EntityAcked(Entity* entity, entity_id_t oldId);

    /// An entity's temporary state has been toggled
    void EntityTemporaryStateToggled(Entity* entity, AttributeChange::Type change);

    /// A component creation into an entity has been acked by the server and assigned a proper replicated ID
    void ComponentAcked(IComponent* comp, component_id_t oldId);

    /// Emitted when entity action is triggered.
    /** @param entity Entity for which action was executed.
        @param action Name of action that was triggered.
        @param params Parameters of the action.
        @param type Execution type.

        @note Use case-insensitive comparison for checking name of the @c action ! */
    void ActionTriggered(Entity *entity, const QString &action, const QStringList &params, EntityAction::ExecTypeField type);

    /// Emitted when an entity is about to be modified:
    void AboutToModifyEntity(ChangeRequest* req, UserConnection* user, Entity* entity);

    /// Emitted when being destroyed
    void Removed(Scene* scene);

    /// Signal when the whole scene is cleared
    void SceneCleared(Scene* scene);

    /// An entity's parent has changed.
    void EntityParentChanged(Entity* entity, Entity* newParent, AttributeChange::Type change);

private slots:
    /// Handle frame update. Signal this frame's entity creations.
    void OnUpdated(float frameTime);

private:
    friend class ::SceneAPI;

    /// Create entity from an XML element and recurse into child entities. Called internally.
    void CreateEntityFromXml(EntityPtr parent, const QDomElement& ent_elem, bool useEntityIDsFromFile,
        AttributeChange::Type change, QList<EntityWeakPtr>& entities, EntityIdMap& oldToNewIds);
    /// Create entity from binary data and recurse into child entities. Called internally.
    void CreateEntityFromBinary(EntityPtr parent, kNet::DataDeserializer& source, bool useEntityIDsFromFile,
        AttributeChange::Type change, QList<EntityWeakPtr>& entities, EntityIdMap& oldToNewIds);
    /// Create entity from entity desc and recurse into child entities. Called internally.
    void CreateEntityFromDesc(EntityPtr parent, const EntityDesc& source, bool useEntityIDsFromFile,
        AttributeChange::Type change, QList<Entity *>& entities, EntityIdMap& oldToNewIds);
    /// Create entity desc from an XML element and recurse into child entities. Called internally.
    void CreateEntityDescFromXml(SceneDesc& sceneDesc, QList<EntityDesc>& dest, const QDomElement& ent_elem, bool resolveAssets) const;

    /// Container for an ongoing attribute interpolation
    struct AttributeInterpolation
    {
        AttributeInterpolation() : time(0.0f), length(0.0f) {}
        AttributeWeakPtr dest, start, end;
        float time;
        float length;
    };

    /// Resolved parent Entity id that is set to EC_Placeable::parentRef.
    /** @return Returns 0 if parent is not set or the parent ref is not a Entity id (but a entity name). */
    entity_id_t PlaceableParentId(const Entity *ent) const;
    entity_id_t PlaceableParentId(const EntityDesc &ent) const; ///< @overload

    UniqueIdGenerator idGenerator_; ///< Entity ID generator
    EntityMap entities_; ///< All entities in the scene.
    Framework *framework_; ///< Parent framework.
    QString name_; ///< Name of the scene.
    bool viewEnabled_; ///< View enabled -flag.
    bool interpolating_; ///< Currently doing interpolation-flag.
    bool authority_; ///< Authority -flag
    std::vector<AttributeInterpolation> interpolations_; ///< Running attribute interpolations.
    std::vector<std::pair<EntityWeakPtr, AttributeChange::Type> > entitiesCreatedThisFrame_; ///< Entities to signal for creation at frame end.
    ParentingTracker parentTracker_; ///< Tracker for client side mass Entity imports (eg. SceneDesc based).
};
Q_DECLARE_METATYPE(Scene*);
Q_DECLARE_METATYPE(Scene::EntityMap)
Q_DECLARE_METATYPE(QList<Entity*>)

#include "Scene.inl"
