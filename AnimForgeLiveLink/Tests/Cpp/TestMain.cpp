// AnimForgeLiveLink - standalone C++ tests for AnimForgeWarpVizShared.
//
// No Maya, no Unreal, no test framework: any C++14 compiler builds this.
//
//   cl /std:c++14 /EHsc /I..\..\UnrealGymProject\Shared\AnimForgeWarpVizShared ^
//      TestMain.cpp ..\..\UnrealGymProject\Shared\AnimForgeWarpVizShared\WarpVizJson.cpp ^
//      ..\..\UnrealGymProject\Shared\AnimForgeWarpVizShared\WarpVizProtocol.cpp ^
//      /Fe:warpviz_tests.exe && warpviz_tests.exe
//
// (or run build_and_run.bat, which finds MSVC/clang/gcc automatically)
//
// The cases intentionally mirror Tests/Python: both language implementations
// of the protocol and the skew warp must agree, and these tests are the
// contract that keeps them honest.

#include "SkewWarpMath.h"
#include "WarpVizJson.h"
#include "WarpVizProtocol.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace AnimForge::WarpViz;

// --- tiny assertion harness -------------------------------------------------

static int GChecksFailed = 0;
static int GChecksPassed = 0;

#define CHECK(Condition)                                                        \
    do {                                                                        \
        if (Condition) { ++GChecksPassed; }                                     \
        else {                                                                  \
            ++GChecksFailed;                                                    \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #Condition);    \
        }                                                                       \
    } while (0)

#define CHECK_NEAR(A, B, Tol)                                                   \
    do {                                                                        \
        const double _a = (A), _b = (B);                                        \
        if (std::fabs(_a - _b) <= (Tol)) { ++GChecksPassed; }                   \
        else {                                                                  \
            ++GChecksFailed;                                                    \
            std::printf("FAIL %s:%d  %s=%.12g vs %s=%.12g\n",                   \
                        __FILE__, __LINE__, #A, _a, #B, _b);                    \
        }                                                                       \
    } while (0)

static void Announce(const char* Name) { std::printf("[test] %s\n", Name); }

// --- JSON --------------------------------------------------------------------

static void TestJsonRoundTrip()
{
    Announce("json round trip");

    JsonValue Obj = JsonValue::MakeObject();
    Obj.Set("name", JsonValue("warp \"target\"\nline2"));
    Obj.Set("count", JsonValue(42));
    Obj.Set("ratio", JsonValue(0.125));
    Obj.Set("enabled", JsonValue(true));
    Obj.Set("nothing", JsonValue());

    JsonValue Arr = JsonValue::MakeArray();
    Arr.Add(JsonValue(1.5));
    Arr.Add(JsonValue("two"));
    Obj.Set("items", Arr);

    const std::string Text = Obj.Write();

    JsonValue Parsed;
    std::string Error;
    CHECK(JsonValue::Parse(Text, Parsed, Error));
    CHECK(Parsed.GetString("name") == "warp \"target\"\nline2");
    CHECK_NEAR(Parsed.GetNumber("count"), 42.0, 0.0);
    CHECK_NEAR(Parsed.GetNumber("ratio"), 0.125, 0.0);
    CHECK(Parsed.GetBool("enabled"));
    CHECK(Parsed.Find("nothing") && Parsed.Find("nothing")->IsNull());
    CHECK(Parsed.Find("items")->Items().size() == 2);
    CHECK(Parsed.Find("items")->Items()[1].AsString() == "two");
}

static void TestJsonEscapes()
{
    Announce("json escapes / unicode");

    JsonValue Parsed;
    std::string Error;
    CHECK(JsonValue::Parse("{\"s\":\"tab\\tnew\\nquote\\\"back\\\\\"}", Parsed, Error));
    CHECK(Parsed.GetString("s") == "tab\tnew\nquote\"back\\");

    // \u escape incl. surrogate pair (U+1F600).
    CHECK(JsonValue::Parse("{\"u\":\"\\u00e9 \\ud83d\\ude00\"}", Parsed, Error));
    const std::string U = Parsed.GetString("u");
    CHECK(U.size() == 7); // 2-byte e-acute + space + 4-byte emoji
}

static void TestJsonRejectsMalformed()
{
    Announce("json rejects malformed input");

    JsonValue Parsed;
    std::string Error;
    CHECK(!JsonValue::Parse("{", Parsed, Error));
    CHECK(!JsonValue::Parse("{\"a\":}", Parsed, Error));
    CHECK(!JsonValue::Parse("[1,2,]moretext", Parsed, Error));
    CHECK(!JsonValue::Parse("{\"a\":1}trailing", Parsed, Error));
    CHECK(!JsonValue::Parse("\"unterminated", Parsed, Error));
    CHECK(!Error.empty());
}

// --- framing ------------------------------------------------------------------

static void TestFraming()
{
    Announce("frame encode/decode + fragmentation + resync");

    const std::string Payload = "{\"type\":\"Handshake\"}";
    const std::vector<uint8_t> Frame = EncodeFrame(Payload);
    CHECK(Frame.size() == 8 + Payload.size());

    // Whole-frame decode.
    {
        FrameDecoder Decoder;
        Decoder.Feed(Frame.data(), Frame.size());
        std::string Out;
        CHECK(Decoder.Next(Out));
        CHECK(Out == Payload);
        CHECK(!Decoder.Next(Out));
    }

    // Byte-by-byte.
    {
        FrameDecoder Decoder;
        std::string Out;
        for (size_t i = 0; i < Frame.size(); ++i)
        {
            Decoder.Feed(&Frame[i], 1);
            if (i + 1 < Frame.size())
            {
                CHECK(!Decoder.Next(Out));
            }
        }
        CHECK(Decoder.Next(Out));
        CHECK(Out == Payload);
    }

    // Garbage prefix then two frames back to back.
    {
        FrameDecoder Decoder;
        const uint8_t Garbage[] = { 0x13, 0x37, 0x00 };
        Decoder.Feed(Garbage, sizeof(Garbage));
        Decoder.Feed(Frame.data(), Frame.size());
        Decoder.Feed(Frame.data(), Frame.size());
        std::string Out;
        CHECK(Decoder.Next(Out));
        CHECK(Decoder.Next(Out));
        CHECK(!Decoder.Next(Out));
        CHECK(Decoder.DroppedBytes() == 3);
    }

    // Corrupt length field is skipped.
    {
        FrameDecoder Decoder;
        std::vector<uint8_t> Bogus(kFrameMagic, kFrameMagic + 4);
        Bogus.push_back(0xFF); Bogus.push_back(0xFF);
        Bogus.push_back(0xFF); Bogus.push_back(0xFF);
        Decoder.Feed(Bogus.data(), Bogus.size());
        Decoder.Feed(Frame.data(), Frame.size());
        std::string Out;
        CHECK(Decoder.Next(Out));
        CHECK(Out == Payload);
    }
}

// --- protocol messages ---------------------------------------------------------

static void TestEvaluateRequestRoundTrip()
{
    Announce("EvaluateRequest encode/decode round trip");

    EvaluateRequest Request;
    Request.RequestId = "deadbeefcafef00d";
    Request.CharacterId = "AF_Mannequin";
    Request.ClipName = "MM_Vault_Low";
    Request.Range = { 10.0, 50.0, 30.0 };
    Request.WarpWindow = { 20.0, 45.0, 30.0 };
    Request.Method = WarpMethod::SkewWarp;
    Request.Target.Name = "warpTarget_loc";
    Request.Target.Translation = Vec3(120.0, 0.0, 340.0);
    Request.Target.Rotation = Quat(0.0, 0.7071, 0.0, 0.7071);
    Request.bWarpRotation = true;
    Request.GhostIntervalFrames = 5.0;
    for (int i = 0; i < 3; ++i)
    {
        TrajectorySample Sample;
        Sample.TimeSeconds = i / 30.0;
        Sample.Translation = Vec3(0.0, 0.0, 10.0 * i);
        Request.MayaRootSamples.push_back(Sample);
    }

    EvaluateRequest Decoded;
    std::string Error;
    CHECK(DecodeEvaluateRequest(EncodeEvaluateRequest(Request), Decoded, Error));
    CHECK(Decoded.RequestId == Request.RequestId);
    CHECK(Decoded.ClipName == Request.ClipName);
    CHECK(Decoded.Method == WarpMethod::SkewWarp);
    CHECK_NEAR(Decoded.WarpWindow.EndFrame, 45.0, 0.0);
    CHECK(Decoded.MayaRootSamples.size() == 3);
    CHECK(Decoded.Target.Translation.NearlyEquals(Vec3(120.0, 0.0, 340.0)));

    // Validation failures.
    EvaluateRequest Bad = Request;
    Bad.RequestId.clear();
    CHECK(!DecodeEvaluateRequest(EncodeEvaluateRequest(Bad), Decoded, Error));

    CHECK(!DecodeEvaluateRequest("garbage", Decoded, Error));
    CHECK(!DecodeEvaluateRequest(
        EncodeHandshake({ "Maya", "X" }), Decoded, Error)); // wrong type
}

static void TestEvaluateResultRoundTrip()
{
    Announce("EvaluateResult encode/decode round trip");

    EvaluateResult Result;
    Result.RequestId = "0123456789abcdef";
    Result.bSuccess = true;
    Result.EvaluationMs = 4.2;
    for (int i = 0; i < 2; ++i)
    {
        TrajectorySample Sample;
        Sample.TimeSeconds = i * 0.1;
        Sample.Translation = Vec3(i * 1.0, 0.0, i * 2.0);
        Result.WarpedTrajectory.push_back(Sample);
        Result.OriginalTrajectory.push_back(Sample);
    }
    GhostPose Pose;
    Pose.TimeSeconds = 0.1;
    JointTransform Joint;
    Joint.JointName = "root";
    Joint.Translation = Vec3(1.0, 2.0, 3.0);
    Pose.Joints.push_back(Joint);
    Result.GhostPoses.push_back(Pose);
    Result.Warnings.push_back("root drift 0.3cm");

    EvaluateResult Decoded;
    std::string Error;
    CHECK(DecodeEvaluateResult(EncodeEvaluateResult(Result), Decoded, Error));
    CHECK(Decoded.bSuccess);
    CHECK(Decoded.WarpedTrajectory.size() == 2);
    CHECK(Decoded.GhostPoses.size() == 1);
    CHECK(Decoded.GhostPoses[0].Joints[0].JointName == "root");
    CHECK(Decoded.Warnings.size() == 1);
    CHECK_NEAR(Decoded.EvaluationMs, 4.2, 1e-12);
}

// --- skew warp -------------------------------------------------------------------

static std::vector<TrajectorySample> StraightWalk(int Frames = 11, double Step = 10.0)
{
    std::vector<TrajectorySample> Samples;
    for (int i = 0; i < Frames; ++i)
    {
        TrajectorySample Sample;
        Sample.TimeSeconds = i / 30.0;
        Sample.Translation = Vec3(0.0, 0.0, Step * i);
        Samples.push_back(Sample);
    }
    return Samples;
}

static void TestSkewWarpMatrix()
{
    Announce("skew matrix W*D == T, shear property");

    const Vec3 D(3.0, -1.0, 12.0);
    const Vec3 T(5.0, 4.0, 9.0);
    const Mat3 W = ComputeSkewWarpMatrix(D, T);
    CHECK(W.Transform(D).NearlyEquals(T, 1e-9));

    const Mat3 WPure = ComputeSkewWarpMatrix(Vec3(0.0, 0.0, 10.0), Vec3(4.0, 0.0, 10.0));
    const Vec3 Perp(2.5, -7.0, 0.0);
    CHECK(WPure.Transform(Perp).NearlyEquals(Perp, 1e-12));

    // Degenerate D -> identity.
    const Mat3 WDegen = ComputeSkewWarpMatrix(Vec3(), Vec3(5.0, 0.0, 0.0));
    CHECK(WDegen.Transform(Vec3(1.0, 2.0, 3.0)).NearlyEquals(Vec3(1.0, 2.0, 3.0), 1e-12));
}

static void TestWarpTrajectory()
{
    Announce("trajectory warp endpoint / pre-window / post-window");

    const std::vector<TrajectorySample> Samples = StraightWalk(21);
    WarpTrajectoryParams Params;
    Params.WindowStartSeconds = Samples[5].TimeSeconds;
    Params.WindowEndSeconds = Samples[15].TimeSeconds;
    Params.TargetTranslation = Vec3(40.0, 0.0, 160.0);
    Params.bWarpRotation = false;

    const WarpTrajectoryResult Result = WarpTrajectory(Samples, Params);
    CHECK(Result.bWindowFound);

    // Before the window: untouched.
    for (int i = 0; i < 5; ++i)
    {
        CHECK(Result.Samples[i].Translation.NearlyEquals(Samples[i].Translation, 1e-12));
    }
    // Window end: on target.
    CHECK(Result.Samples[15].Translation.NearlyEquals(Vec3(40.0, 0.0, 160.0), 1e-9));
    // After the window: deltas preserved (rigid carry, no rotation warp).
    for (int i = 16; i < 21; ++i)
    {
        const Vec3 SourceDelta = Samples[i].Translation - Samples[i - 1].Translation;
        const Vec3 WarpedDelta = Result.Samples[i].Translation - Result.Samples[i - 1].Translation;
        CHECK(SourceDelta.NearlyEquals(WarpedDelta, 1e-9));
    }
}

static void TestWarpRotationCorrection()
{
    Announce("rotation correction reaches target progressively");

    const std::vector<TrajectorySample> Samples = StraightWalk();
    const double HalfSqrt2 = std::sqrt(0.5);

    WarpTrajectoryParams Params;
    Params.WindowStartSeconds = Samples.front().TimeSeconds;
    Params.WindowEndSeconds = Samples.back().TimeSeconds;
    Params.TargetTranslation = Vec3(0.0, 0.0, 100.0);
    Params.TargetRotation = Quat(0.0, HalfSqrt2, 0.0, HalfSqrt2); // 90 deg yaw
    Params.bWarpRotation = true;

    const WarpTrajectoryResult Result = WarpTrajectory(Samples, Params);

    const Quat End = Result.Samples.back().Rotation;
    CHECK(std::fabs(End.Dot(Params.TargetRotation)) > 1.0 - 1e-9);

    double PreviousAngle = 0.0;
    for (const TrajectorySample& Sample : Result.Samples)
    {
        const double W = std::fabs(Sample.Rotation.W) > 1.0 ? 1.0 : std::fabs(Sample.Rotation.W);
        const double Angle = 2.0 * std::acos(W);
        CHECK(Angle >= PreviousAngle - 1e-9);
        PreviousAngle = Angle;
    }
}

static void TestWarpDegenerateInPlace()
{
    Announce("degenerate in-place anim ramps to target");

    std::vector<TrajectorySample> Samples;
    for (int i = 0; i < 11; ++i)
    {
        TrajectorySample Sample;
        Sample.TimeSeconds = i / 30.0;
        Samples.push_back(Sample);
    }

    WarpTrajectoryParams Params;
    Params.WindowStartSeconds = 0.0;
    Params.WindowEndSeconds = Samples.back().TimeSeconds;
    Params.TargetTranslation = Vec3(0.0, 0.0, 60.0);
    Params.bWarpRotation = false;

    const WarpTrajectoryResult Result = WarpTrajectory(Samples, Params);
    CHECK(Result.Samples.front().Translation.NearlyEquals(Vec3(), 1e-12));
    CHECK(Result.Samples.back().Translation.NearlyEquals(Vec3(0.0, 0.0, 60.0), 1e-9));
    for (size_t i = 1; i < Result.Samples.size(); ++i)
    {
        CHECK(Result.Samples[i].Translation.Z >= Result.Samples[i - 1].Translation.Z - 1e-9);
    }
}

// --- main ------------------------------------------------------------------------

int main()
{
    TestJsonRoundTrip();
    TestJsonEscapes();
    TestJsonRejectsMalformed();
    TestFraming();
    TestEvaluateRequestRoundTrip();
    TestEvaluateResultRoundTrip();
    TestSkewWarpMatrix();
    TestWarpTrajectory();
    TestWarpRotationCorrection();
    TestWarpDegenerateInPlace();

    std::printf("\n%d checks passed, %d failed\n", GChecksPassed, GChecksFailed);
    return GChecksFailed == 0 ? 0 : 1;
}
