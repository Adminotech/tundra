
#include "DebugOperatorNew.h"

#include "TestScene.h"

#include "Framework.h"
#include "SceneAPI.h"
#include "PluginAPI.h"
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
        test_.ProcessEvents();
    }

    void Scene::CreateEntity()
    {
        foreach(bool replicated, TrueAndFalse())
        {
            foreach(bool temporary, TrueAndFalse())
            {
                qDebug() << qPrintable(QString("replicated = %1 temporary = %2")
                    .arg(TruthyString(replicated), -6).arg(TruthyString(temporary), -6));

                QBENCHMARK
                {
                    EntityPtr ent = test_.scene->CreateEntity(0, QStringList(), AttributeChange::Default,
                        replicated, replicated, temporary);
                    
                    QVERIFY(ent);
                    QVERIFY(ent->ParentScene());
                    QVERIFY(!ent->Parent());

                    QCOMPARE(ent->IsReplicated(), replicated);
                    QCOMPARE(ent->IsUnacked(), replicated); // replicated == server needs to ack entity
                    QCOMPARE(ent->IsTemporary(), temporary);

                    QCOMPARE(ent->Name(), EmptyQString);
                    QCOMPARE(ent->Description(), EmptyQString);
                    QCOMPARE(ent->Group(), EmptyQString);

                    QCOMPARE(ent->NumComponents(), ZeroSizeT);
                    QCOMPARE(ent->NumChildren(), ZeroSizeT);
                }

                test_.ProcessEvents();
            }
        }
    }

    void Scene::CreateComponents_Unparented()
    {
        SceneAPI *sceneApi = test_.framework->Scene();

        foreach(const QString &componentTypeName, sceneApi->ComponentTypes())
        {
            u32 componentTypeId = sceneApi->ComponentTypeIdForTypeName(componentTypeName);

            qDebug() << qPrintable(QString("%1 %2").arg(componentTypeId, -4).arg(componentTypeName));

            /** @note This benchmark is not really sensible here. What we would want is to benchmark each
                component type separately to find out what components are slow to create.
                This will however give us some indication what the general perf of creating components is. */
            QBENCHMARK
            {
                ComponentPtr byName = sceneApi->CreateComponentByName(0, componentTypeName);

                QVERIFY(byName);
                QVERIFY(!byName->ParentScene());
                QVERIFY(!byName->ParentEntity());

                QCOMPARE(byName->TypeId(), componentTypeId);
                QCOMPARE(byName->TypeName(), componentTypeName);

                ComponentPtr byId = sceneApi->CreateComponentById(0, componentTypeId);

                QVERIFY(byId);
                QVERIFY(!byId->ParentScene());
                QVERIFY(!byId->ParentEntity());

                QCOMPARE(byId->TypeId(), componentTypeId);
                QCOMPARE(byId->TypeName(), componentTypeName);
            }

            test_.ProcessEvents();
        }
    }

    void Scene::CreateComponents_Parented()
    {
        SceneAPI *sceneApi = test_.framework->Scene();

        foreach(const QString &componentTypeName, sceneApi->ComponentTypes())
        {
            u32 componentTypeId = sceneApi->ComponentTypeIdForTypeName(componentTypeName);

            qDebug() << qPrintable(QString("%1 %2").arg(componentTypeId, -4).arg(componentTypeName));

            foreach(bool replicated, TrueAndFalse())
            {
                foreach(bool temporary, TrueAndFalse())
                {
                    /** @note Would be nice if benchmark could be enabled. This however makes the test take a lot of time per
                        component, some of the rendering related things are really heavy to initialize even in headless mode. */
                    //QBENCHMARK
                    //{
                        EntityPtr parent = test_.scene->CreateEntity(0, QStringList(), AttributeChange::Default,
                            replicated, replicated, temporary);

                        ComponentPtr byName = parent->CreateComponent(componentTypeName, "ByName");

                        QVERIFY(byName);
                        QVERIFY(byName->ParentScene());
                        QVERIFY(byName->ParentEntity());

                        QCOMPARE(byName->ParentScene(), test_.scene.get());
                        QCOMPARE(byName->ParentEntity(), parent.get());

                        QCOMPARE(byName->TypeId(), componentTypeId);
                        QCOMPARE(byName->TypeName(), componentTypeName);

                        ComponentPtr byId = parent->CreateComponent(componentTypeId, "ById");

                        QVERIFY(byId);
                        QVERIFY(byId->ParentScene());
                        QVERIFY(byId->ParentEntity());

                        QCOMPARE(byId->ParentScene(), test_.scene.get());
                        QCOMPARE(byId->ParentEntity(), parent.get());

                        QCOMPARE(byId->TypeId(), componentTypeId);
                        QCOMPARE(byId->TypeName(), componentTypeName);
                    //}

                    test_.ProcessEvents();
                }
            }
        }
    }
}

// QTest entry point
QTEST_APPLESS_MAIN(TundraTest::Scene)
