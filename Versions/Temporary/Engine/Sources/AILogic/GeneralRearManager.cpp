#include "stdafx.h"

#include "Diplomacy.h"
#include "Formation.h"
#include "GeneralAirForce.h"
#include "GeneralArtillery.h"
#include "GeneralBotObjectives.h"
#include "GeneralConsts.h"
#include "GeneralInternal.h"
#include "PlayerReinforcement.h"
#include "Reinforcement.h"
#include "Soldier.h"
#include "UnitsIterators.h"
#include "UnitsIterators2.h"

#include <fmt/format.h>

//
// Contains CGeneral functionality related to calling in reinforcements
//

#define AIR_REINF_TURN_LIMIT 2

extern NTimer::STime curTime;
extern CPlayerReinforcementArray theReinfArray;

void CGeneral::InitRearManager()
{
	enemyByRType.resize( NDb::_RT_NONE, 0.0f );
}

static const bool IsAviationReinfType( const int /*NDb::EReinforcementType*/ eRType )
{
	return ( eRType == NDb::RT_BOMBERS || eRType == NDb::RT_GROUND_ATTACK_PLANES || eRType == NDb::RT_RECON ||
	         eRType == NDb::RT_FIGHTERS || eRType == NDb::RT_EXTRA_AIR_1 || eRType == NDb::RT_EXTRA_AIR_2 ||
	         eRType == NDb::RT_EXTRA_AIR_3 || eRType == NDb::RT_EXTRA_AIR_4 || eRType == NDb::RT_EXTRA_AIR_5 ||
	         eRType == NDb::RT_EXTRA_MAXLVL_AIR_1 || eRType == NDb::RT_EXTRA_MAXLVL_AIR_2 ||
	         eRType == NDb::RT_EXTRA_MAXLVL_AIR_3 || eRType == NDb::RT_EXTRA_MAXLVL_AIR_4 ||
	         eRType == NDb::RT_EXTRA_MAXLVL_AIR_5 || eRType == NDb::RT_EXTRA_MAXLVL_MIXED_1 ||
	         eRType == NDb::RT_EXTRA_MAXLVL_MIXED_2 || eRType == NDb::RT_EXTRA_MAXLVL_MIXED_3 ||
	         eRType == NDb::RT_EXTRA_MAXLVL_MIXED_4 || eRType == NDb::RT_EXTRA_MAXLVL_MIXED_5 );
}

void CGeneral::CheckAvailableReinforcement()
{
	int nReinfTypeNeeded, nReinfTypeToCall = -2;
	float fEffect, fMaxEffect = enemyByRType[0];
	bool bReinfFound = false;

	// there is no ground reinforcements
	if ( !theReinfArray[nPlayer].HasGroundReinforcements() )
	{
		pAirForce->PassTurn();
		return;
	}

	if ( !theReinfArray[nPlayer].CanCallNow() )
	{
		return;
	}

	// A bot that needs a key point repaired calls engineers before anything else. The cooldown
	// gives the truck time to arrive, so that the calls aren't all spent on the same repair.
	const bool bNetGame = theDipl.IsNetGame();
	NBotObjectives::SForces forces;
	if ( bNetGame )
	{
		CountForces( &forces );
	}
	if ( bEngineersNeeded && curTime >= timeNextEngineersCall && NBotObjectives::MayCallEngineers( forces ) &&
	     theReinfArray[nPlayer].HasReinforcement( NDb::RT_ENGINEERING ) )
	{
		theReinfArray[nPlayer].CallReinforcement( NDb::RT_ENGINEERING, theReinfArray[nPlayer].GetRandomPoint(), -1 );
		timeNextEngineersCall = curTime + SGeneralConsts::BOT_ENGINEERS_CALL_COOLDOWN;
		return;
	}

	if ( !pAirForce->TurnReturned() )
	{
		return;
	}

	// Pass some of the calls to airforce general
	++nAirReinfTurnCounter;
	nAirReinfTurnCounter %= AIR_REINF_TURN_LIMIT;
	if ( !nAirReinfTurnCounter )
	{
		pAirForce->PassTurn();
		//CONSOLE_BUFFER_LOG( CONSOLE_STREAM_DEBUG_WINDOW, StrFmt( "Passing turn to airforce" ) );
		return;
	}

	if ( bNetGame )
	{
		CallBotReinforcement( forces );
		return;
	}

	// Select the most needed category (max value)
	nReinfTypeNeeded = 0;
	for ( int i = 0; i < NDb::_RT_NONE; ++i )
	{
		if ( enemyByRType[i] > fMaxEffect && !IsAviationReinfType( i ) )
		{
			fMaxEffect = enemyByRType[i];
			nReinfTypeNeeded = i;
		}
	}

	// if time has come, select one and call
	fMaxEffect = -2.0f; //Since effectiveness is [-1 .. 1]
	int nAvailReinfs[NDb::_RT_NONE];
	float fReinfEff[NDb::_RT_NONE];
	float fSum = 0.0f;
	int nARQuantity = 0;
	for ( int i = 0; i < NDb::_RT_NONE; ++i )
	{
		if ( !theReinfArray[nPlayer].HasReinforcement( NDb::EReinforcementType( i ) ) || IsAviationReinfType( i ) )
		{
			continue;
		}

		// calculate potential effect
		fEffect = 0;
		for ( int j = 0; j < NDb::_RT_NONE; ++j )
		{
			fEffect +=
					NReinforcement::GetReinforcementExpediency( NDb::EReinforcementType( i ), NDb::EReinforcementType( j ) ) /
					( 1 + enemyByRType[nReinfTypeNeeded] - enemyByRType[j] )   // Weigh according to how close it is to
					/ ( 1 + enemyByRType[nReinfTypeNeeded] - enemyByRType[j] ) // the most needed (squared)
					* ( ( j == nReinfTypeNeeded ) ? 2 : 1 );                   // and added multiplier for the exact hit
		}
		if ( fEffect > fMaxEffect )
		{
			nReinfTypeToCall = i;
			fMaxEffect = fEffect;
			bReinfFound = true;
		}
		fSum += fEffect;
		nAvailReinfs[nARQuantity] = i;
		fReinfEff[nARQuantity] = fSum;
		++nARQuantity;
	}
	if ( bReinfFound ) // if there was a reinforcement available, call it
	{
		//theReinfArray[nPlayer].CallReinforcement( NDb::EReinforcementType( nReinfTypeToCall ),	theReinfArray[nPlayer].GetRandomPoint(), -1 );
		//CONSOLE_BUFFER_LOG( CONSOLE_STREAM_DEBUG_WINDOW, StrFmt( "Calling reinforcement %d", nReinfTypeToCall ) );
		float fRandValue = NRandom::Random( 0.0f, fSum );
		RecordRandomCall();
		const int i = NReinforcement::SelectByCumulativeWeight( fReinfEff, nARQuantity, fRandValue );
		// check if this reinforcement will increase mobile tanks nuber over the maximum allowed number
		// don't call if it will
		int nWorkers = theReinfArray[nPlayer].GetNEntries( NDb::EReinforcementType( nAvailReinfs[i] ) );
		for ( Tasks::iterator it = tasks.begin(); it != tasks.end(); ++it )
		{
			nWorkers += ( *it )->GetWorkerCount();
		}
		nWorkers += tanksFree.size();

		if ( nWorkers <= nMaxAllowedMobileTanks )
		{
			theReinfArray[nPlayer].CallReinforcement(
					NDb::EReinforcementType( nAvailReinfs[i] ), theReinfArray[nPlayer].GetRandomPoint(), -1 );
		}
	}
}

static int GetBotCategory( const int /*NDb::EReinforcementType*/ eType )
{
	switch ( eType )
	{
		case NDb::RT_MAIN_INFANTRY:
		case NDb::RT_ASSAULT_INFANTRY:
		case NDb::RT_ELITE_INFANTRY:
		case NDb::RT_PARATROOPS:
			return NBotObjectives::FC_INFANTRY;
		case NDb::RT_ASSAULT_GUNS:
		case NDb::RT_LIGHT_TANKS:
		case NDb::RT_TANKS:
		case NDb::RT_HEAVY_TANKS:
			return NBotObjectives::FC_ARMOR;
		case NDb::RT_ARTILLERY_ANTITANK:
		case NDb::RT_TANK_DESTROYERS:
			return NBotObjectives::FC_ANTITANK;
		case NDb::RT_ARTILLERY:
		case NDb::RT_ARTILLERY_ROCKET:
			return NBotObjectives::FC_ARTILLERY;
		case NDb::RT_LIGHT_AAA:
		case NDb::RT_HEAVY_AAA:
			return NBotObjectives::FC_ANTIAIR;
		default:
			return NBotObjectives::FC_NOT_COMBAT;
	}
}

void CGeneral::CountForces( NBotObjectives::SForces *pForces ) const
{
	for ( CGlobalIter iter( 0, ANY_PARTY ); !iter.IsFinished(); iter.Iterate() )
	{
		CAIUnit *pUnit = *iter;
		if ( !pUnit->IsAlive() || pUnit->GetPlayer() != nPlayer )
		{
			continue;
		}
		// By what the unit is rather than how it was called: a mod's mixed reinforcement has no category.
		const SUnitBaseRPGStats *pStats = pUnit->GetStats();
		if ( pStats->IsInfantry() )
		{
			pForces->own[NBotObjectives::FC_INFANTRY] += 0.2f; // about a squad's worth of soldiers is one unit
		}
		else if ( pStats->etype == RPG_TYPE_ART_AAGUN || pStats->etype == RPG_TYPE_SPG_AAGUN )
		{
			pForces->own[NBotObjectives::FC_ANTIAIR] += 1.0f;
		}
		else if ( pStats->etype == RPG_TYPE_ART_GUN || pStats->etype == RPG_TYPE_SPG_ANTITANK )
		{
			pForces->own[NBotObjectives::FC_ANTITANK] += 1.0f;
		}
		else if ( pStats->IsArtillery() )
		{
			pForces->own[NBotObjectives::FC_ARTILLERY] += 1.0f;
		}
		else if ( pStats->IsArmor() || pStats->IsSPG() )
		{
			pForces->own[NBotObjectives::FC_ARMOR] += 1.0f;
		}
	}

	// Only enemies the side has seen recently: this map forgets the others (EraseLastSeen)
	for ( CEnemyVisibility::const_iterator it = enemys.begin(); it != enemys.end(); ++it )
	{
		const CAIUnit *pEnemy = it->second.first;
		if ( !IsValidObj( pEnemy ) )
		{
			continue;
		}
		const SUnitBaseRPGStats *pStats = pEnemy->GetStats();
		if ( pStats->IsAviation() )
		{
			pForces->enemyAir += 1.0f;
		}
		else if ( pStats->IsInfantry() )
		{
			pForces->enemyInfantry += 0.2f;
		}
		else if ( pStats->IsArmor() || pStats->IsSPG() )
		{
			pForces->enemyArmor += 1.0f;
		}
	}
}

void CGeneral::CallBotReinforcement( const NBotObjectives::SForces &forces )
{
	bool available[NBotObjectives::FC_COUNT] = {};
	for ( int i = 0; i < NDb::_RT_NONE; ++i )
	{
		const int nCategory = GetBotCategory( i );
		if ( nCategory != NBotObjectives::FC_NOT_COMBAT && theReinfArray[nPlayer].HasReinforcement( NDb::EReinforcementType( i ) ) )
		{
			available[nCategory] = true;
		}
	}

	// Not over the limit of units, counting what this call would bring
	int nWorkers = pBotArtillery ? pBotArtillery->GetGunCount() : 0;
	for ( Tasks::iterator it = tasks.begin(); it != tasks.end(); ++it )
	{
		nWorkers += ( *it )->GetWorkerCount();
	}
	if ( nWorkers >= nMaxAllowedMobileTanks )
	{
		return;
	}

	const int nCategory = NBotObjectives::ChooseCategory( forces, available );
	std::vector<int> types;
	for ( int i = 0; i < NDb::_RT_NONE; ++i )
	{
		const bool bAvailable = theReinfArray[nPlayer].HasReinforcement( NDb::EReinforcementType( i ) );
		if ( bAvailable && GetBotCategory( i ) == nCategory && nCategory != NBotObjectives::FC_NOT_COMBAT )
		{
			types.push_back( i );
		}
	}

	// Mods add reinforcement types of their own, which the categories above don't know. A side
	// with nothing else still has to call something, as long as it isn't only engineers or aircraft.
	if ( types.empty() )
	{
		for ( int i = 0; i < NDb::_RT_NONE; ++i )
		{
			if ( theReinfArray[nPlayer].HasReinforcement( NDb::EReinforcementType( i ) ) && !IsAviationReinfType( i ) &&
			     i != NDb::RT_ENGINEERING )
			{
				types.push_back( i );
			}
		}
	}
	if ( types.empty() )
	{
		return;
	}

	const int nChoice = NRandom::Random( int( types.size() ) );
	RecordRandomCall();
	theReinfArray[nPlayer].CallReinforcement(
			NDb::EReinforcementType( types[nChoice] ), theReinfArray[nPlayer].GetRandomPoint(), -1 );
}

void CGeneral::BalanceUpdate( EBalanceAction eAction, CCommonUnit *_pUnit )
{
	if ( IsValidObj( _pUnit ) == false )
	{
		return;
	}

	if ( _pUnit->IsFormation() ) // Filter out formations, and count soldiers in there
	{
		CFormation *pForm = checked_cast<CFormation *>( _pUnit );

		for ( int i = 0; i < pForm->Size(); ++i )
		{
			BalanceUpdate( eAction, ( *pForm )[i] ); //Self-call!
		}

		return;
	}

	CAIUnit *pUnit = dynamic_cast<CAIUnit *>( _pUnit );
	NI_VERIFY( pUnit, "Invalid unit (not AIUnit)", return );
	const NDb::SUnitBaseRPGStats *pUnitStats = pUnit->GetStats();
	NDb::EDBUnitRPGType eUType = pUnitStats->eDBtype;
	NDb::EReinforcementType eRType = pUnit->GetReinforcementType();
	std::vector<float> effects( NDb::_RT_NONE, 0.0f );
	float fTypeMult = pUnitStats->fPrice; //Type multiplier, to compensate for infantry numbers and valuable units

	if ( pUnit->GetStats()->IsInfantry() )
	{
		if ( eUType != DB_RPG_TYPE_SNIPER )
		{
			fTypeMult *= 0.1f; // Soldier counts as 1/10 (all but snipers);
		}
	}

	int nUnitParty = pUnit->GetParty();
	int nAvail;
	int nAffected = 0;

	//Check validity of values, and reset to least harmful
	NI_VERIFY( eUType >= 0 && eUType < NDb::DB_RPG_TYPE_COUNT,
	           fmt::format( "Invalid unit type {}", int( eUType ) ),
	           eUType = NDb::DB_RPG_TYPE_SOLDIER );

	if ( eRType < 0 || eRType >= _RT_NONE )
	{
		eRType = NReinforcement::GetReinforcementType( eUType );
	}

	//Find out what reinfs the unit can be in
	for ( int i = 0; i < NDb::_RT_NONE; ++i )
	{
		effects[i] = 0.0;

		// Get availability for enemy
		// The array is per player. A party number only happens to match one in single player.
		nAvail = theReinfArray[theDipl.IsNetGame() ? pUnit->GetPlayer() : nUnitParty].GetUnitAvailability(
				eUType, NDb::EReinforcementType( i ) );

		if ( nAvail )
		{
			effects[i] += 1.0f / nAvail;
			++nAffected;
		}
		else if ( i == eRType )
		{
			effects[eRType] += 1;
		}
	}

	//Depending
	for ( int i = 0; i < NDb::_RT_NONE; ++i )
	{
		switch ( eAction )
		{
			case BA_ADD_ENEMY:
				enemyByRType[i] += effects[i] * fTypeMult;
				break;
			case BA_REMOVE_ENEMY:
				enemyByRType[i] -= effects[i] * fTypeMult;
				break;
			case BA_ADD_OWN:
				for ( int j = 0; j < NDb::_RT_NONE; ++j )
				{
					enemyByRType[i] -=
							effects[j] * fTypeMult *
							NReinforcement::GetReinforcementExpediency( NDb::EReinforcementType( j ), NDb::EReinforcementType( i ) ) /
							2;
				}
				break;
			case BA_REMOVE_OWN:
				for ( int j = 0; j < NDb::_RT_NONE; ++j )
				{
					enemyByRType[i] +=
							effects[j] * fTypeMult *
							NReinforcement::GetReinforcementExpediency( NDb::EReinforcementType( j ), NDb::EReinforcementType( i ) ) /
							2;
				}
				break;
		}
	}
}
