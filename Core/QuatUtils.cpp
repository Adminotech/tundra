#include "CoreStableHeaders.h"
#include "Quaternion.h"
#include "QuatUtils.h"

namespace Core
{
    Quaternion UnpackQuaternionFromFloat3(float x, float y, float z)
    {
        float sq = x*x+y*y+z*z;
        // If the inputted coordinates are already too large in magnitude, renormalize the inputs and just set w = 0.
        // It can happen in two cases: Either float imprecision gave us a bit too high values, so setting w=0 is the proper action,
        // or then server sent us values that are bad to begin with. Anything is incorrect in this case, but to preserve at least
        // some sensibility in computations, renormalize the components and set w=0.
        if (sq >= 1.f) 
        {              
            float invNorm = 1.f / sqrt(sq);
            return Core::Quaternion(x * invNorm, y * invNorm, z * invNorm, 0.f);
        }
        float w = 1.f - sqrt(sq);
        return Quaternion(x, y, z, w);
    }
    
    Vector3D<float> PackQuaternionToFloat3(float x, float y, float z, float w)
    {
        // A quaternion is sent over the stream in a slightly compressed form - the w component is omitted.
        // The other end can reconstruct the w component because the quat is normed.
        
        float norm = (float)sqrt(x * x + y * y + z * z + w * w);
        const float epsilon = 1e-6f;
        if (norm  < epsilon)
        {
            return Vector3D<float>();
            ///\todo Log error - singular quaternion! App logic is in bad state here.
        }
        else
        {
            // Normalize the quaternion. \note For optimization purposes, we could only normalize if the norm is not near to one.
            norm = 1.0f / norm;

            if (w < 0.0f) // The server will reconstruct the w component as positive - so negate the whole quat here if w is negative.
                norm = -norm;
            
            x *= norm;
            y *= norm;
            z *= norm;
        }

        return Vector3D<float>(x, y, z);
    }
}
