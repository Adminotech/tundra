// For conditions of distribution and use, see copyright notice in LICENSE

#include "StableHeaders.h"
#define MATH_OGRE_INTEROP
#include "DebugOperatorNew.h"

#include "EC_Light.h"
#include "Renderer.h"
#include "EC_Placeable.h"

#include "Entity.h"
#include "Scene/Scene.h"
#include "AttributeMetadata.h"
#include "LoggingFunctions.h"
#include "OgreRenderingModule.h"
#include "OgreWorld.h"

#include <OgreSceneManager.h>
#include <OgreSceneNode.h>

#include "MemoryLeakCheck.h"

using namespace OgreRenderer;

EC_Light::EC_Light(Scene* scene) :
    IComponent(scene),
    light_(0),
    attached_(false),
    INIT_ATTRIBUTE_VALUE(type, "Light type", PointLight),
    INIT_ATTRIBUTE_VALUE(diffColor, "Diffuse color", Color(1.0f, 1.0f, 1.0f)),
    INIT_ATTRIBUTE_VALUE(specColor, "Specular color", Color(0.0f, 0.0f, 0.0f)),
    INIT_ATTRIBUTE_VALUE(castShadows, "Cast shadows", false),
    INIT_ATTRIBUTE_VALUE(range, "Light range", 25.0f),
    INIT_ATTRIBUTE_VALUE(brightness, "Brightness", 1.0f),
    INIT_ATTRIBUTE_VALUE(constAtten, "Constant atten", 0.0f),
    INIT_ATTRIBUTE_VALUE(linearAtten, "Linear atten", 0.01f),
    INIT_ATTRIBUTE_VALUE(quadraAtten, "Quadratic atten", 0.01f),
    INIT_ATTRIBUTE_VALUE(innerAngle, "Light inner angle", 30.0f),
    INIT_ATTRIBUTE_VALUE(outerAngle, "Light outer angle", 40.0f)
{
    static AttributeMetadata typeAttrData;
    static bool metadataInitialized = false;
    if(!metadataInitialized)
    {
        typeAttrData.enums[PointLight] = "Point";
        typeAttrData.enums[Spotlight] = "Spot";
        typeAttrData.enums[DirectionalLight] = "Directional";
        metadataInitialized = true;
    }
    type.SetMetadata(&typeAttrData);

    connect(this, SIGNAL(ParentEntitySet()), SLOT(UpdateSignals()));
}

EC_Light::~EC_Light()
{
    if (world_.expired())
        return;
    
    if (light_)
    {
        DetachLight();
        Ogre::SceneManager* sceneMgr = world_.lock()->OgreSceneManager();
        sceneMgr->destroyLight(light_);
        light_ = 0;
    }
}

void EC_Light::UpdateSignals()
{
    Entity* parent = ParentEntity();
    if (parent && ParentScene())
    {
        world_ = ParentScene()->GetWorld<OgreWorld>();
        if (!world_.expired() && ParentScene()->ViewEnabled())
        {
            OgreWorldPtr world = world_.lock();
            Ogre::SceneManager* sceneMgr = world->OgreSceneManager();
            light_ = sceneMgr->createLight(world->GetUniqueObjectName("EC_Light"));

            // Update light to reflect current values.
            FullUpdate();
        }

        connect(parent, SIGNAL(ComponentAdded(IComponent*, AttributeChange::Type)), SLOT(OnComponentAdded(IComponent*, AttributeChange::Type)));
        connect(parent, SIGNAL(ComponentRemoved(IComponent*, AttributeChange::Type)), SLOT(OnComponentRemoved(IComponent*, AttributeChange::Type)));
        CheckForPlaceable();
    }
}

void EC_Light::CheckForPlaceable()
{
    if (!placeable_)
    {
        Entity* entity = ParentEntity();
        if (entity)
        {
            ComponentPtr placeable = entity->GetComponent(EC_Placeable::TypeNameStatic());
            if (placeable)
                SetPlaceable(placeable);
        }
    }
}

void EC_Light::OnComponentAdded(IComponent* component, AttributeChange::Type change)
{
    if (component->TypeId() == EC_Placeable::TypeIdStatic())
        CheckForPlaceable();
}

void EC_Light::OnComponentRemoved(IComponent* component, AttributeChange::Type change)
{
    if (component == placeable_.get())
        SetPlaceable(ComponentPtr());
}

void EC_Light::SetPlaceable(const ComponentPtr &placeable)
{
    if (!light_)
        return;
    
    if (placeable && dynamic_cast<EC_Placeable*>(placeable.get()) == 0)
    {
        LogError("Attempted to set a placeable which is not a placeable");
        return;
    }
    
    if (placeable_ == placeable)
        return;
    
    DetachLight();
    placeable_ = placeable;
    AttachLight();
}

void EC_Light::AttachLight()
{
    if (light_ && placeable_ && !attached_)
    {
        EC_Placeable* placeable = checked_static_cast<EC_Placeable*>(placeable_.get());
        Ogre::SceneNode* node = placeable->GetSceneNode();
        node->attachObject(light_);
        attached_ = true;
    }
}

void EC_Light::DetachLight()
{
    if (light_ && placeable_ && attached_)
    {
        EC_Placeable* placeable = checked_static_cast<EC_Placeable*>(placeable_.get());
        Ogre::SceneNode* node = placeable->GetSceneNode();
        node->detachObject(light_);
        attached_ = false;
    }
}

void EC_Light::AttributesChanged()
{
    FullUpdate();
}

void EC_Light::FullUpdate()
{
    if (!light_)
        return;

    Ogre::Light::LightTypes ogreType = Ogre::Light::LT_POINT;

    switch(type.Get())
    {
    case Spotlight:
        ogreType = Ogre::Light::LT_SPOTLIGHT;
        break;
    case DirectionalLight:
        ogreType = Ogre::Light::LT_DIRECTIONAL;
        break;
    }
    
    try
    {
        float b = std::max(brightness.Get(), 1e-3f);
        Color diff = diffColor.Get();
        Color spec = specColor.Get();
        // Because attenuation equation (and therefore brightness) does not affect directional lights,
        // manually multiply the colors by brightness for a dir.light
        if (ogreType == Ogre::Light::LT_DIRECTIONAL)
        {
            diff.r *= b;
            diff.g *= b;
            diff.b *= b;
            spec.r *= b;
            spec.g *= b;
            spec.b *= b;
        }
        
        light_->setType(ogreType);
        light_->setCastShadows(castShadows.Get());
        light_->setDiffuseColour(diff);
        light_->setSpecularColour(spec);

        /// @todo This is a Meshmoon specific change for the Meshmoon shaders. This line should not be committed before the rex shaders have been sent to Tundra core.
        light_->setAttenuation(range.Get(), brightness.Get(), 0.0f, 0.0f);
        //light_->setAttenuation(range.Get(), constAtten.Get() / b , linearAtten.Get() / b, quadraAtten.Get() / b);

        // Note: Ogre throws exception if we try to set this when light is not spotlight
        if (ogreType == Ogre::Light::LT_SPOTLIGHT)
            light_->setSpotlightRange(Ogre::Degree(innerAngle.Get()), Ogre::Degree(outerAngle.Get()));
    }
    catch(const Ogre::Exception& e)
    {
        LogError("Exception while setting EC_Light parameters to Ogre: " + std::string(e.what()));
    }
}
