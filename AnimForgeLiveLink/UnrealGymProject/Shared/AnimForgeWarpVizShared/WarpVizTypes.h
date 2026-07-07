// AnimForgeWarpVizShared - WarpVizTypes.h
// Plain C++14 value types shared between the Maya plugin (AnimForgeMayaWarpViz)
// and the Unreal plugin (AnimForgeUnrealWarpViz). No Maya / Unreal / STL-heavy
// dependencies beyond <vector> and <string> so this compiles in both toolchains
// and in the standalone test harness.

#pragma once

#include <cmath>
#include <string>
#include <vector>

namespace AnimForge
{
namespace WarpViz
{

// ---------------------------------------------------------------------------
// Math value types
// ---------------------------------------------------------------------------

struct Vec3
{
    double X = 0.0;
    double Y = 0.0;
    double Z = 0.0;

    Vec3() = default;
    Vec3(double InX, double InY, double InZ) : X(InX), Y(InY), Z(InZ) {}

    Vec3 operator+(const Vec3& R) const { return Vec3(X + R.X, Y + R.Y, Z + R.Z); }
    Vec3 operator-(const Vec3& R) const { return Vec3(X - R.X, Y - R.Y, Z - R.Z); }
    Vec3 operator*(double S) const { return Vec3(X * S, Y * S, Z * S); }

    double Dot(const Vec3& R) const { return X * R.X + Y * R.Y + Z * R.Z; }
    double LengthSquared() const { return Dot(*this); }
    double Length() const { return std::sqrt(LengthSquared()); }

    bool NearlyEquals(const Vec3& R, double Tolerance = 1e-6) const
    {
        return std::fabs(X - R.X) <= Tolerance
            && std::fabs(Y - R.Y) <= Tolerance
            && std::fabs(Z - R.Z) <= Tolerance;
    }
};

// Unit quaternion, (X, Y, Z, W) convention, W is the scalar part.
struct Quat
{
    double X = 0.0;
    double Y = 0.0;
    double Z = 0.0;
    double W = 1.0;

    Quat() = default;
    Quat(double InX, double InY, double InZ, double InW) : X(InX), Y(InY), Z(InZ), W(InW) {}

    static Quat Identity() { return Quat(0.0, 0.0, 0.0, 1.0); }

    Quat Conjugate() const { return Quat(-X, -Y, -Z, W); }

    double Dot(const Quat& R) const { return X * R.X + Y * R.Y + Z * R.Z + W * R.W; }

    Quat Normalized() const
    {
        const double Len = std::sqrt(Dot(*this));
        if (Len < 1e-12)
        {
            return Identity();
        }
        const double Inv = 1.0 / Len;
        return Quat(X * Inv, Y * Inv, Z * Inv, W * Inv);
    }

    // Hamilton product: (*this) then... note composition order matches UE:
    // (A * B) rotates by B first, then A.
    Quat operator*(const Quat& R) const
    {
        return Quat(
            W * R.X + X * R.W + Y * R.Z - Z * R.Y,
            W * R.Y - X * R.Z + Y * R.W + Z * R.X,
            W * R.Z + X * R.Y - Y * R.X + Z * R.W,
            W * R.W - X * R.X - Y * R.Y - Z * R.Z);
    }

    Vec3 RotateVector(const Vec3& V) const
    {
        // v' = q * (v, 0) * q^-1 expanded to the optimized form.
        const Vec3 Q(X, Y, Z);
        const Vec3 T(
            2.0 * (Q.Y * V.Z - Q.Z * V.Y),
            2.0 * (Q.Z * V.X - Q.X * V.Z),
            2.0 * (Q.X * V.Y - Q.Y * V.X));
        return Vec3(
            V.X + W * T.X + (Q.Y * T.Z - Q.Z * T.Y),
            V.Y + W * T.Y + (Q.Z * T.X - Q.X * T.Z),
            V.Z + W * T.Z + (Q.X * T.Y - Q.Y * T.X));
    }

    static Quat Slerp(const Quat& A, const Quat& B, double Alpha)
    {
        Quat To = B;
        double CosTheta = A.Dot(B);
        if (CosTheta < 0.0) // take the short arc
        {
            CosTheta = -CosTheta;
            To = Quat(-B.X, -B.Y, -B.Z, -B.W);
        }

        double ScaleA;
        double ScaleB;
        if (CosTheta > 0.9995) // nearly parallel: lerp to avoid division by ~0
        {
            ScaleA = 1.0 - Alpha;
            ScaleB = Alpha;
        }
        else
        {
            const double Theta = std::acos(CosTheta);
            const double InvSin = 1.0 / std::sin(Theta);
            ScaleA = std::sin((1.0 - Alpha) * Theta) * InvSin;
            ScaleB = std::sin(Alpha * Theta) * InvSin;
        }

        return Quat(
            ScaleA * A.X + ScaleB * To.X,
            ScaleA * A.Y + ScaleB * To.Y,
            ScaleA * A.Z + ScaleB * To.Z,
            ScaleA * A.W + ScaleB * To.W).Normalized();
    }
};

// Row-major 3x3 matrix. Only what the skew warp needs.
struct Mat3
{
    double M[3][3];

    static Mat3 IdentityMatrix()
    {
        Mat3 R;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                R.M[i][j] = (i == j) ? 1.0 : 0.0;
        return R;
    }

    Vec3 Transform(const Vec3& V) const
    {
        return Vec3(
            M[0][0] * V.X + M[0][1] * V.Y + M[0][2] * V.Z,
            M[1][0] * V.X + M[1][1] * V.Y + M[1][2] * V.Z,
            M[2][0] * V.X + M[2][1] * V.Y + M[2][2] * V.Z);
    }
};

// ---------------------------------------------------------------------------
// Domain types (mirrored 1:1 by Scripts/warpviz_protocol.py)
// ---------------------------------------------------------------------------

enum class WarpMethod
{
    SkewWarp,       // shear map preserving motion shape (default, matches UE SkewWarp)
    SimpleWarp,     // uniform per-axis scale of root translation
    Scale,          // uniform scalar scale along original direction only
    Unknown
};

const char* WarpMethodToString(WarpMethod Method);
WarpMethod WarpMethodFromString(const std::string& Name);

struct TimeRange
{
    double StartFrame = 0.0;
    double EndFrame = 0.0;
    double Fps = 30.0;

    double DurationSeconds() const
    {
        return (Fps > 0.0) ? (EndFrame - StartFrame) / Fps : 0.0;
    }
};

// Warp target as authored in Maya (a locator's world transform).
struct WarpTarget
{
    std::string Name;       // Maya locator name, echoed back for the UI
    Vec3 Translation;
    Quat Rotation;
};

// One sample of the root trajectory.
struct TrajectorySample
{
    double TimeSeconds = 0.0;
    Vec3 Translation;
    Quat Rotation;
};

struct JointTransform
{
    std::string JointName;
    Vec3 Translation;
    Quat Rotation;
};

// A full-skeleton snapshot at one time, used to draw ghost poses in Maya.
struct GhostPose
{
    double TimeSeconds = 0.0;
    std::vector<JointTransform> Joints;
};

} // namespace WarpViz
} // namespace AnimForge
