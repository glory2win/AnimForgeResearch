// DistanceMatchingTypes.cpp

#include "DistanceMatchingTypes.h"

float FDistanceCurveCache::GetTimeForRemainingDistance(float RemainingDistance) const
{
	if (!IsValid())
	{
		return 0.f;
	}

	// More (or equal) distance requested than the anim covers -> start of the anim.
	// This is also the late-trigger clamp: if we missed the ideal trigger window the
	// caller gets t=0 and simply lands short, never past, the target.
	if (RemainingDistance >= RemainingDistances[0])
	{
		return Times[0];
	}

	// Below the curve's minimum (negative distances, overshoot past the target) -> end.
	if (RemainingDistance < RemainingDistances.Last())
	{
		return Times.Last();
	}

	// Binary search on the non-increasing array for the FIRST index whose value is
	// <= RemainingDistance. Invariant: V[Lo] > RemainingDistance >= V[Hi].
	// "First" (rather than "any") is what makes flat zero-tails resolve to the earliest
	// time the root stops moving, so the settle portion of the stop plays naturally.
	int32 Lo = 0;
	int32 Hi = RemainingDistances.Num() - 1;
	while (Hi - Lo > 1)
	{
		const int32 Mid = (Lo + Hi) / 2;
		if (RemainingDistances[Mid] <= RemainingDistance)
		{
			Hi = Mid;
		}
		else
		{
			Lo = Mid;
		}
	}

	const float D0 = RemainingDistances[Lo];
	const float D1 = RemainingDistances[Hi];
	const float Alpha = (D0 - D1) > KINDA_SMALL_NUMBER ? (D0 - RemainingDistance) / (D0 - D1) : 1.f;
	return FMath::Lerp(Times[Lo], Times[Hi], Alpha);
}

float FDistanceCurveCache::GetRemainingDistanceAtTime(float Time) const
{
	if (!IsValid())
	{
		return 0.f;
	}
	if (Time <= Times[0])
	{
		return RemainingDistances[0];
	}
	if (Time >= Times.Last())
	{
		return RemainingDistances.Last();
	}

	// Samples are uniform, so the segment index is direct math instead of a search.
	const float Step = PlayLength / static_cast<float>(Times.Num() - 1);
	const float FracIndex = Time / Step;
	const int32 Index = FMath::Clamp(FMath::FloorToInt32(FracIndex), 0, Times.Num() - 2);
	const float Alpha = FracIndex - static_cast<float>(Index);
	return FMath::Lerp(RemainingDistances[Index], RemainingDistances[Index + 1], Alpha);
}
