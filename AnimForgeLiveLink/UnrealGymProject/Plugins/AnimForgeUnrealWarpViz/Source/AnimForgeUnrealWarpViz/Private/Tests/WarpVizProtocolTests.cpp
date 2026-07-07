// AnimForgeUnrealWarpViz - WarpVizProtocolTests.cpp
//
// Engine-side automation tests for the shared protocol. Run from the editor
// (Session Frontend > Automation, filter "AnimForge.WarpViz") or headless:
//
//   UnrealEditor-Cmd.exe AnimForgeGym.uproject -ExecCmds="Automation RunTests AnimForge.WarpViz; Quit" -unattended -nopause -nullrhi
//
// These mirror Tests/Cpp/TestMain.cpp and the Python suite, so all three
// environments pin down the same behavior.

#include "Misc/AutomationTest.h"

#include "WarpVizProtocol.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AnimForge::WarpViz;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWarpVizFrameRoundTripTest,
	"AnimForge.WarpViz.Protocol.FrameRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FWarpVizFrameRoundTripTest::RunTest(const FString& Parameters)
{
	const std::string Payload = "{\"hello\":\"gym\"}";
	const std::vector<uint8_t> Frame = EncodeFrame(Payload);

	FrameDecoder Decoder;
	Decoder.Feed(Frame.data(), Frame.size());

	std::string Decoded;
	TestTrue(TEXT("frame decodes"), Decoder.Next(Decoded));
	TestEqual(TEXT("payload survives"), FString(Decoded.c_str()), FString(Payload.c_str()));
	TestFalse(TEXT("no spurious second frame"), Decoder.Next(Decoded));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWarpVizFrameFragmentationTest,
	"AnimForge.WarpViz.Protocol.FrameFragmentation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FWarpVizFrameFragmentationTest::RunTest(const FString& Parameters)
{
	const std::string Payload(1000, 'x');
	const std::vector<uint8_t> Frame = EncodeFrame(Payload);

	// Byte-by-byte delivery must decode exactly one frame at the end.
	FrameDecoder Decoder;
	std::string Decoded;
	for (size_t Index = 0; Index < Frame.size(); ++Index)
	{
		Decoder.Feed(&Frame[Index], 1);
		if (Index + 1 < Frame.size())
		{
			TestFalse(TEXT("no frame before all bytes arrive"), Decoder.Next(Decoded));
		}
	}
	TestTrue(TEXT("frame decodes after last byte"), Decoder.Next(Decoded));
	TestEqual(TEXT("length preserved"), static_cast<int32>(Decoded.size()), 1000);

	// Garbage before the magic must be dropped, not fatal.
	FrameDecoder Resync;
	const uint8_t Garbage[] = { 0x00, 0xFF, 0x13, 0x37 };
	Resync.Feed(Garbage, sizeof(Garbage));
	Resync.Feed(Frame.data(), Frame.size());
	TestTrue(TEXT("decoder resyncs after garbage"), Resync.Next(Decoded));
	TestEqual(TEXT("dropped byte count"), static_cast<int32>(Resync.DroppedBytes()), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWarpVizEvaluateRequestRoundTripTest,
	"AnimForge.WarpViz.Protocol.EvaluateRequestRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FWarpVizEvaluateRequestRoundTripTest::RunTest(const FString& Parameters)
{
	EvaluateRequest Request;
	Request.RequestId = "deadbeefdeadbeef";
	Request.CharacterId = "AF_Mannequin";
	Request.ClipName = "MM_Vault_Low";
	Request.Range = { 10.0, 50.0, 30.0 };
	Request.WarpWindow = { 20.0, 45.0, 30.0 };
	Request.Method = WarpMethod::SkewWarp;
	Request.Target.Name = "warpTarget_loc";
	Request.Target.Translation = Vec3(120.0, 0.0, 340.0);
	Request.Target.Rotation = Quat(0.0, 0.7071, 0.0, 0.7071);
	Request.GhostIntervalFrames = 5.0;
	Request.MayaRootSamples.push_back({ 0.0, Vec3(0.0, 0.0, 0.0), Quat::Identity() });
	Request.MayaRootSamples.push_back({ 0.5, Vec3(0.0, 0.0, 50.0), Quat::Identity() });

	EvaluateRequest Decoded;
	std::string Error;
	TestTrue(TEXT("request decodes"),
		DecodeEvaluateRequest(EncodeEvaluateRequest(Request), Decoded, Error));
	TestEqual(TEXT("request id"), FString(Decoded.RequestId.c_str()), FString("deadbeefdeadbeef"));
	TestEqual(TEXT("clip"), FString(Decoded.ClipName.c_str()), FString("MM_Vault_Low"));
	TestTrue(TEXT("method"), Decoded.Method == WarpMethod::SkewWarp);
	TestEqual(TEXT("window end"), Decoded.WarpWindow.EndFrame, 45.0);
	TestEqual(TEXT("sample count"), static_cast<int32>(Decoded.MayaRootSamples.size()), 2);
	TestTrue(TEXT("target pos"), Decoded.Target.Translation.NearlyEquals(Vec3(120.0, 0.0, 340.0)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWarpVizProtocolRejectsBadInputTest,
	"AnimForge.WarpViz.Protocol.RejectsBadInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::SmokeFilter)

bool FWarpVizProtocolRejectsBadInputTest::RunTest(const FString& Parameters)
{
	EvaluateRequest Decoded;
	std::string Error;

	TestFalse(TEXT("non-JSON rejected"),
		DecodeEvaluateRequest("not json at all", Decoded, Error));
	TestFalse(TEXT("wrong type rejected"), DecodeEvaluateRequest(
		EncodeHandshake({ "Maya", "X" }), Decoded, Error));

	// Version mismatch.
	const std::string Bumped =
		"{\"type\":\"EvaluateRequest\",\"protocolVersion\":99,\"payload\":{}}";
	TestFalse(TEXT("version mismatch rejected"), DecodeEvaluateRequest(Bumped, Decoded, Error));
	TestTrue(TEXT("error names the version"),
		FString(Error.c_str()).Contains(TEXT("version")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
