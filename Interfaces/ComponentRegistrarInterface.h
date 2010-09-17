// For conditions of distribution and use, see copyright notice in license.txt

#ifndef incl_Interfaces_ComponentRegistrarInterface_h
#define incl_Interfaces_ComponentRegistrarInterface_h

#include <boost/shared_ptr.hpp>
#include <vector>

class IModule;

namespace Foundation
{
    class Framework;

    //! Base class for all objects that register their entity-component factory services to the framework.
    /*!
        \note It is not necessary to use this class if DECLARE_EC -macro is used.
    */
    class ComponentRegistrarInterface
    {
    public:
        ComponentRegistrarInterface()  {}
        virtual ~ComponentRegistrarInterface() {}
        
        //! registers component to the framework
        virtual void Register(Framework *framework, IModule* module) = 0;

        //! unregisters component to framework
        virtual void Unregister(Framework *framework) = 0;
    };

    typedef boost::shared_ptr<ComponentRegistrarInterface> ComponentRegistrarInterfacePtr;
    typedef std::vector<ComponentRegistrarInterfacePtr> RegistrarVector;
}

#endif
