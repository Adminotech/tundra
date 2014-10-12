
#pragma once

#include "TestHelpers.h"

namespace TundraTest
{
    class Scene : public QObject
    {
        Q_OBJECT
    
    public:
        Scene(const QString &config = "");

    private slots:
        void initTestCase();     // QTest
        void cleanupTestCase();  // QTest
        void cleanup();          // QTest

        void Create_Entity();
        void Create_Attributes_Unparented();
        void Create_Components_Unparented();
        void Create_Components_Parented();

    private:
        TestFramework test_;
    };
}
