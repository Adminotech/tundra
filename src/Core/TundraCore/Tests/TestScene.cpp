
#include "DebugOperatorNew.h"

#include "TestScene.h"

#include "Framework.h"
#include "SceneAPI.h"
#include "Scene.h"

#include <QtTest/QtTest>

#include "MemoryLeakCheck.h"

namespace TundraTest
{
    void Scene::initTestCase()
    {
        test_.Initialize();
    }

    void Scene::cleanupTestCase()
    {
    }

    void Scene::cleanup()
    {
        test_.scene->RemoveAllEntities();
    }

    void Scene::CreateEntity()
    {
        QBENCHMARK
        {
            foreach(bool replicated, TrueAndFalse())
            {
                foreach(bool temporary, TrueAndFalse())
                {
                    EntityPtr ent = test_.scene->CreateEntity(0, QStringList(), AttributeChange::Default,
                        replicated, replicated, temporary);
                    QVERIFY(ent);

                    QCOMPARE(ent->IsReplicated(), replicated);
                    QCOMPARE(ent->IsUnacked(), replicated); // replicated == server needs to ack entity
                    QCOMPARE(ent->IsTemporary(), temporary);

                    QCOMPARE(ent->Name(), EmptyQString);
                    QCOMPARE(ent->Description(), EmptyQString);
                    QCOMPARE(ent->Group(), EmptyQString);
                }
            }
        }
    }
}

// QTest entry point
QTEST_APPLESS_MAIN(TundraTest::Scene)
