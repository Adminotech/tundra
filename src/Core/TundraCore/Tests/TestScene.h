
#pragma once

#include "TestHelpers.h"

namespace TundraTest
{
    class Scene : public QObject
    {
        Q_OBJECT

    private slots:
        void initTestCase();     // QTest
        void cleanupTestCase();  // QTest
        void cleanup();          // QTest

        void CreateEntity();

    private:
        TestFramework test_;
    };
}
