
#include "DebugOperatorNew.h"

#include "TestMath.h"

#include "Framework.h"
#include "Math/MathFunc.h"
#include "Math/float3.h"
#include "Math/float4.h"

#include "Scene.h"
#include "Entity.h"
//#include "EC_Placeable.h"

#include <QtTest/QtTest>

#include "MemoryLeakCheck.h"

namespace TundraTest
{
    Math::Math()
    {
    }

    void Math::initTestCase()
    {
        test_.Initialize();
    }

    void Math::cleanupTestCase()
    {
        test_.ProcessEvents();
        test_.scene->RemoveAllEntities();
        test_.ProcessEvents();
    }

    void Math::cleanup()
    {
        test_.ProcessEvents();
    }

    void Math::float3Data()
    {
        QTest::addColumn<QString>("op");
        QTest::addColumn<float3>("a");
        QTest::addColumn<float3>("b");

        foreach(const QString &op, QStringList() << "+" << "-" << "/" << "*")
        {
            for (int i=0; i<=1000000; i+=100000)
            {
                float f = static_cast<float>(i);
                QTest::newRow(qPrintable(op)) << op << float3(-f*2.f, f, -f) << float3(f, -f*2.f, f);
            }
        }
    }

    void Math::float4Data()
    {
        QTest::addColumn<QString>("op");
        QTest::addColumn<float4>("a");
        QTest::addColumn<float4>("b");

        foreach(const QString &op, QStringList() << "+" << "-" << "/" << "*")
        {
            for (int i=0; i<=1000000; i+=100000)
            {
                float f = static_cast<float>(i);
                QTest::newRow(qPrintable(op)) << op << float4(-f*2.f, f, -f*2.f, f) << float4(f, -f*2.f, f, -f*2.f);
            }
        }
    }

    void Math::Op_float3_data()
    {
        float3Data();
    }

    void Math::Op_float3()
    {
        QFETCH(QString, op);
        QFETCH(float3, a);
        QFETCH(float3, b);

        if (op == "+")
        {
            QBENCHMARK { a.Add(b); }
        }
        else if (op == "-")
        {
            QBENCHMARK { a.Sub(b); }
        }
        else if (op == "/")
        {
            QBENCHMARK { a.Div(b); }
        }
        else if (op == "*")
        {
            QBENCHMARK { a.Mul(b); }
        }
    }

    void Math::Op_float4_data()
    {
        float4Data();
    }

    void Math::Op_float4()
    {
        QFETCH(QString, op);
        QFETCH(float4, a);
        QFETCH(float4, b);

        if (op == "+")
        {
            QBENCHMARK { a.Add(b); }
        }
        else if (op == "-")
        {
            QBENCHMARK { a.Sub(b); }
        }
        else if (op == "/")
        {
            QBENCHMARK { a.Div(b); }
        }
        else if (op == "*")
        {
            QBENCHMARK { a.Mul(b); }
        }
    }

    void Math::MathFunc_data()
    {
        float3Data();
    }

    void Math::MathFunc()
    {
        QFETCH(QString, op);
        QFETCH(float3, a);
        QFETCH(float3, b);

        if (op == "+")
        {
            QBENCHMARK { Max(a.x, b.x); }
        }
        else if (op == "-")
        {
            QBENCHMARK { Min(a.x, b.x); }
        }
    }

    /* See header...
    void Math::ParentChildData()
    {
        QTest::addColumn<entity_id_t>("parentEntityId");
        QTest::addColumn<entity_id_t>("childEntityId");

        EntityPtr parent = test_.scene->CreateLocalEntity(QStringList() << EC_Placeable::TypeNameStatic(), AttributeChange::LocalOnly, false, true);
        PlaceablePtr parentPlaceable = parent->Component<EC_Placeable>();

        test_.ProcessEvents();

        EntityPtr child = test_.scene->CreateLocalEntity(QStringList() << EC_Placeable::TypeNameStatic(), AttributeChange::LocalOnly, false, true);
        PlaceablePtr childPlaceable = child->Component<EC_Placeable>();
        
        test_.ProcessEvents();

        child->SetParent(parent);

        test_.ProcessEvents();

        Transform t = parentPlaceable->transform.Get();
        t.pos.Set(10, -20, 30);
        t.rot.Set(-10, 20, -30);
        t.scale.Set(1, 2, 3);
        parentPlaceable->transform.Set(t, AttributeChange::LocalOnly);

        t = childPlaceable->transform.Get();
        t.pos.Set(30, -10, 20);
        t.rot.Set(-30, 10, -20);
        t.scale.Set(3, 1, 2);
        childPlaceable->transform.Set(t, AttributeChange::LocalOnly);

        test_.ProcessEvents();

        QTest::newRow("Entity level parented Entity pair") << parent->Id() << child->Id();
    }

    void Math::Placeable_LocalToWorld_data()
    {
        ParentChildData();
    }

    void Math::Placeable_LocalToWorld()
    {
        QFETCH(entity_id_t, parentEntityId);
        QFETCH(entity_id_t, childEntityId);

        EntityPtr parent = test_.scene->EntityById(parentEntityId);
        QVERIFY(parent);
        PlaceablePtr parentPlaceable = parent->Component<EC_Placeable>();
        QVERIFY(parentPlaceable);

        EntityPtr child = test_.scene->EntityById(childEntityId);
        QVERIFY(child);
        PlaceablePtr childPlaceable = child->Component<EC_Placeable>();
        QVERIFY(childPlaceable);

        QBENCHMARK
        {
            childPlaceable->LocalToWorld();
        }
    }
    */
}

// QTest entry point
QTEST_APPLESS_MAIN(TundraTest::Math);
