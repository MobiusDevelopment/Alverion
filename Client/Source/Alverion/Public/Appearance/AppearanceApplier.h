#pragma once

#include "CoreMinimal.h"

class AActor;
class UAnimationAsset;
class UClass;
class USkeletalMesh;

namespace Network
{
	struct FAppearance;
}

/**
 * Puts an appearance onto a spawned MetaHuman, in one place, so character creation, character select and a stranger in town agree from the same bytes.
 * Each head is a whole character rather than a face on a shared body, because a MetaHuman bakes head and body textures together and they only match as a pair.
 * Grooms wear bindings baked for that character's face; hair colour, skin tone and eye colour shift the materials the character was built with.
 */
namespace Appearance
{
	/** What a body is doing, which picks the clip it plays. Characters walk or run on land, and swim in deep water. */
	enum class EMotion : uint8
	{
		Idle,
		Run,
		Fall,
		SwimIdle,
		Swim,
		GoblinIdle,
		GoblinRun,
		Walk,

		/** Moving over land at the speed given to animate, which picks the clip that suits it best: a walk, or a run. */
		Move,

		/** The same for the goblin, which has only its run. A goblin walk clip would join it here. */
		GoblinMove
	};

	/** A clip a body can move with, and how fast it travels on that body. */
	struct FMovingClip
	{
		EMotion motion;
		float speedCm;
	};

	/**
	 * The clip a body moving at the speed should play: the one that leaves its feet the least slip once its rate is clamped, which is how far the speed the clip covers is from the body's, in ratio.
	 * The clip already playing stays unless another cuts the slip by a clear margin, so a body at the crossover does not swap clips every frame.
	 * A clip that could not be measured is not considered, and when none could be the last of them is the answer, which is listed as the fastest. The list must not be empty.
	 */
	ALVERION_API EMotion pickMovingMotion(TConstArrayView<FMovingClip> clips, float speedCm, EMotion playing);

	/**
	 * How fast a run clip travels on a body, in centimetres a second, or zero when that cannot be told.
	 * A clip with root motion says so itself. Otherwise it is how fast a planted foot slides back through the body's space, found by posing the clip on the body's own bones over one loop.
	 * The feet are foot_l and foot_r, or ball_l and ball_r when a skeleton has no foot bones.
	 */
	ALVERION_API float measureRunSpeed(const UAnimationAsset& clip, const USkeletalMesh& mesh);

	/**
	 * The rate a run clip plays at for a body that moves at the speed, as a ratio of the clip's own pace: the speed over the one the clip's feet cover, so the strides match the ground.
	 * Kept within a range that stopping, starting and teleports cannot push into a crawl or a blur. A clip that could not be measured plays at its own rate, which is one.
	 */
	ALVERION_API float playRate(float speedCm, float clipSpeedCm);

	/**
	 * Plays the clip for a motion on a MetaHuman's body, looping, and leaves it alone while the motion stays the same, so a loop keeps its phase.
	 * A run clip follows the speed the body moves at, which changes its rate and never restarts it.
	 */
	ALVERION_API void animate(AActor& body, EMotion motion, float speedCm);

	/** What a body wears, one bit each: the starter outfit's garments in the order it lists them, then the sword. */
	inline constexpr uint8 wornHat = 1 << 0;
	inline constexpr uint8 wornShirt = 1 << 1;
	inline constexpr uint8 wornPants = 1 << 2;
	inline constexpr uint8 wornGloves = 1 << 3;
	inline constexpr uint8 wornSandals = 1 << 4;
	inline constexpr uint8 wornSword = 1 << 5;
	inline constexpr uint8 wornEverything = 0x3F;

	/** What equipped items show as on the body, by each item's visual in the item data. An item that shows nowhere, or that the data does not list, adds nothing. */
	ALVERION_API uint8 wornPieces(TConstArrayView<int32> itemIds);

	/** Applies what it can and leaves the rest: a missing groom or eye material is logged and skipped, since a bald character beats no character. */
	ALVERION_API void apply(AActor& avatar, const Network::FAppearance& appearance, uint8 worn = wornEverything);

	/**
	 * Dresses the actual body in equipment-owned starter garments, retaining separate cloth simulation and reusing its components. Diagnostic selections hide the other pieces.
	 * Only what worn names shows. The underwear stands in for a shirt or pants that is not worn, and fallback shows the underwear alone.
	 */
	ALVERION_API bool dressStarter(AActor& avatar, const Network::FAppearance& appearance, const FString& pieceOnly = FString(), bool fallback = false, uint8 worn = wornEverything);

	/** How tall a worn body stands against a human of the same frame: its scale, or the dwarf's whole height, of which its scale is only a part. */
	ALVERION_API float heightScale(AActor& body);

	/**
	 * Whether a body already stands in the shape an appearance wants.
	 * A dwarf is its human donor's class in another shape, so a body standing as one of them has to be stood again to be the other.
	 */
	ALVERION_API bool sameShape(AActor& body, const Network::FAppearance& appearance);

	/** Fetches what every character needs, so the first one shown does not stall the game thread. Harmless to call again. */
	ALVERION_API void warm();

	/**
	 * Answers with the character class an appearance wears, at once when it is in memory and later when it had to be fetched.
	 * A character is about two hundred megabytes, and loading one where it is wanted stops the game thread for a second.
	 * The answer is null when the MetaHuman assets are not installed, which a fresh clone is.
	 */
	ALVERION_API void requestBodyClass(const Network::FAppearance& appearance, TFunction<void(UClass*)> whenReady);

	/**
	 * Stands the MetaHuman an appearance describes on a host whose feet are feetOffset from its root, facing the host's forward, and dresses it.
	 * The body has no collision of its own, so it never blocks the host it rides on. The callback gets it once it stands, or null when the MetaHuman assets are missing.
	 * Bodies stand one per frame of the host's world, never in this call, so a crowd arriving together fills in over a few frames instead of freezing one.
	 * An authored body uses its assembled Blueprint and keeps its own materials and grooms.
	 * The host owns it from then on and destroys it with itself, since an attached actor outlives its parent otherwise.
	 */
	ALVERION_API void attachTo(AActor& host, const Network::FAppearance& appearance, const FVector& feetOffset, TFunction<void(AActor*)> whenAttached, const FString& authoredBody = FString(), uint8 worn = wornEverything);
}
