// For conditions of distribution and use, see copyright notice in LICENSE

#pragma once

#include "IModule.h"
#include "OgreModuleApi.h"
#include "OgreModuleFwd.h"
#include "SceneFwd.h"
#include <OgreRenderSystem.h>

namespace OgreRenderer
{
    /** @bug Ogre assert fail when viewing a mesh that contains a reference to non-existing skeleton. */

    /// A renderer module using Ogre
    class OGRE_MODULE_API OgreRenderingModule : public IModule
    {
        Q_OBJECT

    public:
        OgreRenderingModule();
        virtual ~OgreRenderingModule();

        virtual void Load();
        virtual void Initialize();
        virtual void Uninitialize();

        /// Returns the renderer.
        const RendererPtr &Renderer() const { return renderer; }

        /// Ogre resource group for cached asset files.
        static std::string CACHE_RESOURCE_GROUP;

        // DEPRECATED
        /// @cond PRIVATE
        const RendererPtr &GetRenderer() const { return Renderer(); } /**< @deprecated Use Renderer() instead. @todo Remove. */
        /// @endcond

    public slots:
        /// Prints renderer stats to console.
        void ConsoleStats();

        /// Toggles visibility of the Ogre profiler overlay.
        /** @note Applicable only if Ogre built with profiler support. */
        void ToggleOgreProfilerOverlay();

        /// Sets attribute value for material.
        void SetMaterialAttribute(const QStringList &params);

    private slots:
        /// Creates OgreWorld for a Scene.
        void CreateOgreWorld(Scene *scene);
        /// Removes OgreWorld from a Scene.
        void RemoveOgreWorld(Scene *scene);

    private:
        RendererPtr renderer;  ///< Renderer
        OgreRenderSystemListener *renderSystemListener;
    };

    class OgreRenderSystemListener : public Ogre::RenderSystem::Listener
    {
    public:
        OgreRenderSystemListener(OgreRenderer::OgreRenderingModule* renderingModule);
        ~OgreRenderSystemListener();

        void eventOccurred(const Ogre::String& eventName, const Ogre::NameValuePairList* parameters = 0);

    private:
        OgreRenderer::OgreRenderingModule* renderingModule_;
    };
}
