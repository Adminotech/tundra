
#include "DebugOperatorNew.h"

#include "TestScene.h"

#include "Framework.h"
#include "SceneAPI.h"
#include "PluginAPI.h"
#include "Scene.h"

#include "kNet/DataSerializer.h"

#include <QtTest/QtTest>

#include "MemoryLeakCheck.h"

namespace TundraTest
{
    Scene::Scene(const QString &config)
    {
        test_.SetConfig(config);
    }

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

    void Scene::Create_Entity()
    {
        foreach(bool replicated, TrueAndFalse())
        {
            foreach(bool temporary, TrueAndFalse())
            {
                qDebug() << qPrintable(QString("Replicated = %1 Temporary = %2")
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

    void Scene::Create_Attributes_Unparented()
    {
        SceneAPI *sceneApi = test_.framework->Scene();

        foreach(const QString &attributeTypeName, SceneAPI::AttributeTypes())
        {
            u32 attributeTypeId = sceneApi->AttributeTypeIdForTypeName(attributeTypeName);

            qDebug() << qPrintable(QString("%1 %2")
                .arg(attributeTypeId, -3).arg(attributeTypeName));

            kNet::DataSerializer dsName(64 * 1024);
            kNet::DataSerializer dsId(64 * 1024);

            QBENCHMARK
            {                
                IAttribute *byName = SceneAPI::CreateAttribute(attributeTypeName, "ByName");

                QVERIFY(byName);
                QVERIFY(!byName->Owner());

                dsName.ResetFill();
                byName->ToBinary(dsName);

                QVERIFY(dsName.BytesFilled() > 0);

                IAttribute *byId = SceneAPI::CreateAttribute(attributeTypeId, "ById");

                QVERIFY(byId);
                QVERIFY(!byId->Owner());

                dsId.ResetFill();
                byName->ToBinary(dsId);

                QVERIFY(dsName.BytesFilled() > 0);

                QVERIFY(dsName.BytesFilled() == dsId.BytesFilled());

                SAFE_DELETE(byName);
                SAFE_DELETE(byId);
            }

            test_.ProcessEvents();
        }
    }

    void Scene::Create_Components_Unparented()
    {
        SceneAPI *sceneApi = test_.framework->Scene();

        foreach(const QString &componentTypeName, sceneApi->ComponentTypes())
        {
            QVERIFY(sceneApi->IsComponentTypeRegistered(componentTypeName));
            QVERIFY(sceneApi->IsComponentFactoryRegistered(componentTypeName));

            u32 componentTypeId = sceneApi->ComponentTypeIdForTypeName(componentTypeName);

            qDebug() << qPrintable(QString("%1 %2")
                .arg(componentTypeId, -3).arg(componentTypeName));

            /** @note This will benchmark all types together, so it wont give us a very good metric.
                There needs to be separate test function for each component that we want to benchmark. */
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

    void Scene::Create_Components_Parented()
    {
        SceneAPI *sceneApi = test_.framework->Scene();

        foreach(const QString &componentTypeName, sceneApi->ComponentTypes())
        {
            u32 componentTypeId = sceneApi->ComponentTypeIdForTypeName(componentTypeName);

            qDebug() << qPrintable(QString("%1 %2")
                .arg(componentTypeName).arg(componentTypeId));

            foreach(bool replicated, TrueAndFalse())
            {
                foreach(bool temporary, TrueAndFalse())
                {
                    qDebug() << qPrintable(QString("  Replicated = %1 Temporary = %2")
                        .arg(TruthyString(replicated), -6).arg(TruthyString(temporary), -6));

                    EntityPtr parent = test_.scene->CreateEntity(0, QStringList(), AttributeChange::Default,
                        replicated, replicated, temporary);

                    /** @note This benchmarking is enabled and will be very slow for certain rendering related component.
                        The output should be analyzed and the component construction optimized if it takes a long time.
                        Note that we are operating in headless mode, so many rendering related components are probably
                        doing some heavy unneccesary work (eg. creating meshes, textures, materials, 
                        billboards etc.) when rendering will never be done. */
                    QBENCHMARK
                    {
                        QString iteration = QString::number(__iteration_controller.i); // from QBENCHMARK

                        if (__iteration_controller.i > 0 && __iteration_controller.i % 100 == 0)
                            qDebug() << "    Iteration" << __iteration_controller.i;

                        ComponentPtr byName = parent->CreateComponent(componentTypeName, "ByName_" + iteration);

                        QVERIFY(byName);
                        QVERIFY(byName->ParentScene());
                        QVERIFY(byName->ParentEntity());

                        QCOMPARE(byName->ParentScene(), test_.scene.get());
                        QCOMPARE(byName->ParentEntity(), parent.get());

                        QCOMPARE(byName->TypeId(), componentTypeId);
                        QCOMPARE(byName->TypeName(), componentTypeName);

                        ComponentPtr byId = parent->CreateComponent(componentTypeId, "ById_" + iteration);

                        QVERIFY(byId);
                        QVERIFY(byId->ParentScene());
                        QVERIFY(byId->ParentEntity());

                        QCOMPARE(byId->ParentScene(), test_.scene.get());
                        QCOMPARE(byId->ParentEntity(), parent.get());

                        QCOMPARE(byId->TypeId(), componentTypeId);
                        QCOMPARE(byId->TypeName(), componentTypeName);
                    }

                    test_.ProcessEvents();
                }
            }
        }
    }
}

// QTest entry point
QTEST_APPLESS_MAIN(TundraTest::Scene);
