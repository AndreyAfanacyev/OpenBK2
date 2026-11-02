#include "../stdafx.h"

// A net game bot end to end, on real units and the real general: whatever player
// sits at the local keyboard, the bot takes its own units, leaves its ally's
// alone, and sends them to the key point its side doesn't hold.

#include "../AIClassesID.h"
#include "../Artillery.h"
#include "../B2AI.h"
#include "../Building.h"
#include "../Commands.h"
#include "../DBAIConsts.h"
#include "../Diplomacy.h"
#include "../General.h"
#include "../Guns.h"
#include "../GroupLogic.h"
#include "../KeyBuildingBonusSystem.h"
#include "../LinkObject.h"
#include "../PlayerReinforcement.h"
#include "../Reinforcement.h"
#include "../StaticMembers.h"
#include "../Technics.h"
#include "../UnitGuns.h"
#include "../Units.h"
#include "../UnitStates.h"
#include "Common_RTS_AI/AIMap.h"
#include "Common_RTS_AI/CommonPathFinder.h"
#include "libdb/Db.h"
#include "libdb/EditorDb.h"
#include "libdb/ObjMan.h"
#include "Stats_B2_M1/DBMapInfo.h"
#include "System/VFSOperations.h"
#include "System/WinVFS.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <list>
#include <string>
#include <vector>

#include <gtest/gtest.h>

extern CSupremeBeing theSupremeBeing;
extern CKeyBuildingBonusSystem theBonusSystem;
extern CPlayerReinforcementArray theReinfArray;
extern NTimer::STime curTime;
extern CGroupLogic theGroupLogic;

namespace
{
const int KEY_POINT_LINK = 7;
const int KEY_POINT_UNIQUE_ID = 9;
const CVec2 KEY_POINT( 1600, 1600 );

enum
{
	PLAYER_ENEMY = 0,
	PLAYER_BOT = 1,
	PLAYER_ALLY = 2,
	PLAYER_SECOND_BOT = 3,
	N_PLAYERS = 4
};

struct SFields
{
	std::function<void( IBinSaver & )> visit;

	int operator&( IBinSaver &saver )
	{
		visit( saver );
		return 0;
	}
};

class NetBotGeneral : public testing::TestWithParam<int>
{
	static inline std::filesystem::path directory;
	std::vector<CObj<NDb::IObjMan>> records;
	std::vector<CObj<CObjectBase>> objects;
	std::vector<SBinSaverExternalObject> external;
	CObj<CAIMap> map;
	CObj<NDb::SUnitStatsModifier> modifier;
	CDBPtr<NDb::SMechUnitRPGStats> stats;
	NDb::SMapInfo *pMapInfo = nullptr;

protected:
	static void SetUpTestSuite()
	{
		ASSERT_EQ( CastToObjectBaseImpl<CTank>( nullptr, static_cast<void *>( nullptr ) ), nullptr );
		const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
		directory = std::filesystem::temp_directory_path() / ( "obk2-net-bot-" + std::to_string( stamp ) );
		ASSERT_TRUE( std::filesystem::create_directory( directory ) );
		std::filesystem::copy_file( NET_BOT_TYPES_FILE, directory / "types.xml" );
		const std::string path = directory.generic_string() + "/";
		NVFS::SetMainVFS( NVFS::CreateWinVFS( path ) );
		NVFS::SetMainFileCreator( NVFS::CreateWinFileCreator( path ) );
		ASSERT_TRUE( NDb::OpenDatabase( NVFS::GetMainVFS(), NVFS::GetMainFileCreator(), NDb::DATABASE_MODE_EDITOR ) );
	}

	static void TearDownTestSuite()
	{
		NDb::CloseDatabase();
		NVFS::SetMainVFS( nullptr );
		NVFS::SetMainFileCreator( nullptr );
		std::error_code error;
		std::filesystem::remove_all( directory, error );
	}

	template <class T>
	T *MakeRecord( const char *type )
	{
		CObj<NDb::IObjMan> record = NDb::CreateNewObject( type );
		static unsigned nextRecord = 0;
		const std::string name = std::string( type ) + std::to_string( nextRecord++ ) + "netbot.xdb";
		EXPECT_TRUE( NDb::AddNewObject( name, CDBID( name ), record ) );
		records.push_back( record );
		return dynamic_cast<T *>( record->GetObject() );
	}

	template <class T>
	T *MakeObject( int type )
	{
		T *object = dynamic_cast<T *>( NObjectFactory::MakeObject( type ) );
		EXPECT_NE( object, nullptr );
		if ( object )
		{
			objects.push_back( object );
			external.emplace_back( int( external.size() + 1 ), object );
		}
		return object;
	}

	void LoadFields( CObjectBase *object, const std::function<void( IBinSaver & )> &visit )
	{
		CMemoryStream stream;
		{
			CPtr<IBinSaver> saver = CreateBinSaver( &stream, SAVER_MODE_WRITE, external );
			SFields fields{ visit };
			saver->Add( 1, &fields );
		}
		stream.Seek( 0 );
		CPtr<IBinSaver> loader = CreateBinSaver( &stream, SAVER_MODE_READ, external );
		loader->AddPolymorphicBase( 1, object );
	}

	// nUniqueID is kept low: the shared command queues start with room for ten units,
	// and only the real unit creation code grows them.
	CAIUnit *MakeTank( uint8_t player, const int nUniqueID, const CVec2 &vAt = CVec2( 160, 160 ), float fHP = 100.0f )
	{
		return MakeVehicle( AI_TANK, stats, player, nUniqueID, vAt, fHP );
	}

	CAIUnit *MakeEngineerTruck( uint8_t player, const int nUniqueID )
	{
		auto *truckStats = MakeRecord<NDb::SMechUnitRPGStats>( "MechUnitRPGStats" );
		truckStats->etype = NDb::RPG_TYPE_TRN_SUPPORT;
		truckStats->fSight = 1;
		truckStats->fSpeed = 10;
		truckStats->fRotateSpeed = 1;
		truckStats->fMaxHP = 100;
		truckStats->fPrice = 100;
		truckStats->vAABBHalfSize = CVec2( 4, 8 );
		auto *actions = MakeRecord<NDb::SUnitActions>( "UnitActions" );
		actions->availCommands.SetData( ACTION_COMMAND_REPEAR_OBJECT );
		truckStats->pActions = actions;
		return MakeVehicle( AI_TRANSPORT_UNIT, truckStats, player, nUniqueID );
	}

	CAIUnit *MakeVehicle( const int nType,
	                      CDBPtr<NDb::SMechUnitRPGStats> unitStats,
	                      uint8_t player,
	                      const int nUniqueID,
	                      const CVec2 &vAt = CVec2( 160, 160 ),
	                      float hp = 100.0f )
	{
		CAIUnit *unit = MakeObject<CAIUnit>( nType );
		CVec3 position( vAt.x, vAt.y, 0 );
		SVector tile( int( position.x / SConsts::TILE_SIZE ), int( position.y / SConsts::TILE_SIZE ) );
		SAIAngle direction( 0 );
		bool idle = true;
		CPtr<NDb::SUnitStatsModifier> modifierPointer = modifier.GetPtr();
		std::vector<int> visibility{ 1, 1, 0 };
		CPtr<CUnitGuns> unitGuns = MakeObject<CUnitGuns>( 0x1108D4C7 );
		CPtr<IUnitState> rest = MakeObject<IUnitState>( 0x1508D494 );
		CPtr<CAIUnit> restOwner = unit;
		LoadFields( rest, [&]( IBinSaver &saver ) { saver.Add( 2, &restOwner ); } );
		SFields path{ [&]( IBinSaver &saver )
		              {
										saver.Add( 3, &direction );
										saver.Add( 4, &direction );
										saver.Add( 9, &position );
										saver.Add( 10, &tile );
										saver.Add( 16, &idle );
									} };
		SFields queue{ [&]( IBinSaver &saver ) { saver.Add( 2, &rest ); } };
		SFields common{ [&]( IBinSaver &saver )
		                {
											saver.Add( 3, &queue );
											saver.Add( 20, &path );
										} };
		SFields ai{ [&]( IBinSaver &saver )
		            {
									saver.Add( 1, &common );
									saver.Add( 6, &player );
									saver.Add( 15, &hp );
									saver.Add( 51, &visibility );
									saver.Add( 58, &modifierPointer );
								} };
		SFields mechanical{ [&]( IBinSaver &saver )
		                    {
													saver.Add( 1, &ai );
													saver.Add( 2, &unitStats );
													saver.Add( 3, &unitGuns );
												} };
		LoadFields( unit, [&]( IBinSaver &saver ) { saver.Add( 1, &mechanical ); } );
		unit->nUniqueID = nUniqueID;
		unit->Mem2UniqueIdObjs();
		return unit;
	}

	// A real howitzer with a real gun, standing installed and ready
	CAIUnit *MakeHowitzer( uint8_t player, const int nUniqueID, const CVec2 &vAt, const float fRange )
	{
		auto *weapon = MakeRecord<NDb::SWeaponRPGStats>( "WeaponRPGStats" );
		weapon->fRangeMax = fRange;
		weapon->nAmmoPerBurst = 1;
		weapon->shells.resize( 1 );
		weapon->shells[0].etrajectory = NDb::SWeaponRPGStats::SShell::TRAJECTORY_HOWITZER;
		auto *gunStats = MakeRecord<NDb::SMechUnitRPGStats>( "MechUnitRPGStats" );
		gunStats->etype = NDb::RPG_TYPE_ART_HOWITZER;
		gunStats->fSight = 1;
		gunStats->fMaxHP = 100;
		gunStats->fPrice = 100;
		gunStats->vAABBHalfSize = CVec2( 4, 8 );
		auto *actions = MakeRecord<NDb::SUnitActions>( "UnitActions" );
		actions->availCommands.SetData( ACTION_COMMAND_ART_BOMBARDMENT );
		actions->availCommands.SetData( ACTION_COMMAND_ROTATE_TO );
		actions->availCommands.SetData( ACTION_COMMAND_STOP );
		gunStats->pActions = actions;
		gunStats->platforms.resize( 1 );
		gunStats->platforms[0].guns.resize( 1 );
		gunStats->platforms[0].guns[0].pWeapon = weapon;
		CDBPtr<NDb::SMechUnitRPGStats> unitStats = gunStats;

		CAIUnit *unit = MakeObject<CAIUnit>( AI_ARTILLERY );
		CPtr<CBasicGun> gun = MakeObject<CBasicGun>( 0x1108D4B5 );
		CPtr<SCommonGunInfo> ammo = MakeObject<SCommonGunInfo>( 0x1108D4C9 );
		ammo->nAmmo = 100;
		CPtr<CUnitGuns> unitGuns = MakeObject<CUnitGuns>( 0x1108D4C7 );
		CPtr<CAIUnit> owner = unit;
		CDBPtr<NDb::SWeaponRPGStats> weaponPointer = weapon;
		int gunType = IGunsFactory::VIS_CML_BALLIST_GUN;
		SFields gunFields{ [&]( IBinSaver &saver ) { saver.Add( 2, &weaponPointer ); } };
		SFields basicGun{ [&]( IBinSaver &saver )
		                  {
												saver.Add( 1, &gunFields );
												saver.Add( 7, &gunType );
												saver.Add( 13, &owner );
												saver.Add( 15, &ammo );
											} };
		LoadFields( gun, [&]( IBinSaver &saver ) { saver.Add( 1, &basicGun ); } );
		std::vector<CObj<CBasicGun>> gunArray{ gun.GetPtr() };
		std::vector<CPtr<SCommonGunInfo>> commonGuns{ ammo };
		std::vector<int> beginnings{ 0 };
		int count = 1;
		SFields gunCollection{ [&]( IBinSaver &saver )
		                       {
														 saver.Add( 2, &weapon->fRangeMax );
														 saver.Add( 4, &commonGuns );
														 saver.Add( 5, &gunArray );
														 saver.Add( 6, &beginnings );
														 saver.Add( 7, &count );
													 } };
		int nFirstArtilleryGun = 0;
		LoadFields( unitGuns,
		            [&]( IBinSaver &saver )
		            {
									saver.Add( 1, &gunCollection );
									saver.Add( 2, &nFirstArtilleryGun );
								} );

		CVec3 position( vAt.x, vAt.y, 0 );
		SVector tile( int( position.x / SConsts::TILE_SIZE ), int( position.y / SConsts::TILE_SIZE ) );
		SAIAngle direction( 0 );
		bool idle = true;
		float hp = 100;
		CPtr<NDb::SUnitStatsModifier> modifierPointer = modifier.GetPtr();
		std::vector<int> visibility{ 1, 1, 0 };
		CPtr<IUnitState> rest = MakeObject<IUnitState>( 0x1508D494 );
		CPtr<CAIUnit> restOwner = unit;
		LoadFields( rest, [&]( IBinSaver &saver ) { saver.Add( 2, &restOwner ); } );
		SFields path{ [&]( IBinSaver &saver )
		              {
										saver.Add( 3, &direction );
										saver.Add( 4, &direction );
										saver.Add( 9, &position );
										saver.Add( 10, &tile );
										saver.Add( 16, &idle );
									} };
		SFields queue{ [&]( IBinSaver &saver ) { saver.Add( 2, &rest ); } };
		SFields common{ [&]( IBinSaver &saver )
		                {
											saver.Add( 3, &queue );
											saver.Add( 20, &path );
										} };
		SFields ai{ [&]( IBinSaver &saver )
		            {
									saver.Add( 1, &common );
									saver.Add( 6, &player );
									saver.Add( 15, &hp );
									saver.Add( 51, &visibility );
									saver.Add( 58, &modifierPointer );
								} };
		bool installed = true;
		float operable = 1;
		EActionNotify installAction = ACTION_NOTIFY_NONE;
		LoadFields( unit,
		            [&]( IBinSaver &saver )
		            {
									saver.Add( 1, &ai );
									saver.Add( 2, &unitStats );
									saver.Add( 4, &unitGuns );
									saver.Add( 6, &installAction );
									saver.Add( 7, &installAction );
									saver.Add( 8, &installAction );
									saver.Add( 9, &installed );
									saver.Add( 17, &operable );
								} );
		unit->nUniqueID = nUniqueID;
		unit->Mem2UniqueIdObjs();
		return unit;
	}

	CAIUnit *MakeTractor( uint8_t player, const int nUniqueID, const CVec2 &vAt )
	{
		auto *tractorStats = MakeRecord<NDb::SMechUnitRPGStats>( "MechUnitRPGStats" );
		tractorStats->etype = NDb::RPG_TYPE_TRN_TRACTOR;
		tractorStats->fSight = 1;
		tractorStats->fSpeed = 10;
		tractorStats->fRotateSpeed = 1;
		tractorStats->fMaxHP = 100;
		tractorStats->fPrice = 100;
		tractorStats->vAABBHalfSize = CVec2( 4, 8 );
		auto *actions = MakeRecord<NDb::SUnitActions>( "UnitActions" );
		actions->availCommands.SetData( ACTION_COMMAND_TAKE_ARTILLERY );
		actions->availCommands.SetData( ACTION_COMMAND_DEPLOY_ARTILLERY );
		tractorStats->pActions = actions;
		return MakeVehicle( AI_TRANSPORT_UNIT, tractorStats, player, nUniqueID, vAt );
	}

	void PlaceKeyBuilding( int nOwner, float fHP = 100.0f )
	{
		CBuildingSimple *building = MakeObject<CBuildingSimple>( 0x1108D450 );
		CVec3 center( KEY_POINT.x, KEY_POINT.y, 0 );
		auto *buildingRecord = MakeRecord<NDb::SBuildingRPGStats>( "BuildingRPGStats" );
		ASSERT_NE( buildingRecord, nullptr );
		buildingRecord->fMaxHP = 100.0f;
		CDBPtr<NDb::SBuildingRPGStats> buildingStats = buildingRecord;
		SFields existing{ [&]( IBinSaver &saver ) { saver.Add( 6, &fHP ); } };
		SFields passability{ [&]( IBinSaver &saver )
		                     {
													 saver.Add( 1, &existing );
													 saver.Add( 2, &center );
												 } };
		int nLinkID = KEY_POINT_LINK;
		SFields base{ [&]( IBinSaver &saver )
		              {
										saver.Add( 1, &passability );
										saver.Add( 28, &nLinkID );
										saver.Add( 29, &buildingStats );
									} };
		LoadFields( building,
		            [&]( IBinSaver &saver )
		            {
									saver.Add( 1, &base );
									saver.Add( 2, &nOwner );
								} );
		building->SetLink( KEY_POINT_LINK );
		building->nUniqueID = KEY_POINT_UNIQUE_ID;

		NDb::SPlayerBonusData bonus;
		bonus.nLinkID = KEY_POINT_LINK;
		pMapInfo->playerBonusObjects.push_back( bonus );
		theBonusSystem.InitBonusSystem( pMapInfo );
	}

	void SetUp() override
	{
		NSingleton::RegisterSingleton( MakeObject<IAILogic>( 0x1108D441 ), IAILogic::tidTypeID );
		NSingleton::RegisterSingleton( new CCommonPathFinder(), CCommonPathFinder::tidTypeID );
		map = new CAIMap( 256, 256, 32, 2, 16 );
		SetAIMap( map );
		units.Init();
		theGroupLogic.Init( nullptr );
		theDipl.SetNPlayers( N_PLAYERS );
		theDipl.SetNetGame( true );
		theDipl.SetParty( PLAYER_ENEMY, 0 );
		theDipl.SetParty( PLAYER_BOT, 1 );
		theDipl.SetParty( PLAYER_ALLY, 1 );
		theDipl.SetParty( PLAYER_SECOND_BOT, 1 );
		theDipl.SetPlayerBot( PLAYER_BOT, true );
		theDipl.SetPlayerBot( PLAYER_SECOND_BOT, true );
		theDipl.SetMyNumber( GetParam() );
		theReinfArray.resize( N_PLAYERS + 1 );
		curTime = 100000;
		SConsts::GENERAL_CELL_SIZE = 256; // normally set when the game consts load

		modifier = new NDb::SUnitStatsModifier;
		external.emplace_back( 1000, modifier );
		auto *unitStats = MakeRecord<NDb::SMechUnitRPGStats>( "MechUnitRPGStats" );
		ASSERT_NE( unitStats, nullptr );
		unitStats->etype = NDb::RPG_TYPE_ARM_MEDIUM;
		unitStats->fSight = 1;
		unitStats->fSpeed = 10;
		unitStats->fRotateSpeed = 1;
		unitStats->fMaxHP = 100;
		unitStats->fPrice = 100;
		unitStats->vAABBHalfSize = CVec2( 4, 8 );
		auto *actions = MakeRecord<NDb::SUnitActions>( "UnitActions" );
		actions->availCommands.SetData( ACTION_COMMAND_SWARM_TO );
		actions->availCommands.SetData( ACTION_COMMAND_MOVE_TO );
		actions->availCommands.SetData( ACTION_COMMAND_ENTRENCH_SELF );
		unitStats->pActions = actions;
		stats = unitStats;

		// The general weighs every unit it receives against these tables
		auto *consts = MakeRecord<NDb::SAIGameConsts>( "AIGameConsts" );
		ASSERT_NE( consts, nullptr );
		consts->reinfExpediency.resize( NDb::_RT_NONE );
		NReinforcement::InitReinforcementTypes( consts );

		pMapInfo = MakeRecord<NDb::SMapInfo>( "MapInfo" );
		ASSERT_NE( pMapInfo, nullptr );
		pMapInfo->players.resize( N_PLAYERS );
	}

	void TearDown() override
	{
		theSupremeBeing.Clear();
		theGroupLogic.Clear();
		theBonusSystem.Clear();
		theReinfArray.clear();
		units.Clear();
		CLinkObject::Clear();
		theDipl.SetPlayerBot( PLAYER_BOT, false );
		theDipl.SetPlayerBot( PLAYER_SECOND_BOT, false );
		theDipl.SetNetGame( false );
		NSingleton::UnRegisterSingleton( IAILogic::tidTypeID );
		objects.clear();
		SetAIMap( nullptr );
		NSingleton::UnRegisterSingleton( CCommonPathFinder::tidTypeID );
	}

	void StartGenerals( const std::list<CCommonUnit *> &startUnits )
	{
		theSupremeBeing.Init( pMapInfo );
		theSupremeBeing.GiveNewUnitsToGenerals( startUnits );
	}

	void RunGenerals( const int nTicks = 8 )
	{
		for ( int i = 0; i < nTicks; ++i )
		{
			curTime += 20000;
			theSupremeBeing.Segment();
		}
	}

	static bool IsOrderedToRepairKeyPoint( CCommonUnit *pUnit )
	{
		CAICommand *pCommand = pUnit->GetCurCmd() ? pUnit->GetCurCmd() : pUnit->GetNextCommand();
		return pCommand && pCommand->ToUnitCmd().nCmdType == ACTION_COMMAND_REPEAR_OBJECT &&
		       pCommand->ToUnitCmd().nObjectID == KEY_POINT_UNIQUE_ID;
	}

	static bool IsOrderedToKeyPoint( CCommonUnit *pUnit )
	{
		const CAICommand *pCommand = pUnit->GetCurCmd() ? pUnit->GetCurCmd() : pUnit->GetNextCommand();
		return pCommand && const_cast<CAICommand *>( pCommand )->ToUnitCmd().nCmdType == ACTION_COMMAND_SWARM_TO &&
		       fabs( const_cast<CAICommand *>( pCommand )->ToUnitCmd().vPos - KEY_POINT ) < 1.0f;
	}
};

TEST_P( NetBotGeneral, OnlyBotsGetGenerals )
{
	StartGenerals( {} );
	EXPECT_FALSE( theSupremeBeing.HasGeneralForPlayer( PLAYER_ENEMY ) );
	EXPECT_TRUE( theSupremeBeing.HasGeneralForPlayer( PLAYER_BOT ) );
	EXPECT_FALSE( theSupremeBeing.HasGeneralForPlayer( PLAYER_ALLY ) );
	EXPECT_TRUE( theSupremeBeing.HasGeneralForPlayer( PLAYER_SECOND_BOT ) );
	EXPECT_FALSE( theSupremeBeing.HasGeneralInParty( 0 ) );
	EXPECT_TRUE( theSupremeBeing.HasGeneralInParty( 1 ) );
}

TEST_P( NetBotGeneral, BotSendsItsOwnUnitsToTheEnemyKeyPointAndLeavesItsAllysAlone )
{
	CAIUnit *botTank = MakeTank( PLAYER_BOT, 1 );
	CAIUnit *secondBotTank = MakeTank( PLAYER_SECOND_BOT, 2 );
	CAIUnit *allyTank = MakeTank( PLAYER_ALLY, 3 );
	CAIUnit *enemyTank = MakeTank( PLAYER_ENEMY, 4 );
	PlaceKeyBuilding( PLAYER_ENEMY );

	StartGenerals( { botTank, secondBotTank, allyTank, enemyTank } );
	RunGenerals();

	EXPECT_TRUE( IsOrderedToKeyPoint( botTank ) );
	EXPECT_TRUE( IsOrderedToKeyPoint( secondBotTank ) ) << "bots on one side must not take each other's units";
	EXPECT_FALSE( IsOrderedToKeyPoint( allyTank ) );
	EXPECT_FALSE( IsOrderedToKeyPoint( enemyTank ) );
}

// A neutral key point under 90% health can't be captured until engineers repair it.
TEST_P( NetBotGeneral, EngineersRepairADamagedNeutralKeyPoint )
{
	CAIUnit *truck = MakeEngineerTruck( PLAYER_BOT, 6 );
	CAIUnit *allyTruck = MakeEngineerTruck( PLAYER_ALLY, 7 );
	PlaceKeyBuilding( N_PLAYERS, 50.0f ); // N_PLAYERS is the neutral player

	StartGenerals( { truck, allyTruck } );
	RunGenerals();

	EXPECT_TRUE( IsOrderedToRepairKeyPoint( truck ) );
	EXPECT_FALSE( IsOrderedToRepairKeyPoint( allyTruck ) );
}

TEST_P( NetBotGeneral, AnUndamagedKeyPointNeedsNoEngineers )
{
	CAIUnit *truck = MakeEngineerTruck( PLAYER_BOT, 8 );
	PlaceKeyBuilding( N_PLAYERS, 100.0f );

	StartGenerals( { truck } );
	RunGenerals();

	EXPECT_FALSE( IsOrderedToRepairKeyPoint( truck ) );
}

TEST_P( NetBotGeneral, ATractorTakesAGunOutOfRangeToTheFront )
{
	CAIUnit *gun = MakeHowitzer( PLAYER_BOT, 1, CVec2( 160, 160 ), 600.0f );
	CAIUnit *tractor = MakeTractor( PLAYER_BOT, 2, CVec2( 200, 160 ) );
	PlaceKeyBuilding( PLAYER_ENEMY );
	StartGenerals( { gun, tractor } );

	RunGenerals( 1 );

	CAICommand *pCommand = tractor->GetNextCommand();
	ASSERT_NE( pCommand, nullptr );
	EXPECT_EQ( pCommand->ToUnitCmd().nCmdType, ACTION_COMMAND_TAKE_ARTILLERY );
	EXPECT_EQ( pCommand->ToUnitCmd().nObjectID, 1 );
}

TEST_P( NetBotGeneral, AGunThatReachesAnEnemyPointFiresNearItWithoutWreckingIt )
{
	// In range but not so close that it would need to move
	CAIUnit *gun = MakeHowitzer( PLAYER_BOT, 1, KEY_POINT - CVec2( 500.0f, 0.0f ), 600.0f );
	PlaceKeyBuilding( PLAYER_ENEMY );
	StartGenerals( { gun } );

	RunGenerals( 2 ); // aims, then fires

	CAICommand *pCommand = gun->GetNextCommand();
	ASSERT_NE( pCommand, nullptr );
	ASSERT_EQ( pCommand->ToUnitCmd().nCmdType, ACTION_COMMAND_ART_BOMBARDMENT );
	const float fFromPoint = fabs( pCommand->ToUnitCmd().vPos - KEY_POINT );
	EXPECT_GT( fFromPoint, float( SConsts::TILE_SIZE ) );
	EXPECT_LT( fFromPoint, 600.0f );
}

TEST_P( NetBotGeneral, ALoneUnitWaitsForOthersBeforeAttackingADefendedPoint )
{
	CAIUnit *tank = MakeTank( PLAYER_BOT, 1 );
	PlaceKeyBuilding( PLAYER_ENEMY );
	StartGenerals( { tank } );

	RunGenerals( 3 ); // a minute
	EXPECT_FALSE( IsOrderedToKeyPoint( tank ) );

	RunGenerals( 5 );
	EXPECT_TRUE( IsOrderedToKeyPoint( tank ) ) << "but not forever";
}

TEST_P( NetBotGeneral, AFreePointIsTakenWithoutWaiting )
{
	CAIUnit *first = MakeTank( PLAYER_BOT, 1 );
	CAIUnit *second = MakeTank( PLAYER_BOT, 2 );
	PlaceKeyBuilding( N_PLAYERS );
	StartGenerals( { first, second } );

	RunGenerals( 1 );

	EXPECT_TRUE( IsOrderedToKeyPoint( first ) );
	EXPECT_TRUE( IsOrderedToKeyPoint( second ) );
}

TEST_P( NetBotGeneral, ABadlyDamagedUnitLeavesForTheRallyPoint )
{
	CAIUnit *damaged = MakeTank( PLAYER_BOT, 1, CVec2( 160, 160 ), 20.0f );
	CAIUnit *healthy = MakeTank( PLAYER_BOT, 2 );
	PlaceKeyBuilding( PLAYER_BOT ); // the rally point is this point of ours, far from the tanks
	StartGenerals( { damaged, healthy } );

	RunGenerals( 1 );

	const CAICommand *pCommand = damaged->GetNextCommand();
	ASSERT_NE( pCommand, nullptr );
	EXPECT_EQ( const_cast<CAICommand *>( pCommand )->ToUnitCmd().nCmdType, ACTION_COMMAND_MOVE_TO );
	EXPECT_LT( fabs( const_cast<CAICommand *>( pCommand )->ToUnitCmd().vPos - KEY_POINT ), 1.0f );
}

TEST_P( NetBotGeneral, UnitsAtAPointOfOursLeaveAGarrisonThatDigsIn )
{
	std::list<CCommonUnit *> tanks;
	std::vector<CAIUnit *> list;
	for ( int i = 1; i <= 6; ++i )
	{
		list.push_back( MakeTank( PLAYER_BOT, i, KEY_POINT + CVec2( float( i ), 0.0f ) ) );
		tanks.push_back( list.back() );
	}
	PlaceKeyBuilding( PLAYER_BOT );
	StartGenerals( tanks );

	RunGenerals( 2 );

	int nDugIn = 0;
	for ( CAIUnit *tank : list )
	{
		const CAICommand *pCommand = tank->GetNextCommand();
		nDugIn += pCommand && const_cast<CAICommand *>( pCommand )->ToUnitCmd().nCmdType == ACTION_COMMAND_ENTRENCH_SELF;
	}
	EXPECT_EQ( nDugIn, 1 ) << "six units can spare one";
}

TEST_P( NetBotGeneral, NoGeneralsAndNoOrdersInANetGameWithoutBots )
{
	theDipl.SetPlayerBot( PLAYER_BOT, false );
	theDipl.SetPlayerBot( PLAYER_SECOND_BOT, false );
	CAIUnit *tank = MakeTank( PLAYER_BOT, 5 );
	PlaceKeyBuilding( PLAYER_ENEMY );

	StartGenerals( { tank } );
	RunGenerals();

	EXPECT_FALSE( theSupremeBeing.HasGeneralInParty( 1 ) );
	EXPECT_FALSE( IsOrderedToKeyPoint( tank ) );
}

// The local player differs between clients, and the result must not.
INSTANTIATE_TEST_SUITE_P( EveryLocalPlayer, NetBotGeneral, testing::Range( 0, int( N_PLAYERS ) ) );
} // namespace
