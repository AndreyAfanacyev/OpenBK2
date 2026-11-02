#include "../stdafx.h"

#include "../Diplomacy.h"
#include "../GeneralBotObjectives.h"

#include <gtest/gtest.h>

using NBotObjectives::AssignTargets;
using NBotObjectives::FindTarget;
using NBotObjectives::SWorker;

namespace
{
std::vector<SWorker> MakeWorkers( const std::vector<CVec2> &positions )
{
	std::vector<SWorker> workers( positions.size() );
	for ( int i = 0; i < positions.size(); ++i )
	{
		workers[i].vPosition = positions[i];
	}
	return workers;
}
} // namespace

TEST( NetBotObjectives, EachUnitGoesToTheNearestPointWithRoomLeft )
{
	const std::vector<CVec2> targets{ CVec2( 0, 0 ), CVec2( 1000, 0 ) };
	std::vector<SWorker> workers = MakeWorkers( { CVec2( 10, 0 ), CVec2( 990, 0 ) } );
	AssignTargets( targets, &workers );
	EXPECT_EQ( workers[0].nTarget, 0 );
	EXPECT_EQ( workers[1].nTarget, 1 );
}

TEST( NetBotObjectives, NoPointIsLeftEmptyWhileAnotherHasMoreThanItsShare )
{
	const std::vector<CVec2> targets{ CVec2( 0, 0 ), CVec2( 1000, 0 ), CVec2( 5000, 0 ) };
	// All of them start next to the first point
	std::vector<SWorker> workers = MakeWorkers( { CVec2( 1, 0 ), CVec2( 2, 0 ), CVec2( 3, 0 ), CVec2( 4, 0 ), CVec2( 5, 0 ), CVec2( 6, 0 ) } );
	AssignTargets( targets, &workers );
	std::vector<int> counts( targets.size(), 0 );
	for ( const SWorker &worker : workers )
	{
		ASSERT_GE( worker.nTarget, 0 );
		++counts[worker.nTarget];
	}
	EXPECT_EQ( counts, ( std::vector<int>{ 2, 2, 2 } ) );
}

TEST( NetBotObjectives, UnitsKeepAValidTargetEvenWhenAnotherIsCloser )
{
	const std::vector<CVec2> targets{ CVec2( 0, 0 ), CVec2( 1000, 0 ) };
	std::vector<SWorker> workers = MakeWorkers( { CVec2( 1, 0 ), CVec2( 1001, 0 ) } );
	workers[0].nTarget = 1;
	workers[1].nTarget = 0;
	AssignTargets( targets, &workers );
	EXPECT_EQ( workers[0].nTarget, 1 );
	EXPECT_EQ( workers[1].nTarget, 0 );
}

TEST( NetBotObjectives, AnOverfilledPointGivesUpItsLastUnits )
{
	const std::vector<CVec2> targets{ CVec2( 0, 0 ), CVec2( 1000, 0 ) };
	std::vector<SWorker> workers = MakeWorkers( { CVec2( 1, 0 ), CVec2( 2, 0 ), CVec2( 3, 0 ), CVec2( 4, 0 ) } );
	for ( SWorker &worker : workers )
	{
		worker.nTarget = 0; // the other point has just been lost
	}
	AssignTargets( targets, &workers );
	EXPECT_EQ( workers[0].nTarget, 0 );
	EXPECT_EQ( workers[1].nTarget, 0 );
	EXPECT_EQ( workers[2].nTarget, 1 );
	EXPECT_EQ( workers[3].nTarget, 1 );
}

TEST( NetBotObjectives, TiesAlwaysGoToTheLowerIndexSoAllClientsAgree )
{
	const std::vector<CVec2> targets{ CVec2( -100, 0 ), CVec2( 100, 0 ) };
	std::vector<SWorker> first = MakeWorkers( { CVec2( 0, 0 ) } );
	AssignTargets( targets, &first );
	EXPECT_EQ( first[0].nTarget, 0 );
}

TEST( NetBotObjectives, ALostTargetIsNotFound )
{
	const std::vector<CVec2> targets{ CVec2( 0, 0 ), CVec2( 1000, 0 ) };
	EXPECT_EQ( FindTarget( targets, CVec2( 1000, 0 ) ), 1 );
	EXPECT_EQ( FindTarget( targets, CVec2( 5, 5 ) ), -1 );
}

TEST( NetBotObjectives, NothingToDoWithoutTargetsOrWorkers )
{
	std::vector<SWorker> workers = MakeWorkers( { CVec2( 1, 0 ) } );
	AssignTargets( {}, &workers );
	EXPECT_EQ( workers[0].nTarget, -1 );
	std::vector<SWorker> none;
	AssignTargets( { CVec2( 0, 0 ) }, &none );
	EXPECT_TRUE( none.empty() );
}

// Every client must get the same answer whichever player sits at its keyboard.
TEST( NetBotDiplomacy, BotsDoNotDependOnTheLocalPlayer )
{
	CDiplomacy dipl;
	dipl.SetNPlayers( 4 );
	dipl.SetNetGame( true );
	dipl.SetPlayerBot( 1, true );
	dipl.SetPlayerBot( 3, true );
	for ( int nLocal = 0; nLocal < 4; ++nLocal )
	{
		dipl.SetMyNumber( nLocal );
		EXPECT_FALSE( dipl.IsAIPlayer( 0 ) );
		EXPECT_TRUE( dipl.IsAIPlayer( 1 ) );
		EXPECT_FALSE( dipl.IsAIPlayer( 2 ) );
		EXPECT_TRUE( dipl.IsAIPlayer( 3 ) );
		EXPECT_FALSE( dipl.IsAIPlayer( 4 ) ) << "the neutral player is not a bot";
	}
}

TEST( NetBotDiplomacy, WithoutBotsANetGameHasNoAIPlayersAndSinglePlayerIsUnchanged )
{
	CDiplomacy dipl;
	dipl.SetNPlayers( 2 );
	dipl.SetNetGame( true );
	EXPECT_FALSE( dipl.IsAIPlayer( 0 ) );
	EXPECT_FALSE( dipl.IsAIPlayer( 1 ) );

	dipl.SetNetGame( false );
	dipl.SetMyNumber( 0 );
	EXPECT_FALSE( dipl.IsAIPlayer( 0 ) );
	EXPECT_TRUE( dipl.IsAIPlayer( 1 ) );
}

TEST( NetBotGarrison, ASmallArmyLeavesOneUnitAndNeverMoreThanAThird )
{
	using NBotObjectives::GarrisonPerPoint;
	EXPECT_EQ( GarrisonPerPoint( 3, 1 ), 1 );
	EXPECT_EQ( GarrisonPerPoint( 7, 1 ), 1 );
	EXPECT_EQ( GarrisonPerPoint( 9, 1 ), 2 );
	EXPECT_EQ( GarrisonPerPoint( 9, 3 ), 1 );
	EXPECT_EQ( GarrisonPerPoint( 2, 1 ), 0 ) << "too few units to spare any";
	EXPECT_EQ( GarrisonPerPoint( 10, 0 ), 0 );
}

TEST( NetBotWaves, AFreePointIsTakenEarlyAndADefendedOneNeedsAGroup )
{
	using NBotObjectives::ShouldLaunchWave;
	EXPECT_TRUE( ShouldLaunchWave( 2, false, false, 0 ) );
	EXPECT_FALSE( ShouldLaunchWave( 1, false, false, 0 ) );
	EXPECT_TRUE( ShouldLaunchWave( 1, false, false, NBotObjectives::WAVE_WAIT_FREE_POINT ) );

	EXPECT_FALSE( ShouldLaunchWave( 5, true, false, 0 ) );
	EXPECT_TRUE( ShouldLaunchWave( 6, true, false, 0 ) );
	EXPECT_FALSE( ShouldLaunchWave( 2, true, false, NBotObjectives::WAVE_WAIT_SHORT ) );
	EXPECT_TRUE( ShouldLaunchWave( 3, true, false, NBotObjectives::WAVE_WAIT_SHORT ) );
	EXPECT_TRUE( ShouldLaunchWave( 1, true, false, NBotObjectives::WAVE_WAIT_LONG ) ) << "must not wait forever";
}

TEST( NetBotWaves, APointUnderAttackCantWaitButNobodyGoesAlone )
{
	EXPECT_TRUE( NBotObjectives::ShouldLaunchWave( 1, true, true, 0 ) );
	EXPECT_FALSE( NBotObjectives::ShouldLaunchWave( 0, true, true, 1000000 ) );
}

TEST( NetBotWaves, ADividedArmySendsSomethingToEachPoint )
{
	EXPECT_EQ( NBotObjectives::WaveSize( 7, 1 ), 7 );
	EXPECT_EQ( NBotObjectives::WaveSize( 7, 2 ), 3 );
	EXPECT_EQ( NBotObjectives::WaveSize( 1, 3 ), 1 );
}

TEST( NetBotRetreat, OnlyAMuchStrongerEnemyTurnsAWaveBack )
{
	EXPECT_FALSE( NBotObjectives::ShouldWaveRetreat( 100.0f, 200.0f ) );
	EXPECT_TRUE( NBotObjectives::ShouldWaveRetreat( 100.0f, 201.0f ) );
	EXPECT_FALSE( NBotObjectives::ShouldWaveRetreat( 0.0f, 500.0f ) );
}

TEST( NetBotForces, EngineersWaitUntilThereIsAnArmy )
{
	NBotObjectives::SForces forces;
	EXPECT_FALSE( NBotObjectives::MayCallEngineers( forces ) ) << "no fighting units at all";
	forces.own[NBotObjectives::FC_ARMOR] = 3.0f;
	EXPECT_FALSE( NBotObjectives::MayCallEngineers( forces ) );
	forces.own[NBotObjectives::FC_INFANTRY] = 1.0f;
	EXPECT_TRUE( NBotObjectives::MayCallEngineers( forces ) );
}

TEST( NetBotForces, TheMixFollowsWhatTheEnemyFields )
{
	using namespace NBotObjectives;
	const bool all[FC_COUNT] = { true, true, true, true, true };

	SForces fresh;
	EXPECT_EQ( ChooseCategory( fresh, all ), FC_ARMOR ) << "the largest share of an empty army";

	SForces againstArmor;
	againstArmor.own[FC_ARMOR] = 4.0f;
	againstArmor.own[FC_INFANTRY] = 3.0f;
	againstArmor.enemyArmor = 10.0f;
	EXPECT_EQ( ChooseCategory( againstArmor, all ), FC_ANTITANK );

	SForces againstAir;
	againstAir.own[FC_ARMOR] = 4.0f;
	againstAir.own[FC_INFANTRY] = 3.0f;
	againstAir.enemyAir = 10.0f;
	EXPECT_EQ( ChooseCategory( againstAir, all ), FC_ANTIAIR );
}

TEST( NetBotForces, OnlyWhatCanBeCalledIsChosen )
{
	using namespace NBotObjectives;
	const bool onlyInfantry[FC_COUNT] = { true, false, false, false, false };
	const bool nothing[FC_COUNT] = {};
	SForces forces;
	EXPECT_EQ( ChooseCategory( forces, onlyInfantry ), FC_INFANTRY );
	EXPECT_EQ( ChooseCategory( forces, nothing ), FC_NOT_COMBAT );
}

TEST( NetBotArtillery, AGunStandsWithinRangeButBehindTheFront )
{
	using namespace NBotObjectives;
	const CVec2 vFocus( 1000, 1000 );
	const CVec2 vPosition = FirePosition( vFocus, CVec2( 0, 1000 ), 800.0f, 0.0f, 0.75f );
	EXPECT_NEAR( vPosition.x, 400.0f, 0.01f ) << "on the near side of the focus";
	EXPECT_NEAR( vPosition.y, 1000.0f, 0.01f );
}

TEST( NetBotArtillery, AGunNeverStandsCloserThanItCanFire )
{
	using namespace NBotObjectives;
	const CVec2 vPosition = FirePosition( CVec2( 1000, 0 ), CVec2( 0, 0 ), 100.0f, 500.0f, 0.75f );
	EXPECT_NEAR( fabs( vPosition - CVec2( 1000, 0 ) ), 600.0f, 0.01f );
	EXPECT_FALSE( IsAtFireDistance( 400.0f, 1000.0f, 500.0f ) );
	EXPECT_TRUE( IsAtFireDistance( 600.0f, 1000.0f, 500.0f ) );
	EXPECT_FALSE( IsAtFireDistance( 990.0f, 1000.0f, 500.0f ) ) << "at the very edge of range";
}

