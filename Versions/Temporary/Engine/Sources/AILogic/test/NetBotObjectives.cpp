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
