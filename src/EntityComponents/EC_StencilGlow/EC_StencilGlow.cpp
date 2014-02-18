/**
    For conditions of distribution and use, see copyright notice in license.txt

    @file   EC_StencilGlow.h
    @brief  Adds an outline to a mesh. */

#define MATH_OGRE_INTEROP
#include "Win.h"
#include "EC_StencilGlow.h"

#include "Scene.h"
#include "Entity.h"
#include "Framework.h"
#include "LoggingFunctions.h"
#include "OgreWorld.h"
#include "EC_Mesh.h"

#include <Ogre.h>
#include <OgreRenderQueueListener.h>

#define STENCIL_GLOW_ENTITY Ogre::RENDER_QUEUE_MAIN + 1
#define STENCIL_GLOW_OUTLINE Ogre::RENDER_QUEUE_OVERLAY - 1

#define COLOR_CUSTOM_PARAM 1

EC_StencilGlow::EC_StencilGlow(Scene *scene) :
    IComponent(scene),
    INIT_ATTRIBUTE_VALUE(enabled, "Enabled", true),
    INIT_ATTRIBUTE_VALUE(color, "Color", Color(1.f, 1.f, 1.f, 0.4f)),
    INIT_ATTRIBUTE_VALUE(scale, "Scale", float3::FromScalar(1.2f)),
    isEnabled(false),
    outlineEntity_(0),
    outlineSceneNode_(0)
{
    connect(this, SIGNAL(ParentEntitySet()), this, SLOT(Initialize()));
}

EC_StencilGlow::~EC_StencilGlow()
{
    DestroyStencilGlow();
}

void EC_StencilGlow::Initialize()
{
    if (!ViewEnabled() || GetFramework()->IsHeadless())
        return;

    world_ = ParentScene()->Subsystem<OgreWorld>();

    EC_Mesh* mesh = GetMesh();
    if (!mesh)
    {
        LogError("EC_StencilGlow needs EC_Mesh in advance in order to set it!");
        return;
    }

    connect(mesh, SIGNAL(MeshChanged()), this, SLOT(OnMeshChanged()));
    connect(mesh, SIGNAL(MeshAboutToBeDestroyed()), this, SLOT(OnMeshAboutToBeDestroyed()));

    if (enabled.Get())
    {
        CreateStencilGlow();
        SetStencilGlowEnabled(true);
    }
}

void EC_StencilGlow::CreateStencilGlow()
{
    if (!ViewEnabled())
        return;

    if (world_.expired())
        return;

    EC_Mesh* mesh = GetMesh();
    if (!mesh)
    {
        LogError("EC_StencilGlow needs EC_Mesh in advance in order to set it!");
        return;
    }

    Ogre::Entity* entity = mesh->OgreEntity();
    if (!entity)
        return;

    if (!outlineEntity_ && !outlineSceneNode_)
    {
        outlineEntity_ = entity->clone(entity->getName() + "_glow");
        outlineEntity_->setRenderQueueGroup(STENCIL_GLOW_OUTLINE);
        outlineEntity_->setMaterialName("cg/stencil_alpha_glow");

        Ogre::SubEntity *subEnt = outlineEntity_->getSubEntity(0);
        if(subEnt)
            subEnt->setCustomParameter(COLOR_CUSTOM_PARAM, color.Get().ToFloat4());

        if (entity->hasSkeleton())
            outlineEntity_->shareSkeletonInstanceWith(entity);
        
        Ogre::SceneManager* mgr = world_.lock()->OgreSceneManager();
        if (mgr)
        {
            outlineSceneNode_ = entity->getParentSceneNode()->createChildSceneNode(entity->getName() + "_outlineGlowNode");
            outlineSceneNode_->setScale(scale.Get());
        }

        isEnabled = false;
        SetStencilGlowEnabled(enabled.Get());
    }
}

void EC_StencilGlow::DestroyStencilGlow()
{
    if (world_.expired())
        return;

    if (outlineEntity_)
    {
        world_.lock()->OgreSceneManager()->destroyEntity(outlineEntity_);
        outlineEntity_ = 0;
    }

    if (outlineSceneNode_)
    {
        world_.lock()->OgreSceneManager()->destroySceneNode(outlineSceneNode_);
        outlineSceneNode_ = 0;
    }
}

void EC_StencilGlow::SetStencilGlowEnabled(bool enable)
{
    if (!ViewEnabled())
        return;

    EC_Mesh* mesh = GetMesh();
    if (!mesh)
        return;
    
    Ogre::Entity* entity = mesh->OgreEntity();
    if (!entity)
        return;

    if (!outlineSceneNode_ && !outlineEntity_)
        return;

    if (enable && !isEnabled)
    {
        entity->setRenderQueueGroup(STENCIL_GLOW_ENTITY);
        outlineSceneNode_->attachObject(outlineEntity_);
        isEnabled = true;
    }
    else if (!enable && isEnabled)
    {
        entity->setRenderQueueGroup(Ogre::RENDER_QUEUE_MAIN);
        outlineSceneNode_->detachObject(outlineEntity_);
        isEnabled = false;
    }
}

void EC_StencilGlow::AttributesChanged()
{
    if(enabled.ValueChanged())
    {
        CreateStencilGlow();
        SetStencilGlowEnabled(enabled.Get());
    }
    if(color.ValueChanged())
    {
        if(outlineEntity_)
        {
            Ogre::SubEntity *subEnt = outlineEntity_->getSubEntity(0);
            if(subEnt)
            {
                Color newColor = color.Get();
                subEnt->setCustomParameter(COLOR_CUSTOM_PARAM, Ogre::Vector4(newColor.r, newColor.g, newColor.b, newColor.a));
            }
        }
    }
    if (scale.ValueChanged() && outlineSceneNode_)
        outlineSceneNode_->setScale(scale.Get());
}

void EC_StencilGlow::OnMeshChanged()
{
    CreateStencilGlow();
    SetStencilGlowEnabled(enabled.Get());
}

void EC_StencilGlow::OnMeshAboutToBeDestroyed()
{
    DestroyStencilGlow();
}

EC_Mesh* EC_StencilGlow::GetMesh() const
{
    return ParentEntity()->Component<EC_Mesh>().get();
}
