
#pragma once

#include "TestHelpers.h"

// Mainly mean to test few parts of MathGeoLib that might utilize SSE
// to see if it makes a difference in Tundra benchmarks.
namespace TundraTest
{
    class Math : public QObject
    {
        Q_OBJECT
    
    public:
        Math();

    private slots:
        void initTestCase();     // QTest
        void cleanupTestCase();  // QTest
        void cleanup();          // QTest

        void Op_float3_data();
        void Op_float3();

        void Op_float4_data();
        void Op_float4();

        void MathFunc_data();
        void MathFunc();

        /** @todo This cant be done without linking to OgreRenderingModule
            and as a executable project, this would mean that you need to
            manually copy the runtime (DLL/so/dylib) to /bin. */
        //void Placeable_LocalToWorld_data();
        //void Placeable_LocalToWorld();

    private:
        void float3Data();
        void float4Data();
        //void ParentChildData();

        TestFramework test_;
    };
}
