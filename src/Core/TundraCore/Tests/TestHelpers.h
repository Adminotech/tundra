
#pragma once

#include "CoreTypes.h"
#include "CoreDefines.h"

#include "Framework.h"
#include "Application.h"
#include "SceneAPI.h"

#include <QObject>
#include <QString>
#include <QList>

namespace TundraTest
{
    typedef shared_ptr<Application> ApplicationPtr;
    typedef shared_ptr<Framework> FrameworkPtr;

    struct TestFramework
    {
        ApplicationPtr application;
        FrameworkPtr framework;
        ScenePtr scene;

        std::vector<const char*> arguments;

        void Initialize(bool createScene = true, bool server = false, bool headless = true, const QString &config = "")
        {
            arguments.push_back("Tundra.exe");

            if (server)
                arguments.push_back("--server");
            if (headless)
                arguments.push_back("--headless");
            if (!config.isEmpty())
            {
                std::string stdConfig = config.toStdString();
                arguments.push_back("--config");
                arguments.push_back(stdConfig.c_str());
            }

            int argc = (int)arguments.size();
            char **argv = (char**)&arguments[0];

            application = MAKE_SHARED(Application, argc, argv);
            framework = MAKE_SHARED(Framework, argc, argv, application.get());
            if (createScene)
                scene = framework->Scene()->CreateScene("TestScene", false, server);
        }
    };

    static QString EmptyQString = QString("");

    static QList<bool> TrueAndFalse()
    {
        QList<bool> modes;
        modes << true << false;
        return modes;
    }
}
