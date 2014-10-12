
#pragma once

#include "CoreTypes.h"
#include "CoreDefines.h"

#include "Framework.h"
#include "Application.h"
#include "SceneAPI.h"

#include <QObject>
#include <QString>
#include <QList>
#include <QDebug>

namespace TundraTest
{
    /** Static common QCOMPARE variables. It is understadably very strict about types,
        so to make tests more compact and easy to read these should be used. */
    static QString EmptyQString = QString("");
    static size_t ZeroSizeT = 0;

    /// Returns a list of true and false values.
    static QList<bool> TrueAndFalse()
    {
        QList<bool> modes;
        modes << true << false;
        return modes;
    }

    static QString TruthyString(bool truthy)
    {
        return truthy ? "true" : "false";
    }

    /// Typedefs for TestFramework
    typedef shared_ptr<Application> ApplicationPtr;
    typedef shared_ptr<Framework> FrameworkPtr;

    /** Unit test framework
        Helps out initialization and releasing framework/application ptrs correctly after the test.
        @note Put this object in your class (stack variable) and call Initialize once in your
        tests entry point: initTestCase(). */
    struct TestFramework
    {
        ApplicationPtr application;
        FrameworkPtr framework;
        ScenePtr scene;

        std::vector<const char*> arguments;
        std::string config;

        ~TestFramework()
        {
            qDebug() << "TestFramework::~TestFramework()";

            /* PostGo is the counterpart of PreGo in Initialize. It will unload
               all plugins and uninitialize APIs, preparing Framework for shutdown. */
            if (framework.get())
            {
                framework->PostGo();
                ProcessEvents();
            }

            // Explicit releasing order
            framework.reset();
            application.reset();

            arguments.clear();
            config.clear();
        }

        /// Call this function once in your initTestCase() function.
        void Initialize(bool createScene = true, bool server = false, bool headless = true, const QString &_config = "")
        {
            qDebug() << qPrintable(QString("TestFramework::Initialize(bool createScene = %1, bool server = %2, bool headless = %3, const QString &config = \"%4\")")
                .arg(TruthyString(createScene)).arg(TruthyString(server)).arg(TruthyString(headless)).arg(_config));

            arguments.push_back("Tundra.exe");

            if (server)
                arguments.push_back("--server");
            if (headless)
                arguments.push_back("--headless");

            SetConfig(_config);

            int argc = (int)arguments.size();
            char **argv = (char**)&arguments[0];

            application = MAKE_SHARED(Application, argc, argv);
            framework = MAKE_SHARED(Framework, argc, argv, application.get());

            /* PreGo will invoke identical behaviour as Framework::Go but without
               blocking with QApplication::exec(). Loading and initializing all APIs and modules. */
            application->Initialize(framework.get());
            framework->PreGo();

            if (createScene)
                scene = framework->Scene()->CreateScene("TestScene", false, server);

            /* Spin once cycle of QApplication even loop now to fire off startup signals etc.
               For example for the SubSystems to be in place via the SceneCreated signal handlers. */
            ProcessEvents();
        }

        /// Call this function whenever you need Qt events, eg. signals, to be processed.
        void ProcessEvents()
        {
            /* First process all events. UpdateFrame() does this with a 1 msec max
               time to spend which is something we don't want to enforce here. */
            QApplication::processEvents();

            // Update Application > Framework > Core APIs > IModules > IRenderer
            application->UpdateFrame();
        }

        // Set the --config value to be used in Framework
        bool SetConfig(const QString _config)
        {
            if (!_config.isEmpty())
            {
                if (config.empty())
                {
                    config = _config.toStdString();
                    arguments.push_back("--config");
                    arguments.push_back(config.c_str());
                    return true;
                }
                else
                    qWarning() << "TestFramework::SetConfig: Config already set to" << QString::fromStdString(config);
            }
            return false;
        }
    };
}
