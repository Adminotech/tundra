
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
        test_.ProcessEvents();
        test_.scene->RemoveAllEntities();
        test_.ProcessEvents();
    }

    void Scene::Create_Entity_data()
    {
        QTest::addColumn<bool>("replicated");
        QTest::addColumn<bool>("temporary");

        foreach(const bool replicated, TrueAndFalse())
        {
            foreach(const bool temporary, TrueAndFalse())
            {
                QString name = QString("Replicated = %1 Temporary = %2").arg(TruthyString(replicated)).arg(TruthyString(temporary));
                QTest::newRow(qPrintable(name)) << replicated << temporary;
            }
        }
    }

    void Scene::Create_Entity()
    {
        QFETCH(bool, replicated);
        QFETCH(bool, temporary);

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
    }

    void Scene::Create_Attributes_Unparented_data()
    {
        QTest::addColumn<QString>("attributeTypeName");
        QTest::addColumn<u32>("attributeTypeId");

        SceneAPI *sceneApi = test_.framework->Scene();

        foreach(const QString &attributeTypeName, SceneAPI::AttributeTypes())
        {
            u32 attributeTypeId = sceneApi->AttributeTypeIdForTypeName(attributeTypeName);

            QString name = QString("%1 %2").arg(attributeTypeName).arg(attributeTypeId);
            QTest::newRow(qPrintable(name)) << attributeTypeName << attributeTypeId;
        }
    }

    void Scene::Create_Attributes_Unparented()
    {
        QFETCH(QString, attributeTypeName);
        QFETCH(u32, attributeTypeId);

        SceneAPI *sceneApi = test_.framework->Scene();

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
    }

    void Scene::Create_Components_Unparented_data()
    {
        QTest::addColumn<QString>("componentTypeName");
        QTest::addColumn<u32>("componentTypeId");

        SceneAPI *sceneApi = test_.framework->Scene();

        foreach(const QString &componentTypeName, sceneApi->ComponentTypes())
        {
            u32 componentTypeId = sceneApi->ComponentTypeIdForTypeName(componentTypeName);

            QString name = QString("%1 %2").arg(componentTypeName).arg(componentTypeId);
            QTest::newRow(qPrintable(name)) << componentTypeName << componentTypeId;
        }
    }

    void Scene::Create_Components_Unparented()
    {
        QFETCH(QString, componentTypeName);
        QFETCH(u32, componentTypeId);

        SceneAPI *sceneApi = test_.framework->Scene();

        QVERIFY(sceneApi->IsComponentTypeRegistered(componentTypeName));
        QVERIFY(sceneApi->IsComponentFactoryRegistered(componentTypeName));

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
    }

    void Scene::Create_Components_Parented_data()
    {
        QTest::addColumn<QString>("componentTypeName");
        QTest::addColumn<u32>("componentTypeId");
        QTest::addColumn<bool>("replicated");
        QTest::addColumn<bool>("temporary");

        SceneAPI *sceneApi = test_.framework->Scene();

        foreach(const QString &componentTypeName, sceneApi->ComponentTypes())
        {
            u32 componentTypeId = sceneApi->ComponentTypeIdForTypeName(componentTypeName);

            foreach(const bool replicated, TrueAndFalse())
            {
                foreach(const bool temporary, TrueAndFalse())
                {
                    QString name = QString("%1 %2 Replicated = %3 Temporary = %4")
                        .arg(componentTypeName).arg(componentTypeId).arg(TruthyString(replicated)).arg(TruthyString(temporary));
                    QTest::newRow(qPrintable(name)) << componentTypeName << componentTypeId << replicated << temporary;
                }
            }
        }
    }

    void Scene::Create_Components_Parented()
    {
        QFETCH(QString, componentTypeName);
        QFETCH(u32, componentTypeId);
        QFETCH(bool, replicated);
        QFETCH(bool, temporary);

        SceneAPI *sceneApi = test_.framework->Scene();

        QBENCHMARK
        {
            /* @note Creating and removing the parent entity inside the benchmark will skew it a bit.
                But if we share a entity for all the benchmark iterations it will get impossibly slow
                if the component does queries (eg. for Placeable) to the parent entity, as it might 
                have thousands of components. */ 
            EntityPtr parent = test_.scene->CreateEntity(0, QStringList(), AttributeChange::Default,
                replicated, replicated, temporary);

            QString iteration = QString::number(__iteration_controller.i); // from QBENCHMARK

            ComponentPtr byName = parent->CreateComponent(componentTypeName, "ByName_" + iteration, AttributeChange::Default, replicated);

            QVERIFY(byName);
            QVERIFY(byName->ParentScene());
            QVERIFY(byName->ParentEntity());

            QCOMPARE(byName->ParentScene(), test_.scene.get());
            QCOMPARE(byName->ParentEntity(), parent.get());

            QCOMPARE(byName->TypeId(), componentTypeId);
            QCOMPARE(byName->TypeName(), componentTypeName);

            // SoundListener forces replication to false in ctor, bug?
            if (componentTypeName.compare("EC_SoundListener") != 0)
            {
                QCOMPARE(byName->IsReplicated(), replicated);
                QCOMPARE(byName->IsUnacked(), replicated); // replicated == server needs to ack entity
            }
            QCOMPARE(byName->IsTemporary(), temporary);

            ComponentPtr byId = parent->CreateComponent(componentTypeId, "ById_" + iteration, AttributeChange::Default, replicated);

            QVERIFY(byId);
            QVERIFY(byId->ParentScene());
            QVERIFY(byId->ParentEntity());

            QCOMPARE(byId->ParentScene(), test_.scene.get());
            QCOMPARE(byId->ParentEntity(), parent.get());

            QCOMPARE(byId->TypeId(), componentTypeId);
            QCOMPARE(byId->TypeName(), componentTypeName);

            // SoundListener forces replication to false in ctor, bug?
            if (componentTypeName.compare("EC_SoundListener") != 0)
            {
                QCOMPARE(byId->IsReplicated(), replicated);
                QCOMPARE(byId->IsUnacked(), replicated); // replicated == server needs to ack entity
            }
            QCOMPARE(byId->IsTemporary(), temporary);

            test_.scene->RemoveEntity(parent->Id());
        }
    }
}

// QTest entry point
QTEST_APPLESS_MAIN(TundraTest::Scene);
