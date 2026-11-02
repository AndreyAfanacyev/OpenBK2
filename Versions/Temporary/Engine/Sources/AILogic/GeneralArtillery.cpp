#include "stdafx.h"

#include "Artillery.h"
#include "Building.h"
#include "Common_RTS_AI/AIMap.h"
#include "Diplomacy.h"
#include "GeneralArtillery.h"
#include "GeneralBotObjectives.h"
#include "GeneralConsts.h"
#include "GeneralInternal.h"
#include "GroupLogic.h"
#include "Guns.h"
#include "KeyBuildingBonusSystem.h"
#include "LinkObject.h"
#include "NewUpdater.h"
#include "port/wordpack.h"
#include "Shell.h"
#include "Technics.h"
#include "UnitStates.h"

#include "AILogic_export.h"

#include <cstdint>

#include <fmt/format.h>

extern NTimer::STime curTime;
extern CGroupLogic theGroupLogic;
extern CEventUpdater updater;
extern CDiplomacy theDipl;

REGISTER_SAVELOAD_CLASS( AILOGIC, 0x1508D4AB, CGeneralArtilleryGoToPosition );
REGISTER_SAVELOAD_CLASS( AILOGIC, 0x1508D4AF, CGeneralArtillery );
REGISTER_SAVELOAD_CLASS( AILOGIC, 0x1508D4F2, CGeneralBotArtillery );

bool HasTruck( CAIUnit *pUnit )
{
	CAIUnit *pTruck = pUnit->GetTruck();
	return pTruck && pTruck->IsRefValid() && pTruck->IsAlive() &&
	       theDipl.GetDiplStatusForParties( pUnit->GetParty(), pTruck->GetParty() ) != EDI_ENEMY;
}

bool CanUnitBombardRegion( CAIUnit *pUnit, const CVec2 &vRegionCenter )
{
	CVec2 vBattlePos;
	if ( ( HasTruck( pUnit ) || pUnit->CanMove() ) && pUnit->DoesReservePosExist() )
	{
		vBattlePos = pUnit->GetBattlePos();
	}
	else
	{
		vBattlePos = pUnit->GetCenterPlain();
	}

	const float fDistToRegion2 = fabs2( vRegionCenter - vBattlePos );
	const float fFireRange2 = sqr( pUnit->GetFirstArtilleryGun()->GetFireRange( 0 ) );

	return fFireRange2 > fDistToRegion2;
}

// артиллерия свободна и может выполнять различные команды
bool IsArtilleryFree( CAIUnit *pUnit )
{
	return !pUnit->GetState() ||
	       ( pUnit->GetState()->GetName() == EUSN_REST || pUnit->GetState()->GetName() == EUSN_AMBUSH );
}

bool IsArtilleryBeingTowed( CAIUnit *pUnit )
{
	return pUnit->GetState() && pUnit->GetState()->GetName() == EUSN_BEING_TOWED;
}

//*******************************************************************
//*												CGeneralArtillery													*
//*******************************************************************

//BASIC_REGISTER_CLASS( CGeneralArtillery );

CGeneralArtillery::CGeneralArtillery( CGeneral *_pOwner )
	: pOwner( _pOwner )
{
}

void CGeneralArtillery::Segment()
{
	// release free trucks
	for ( CUnitsList::iterator it = trucks.begin(); it != trucks.end(); )
	{
		if ( IsValidObj( *it ) )
		{
			if ( !checked_cast_ptr<CAITransportUnit *>( *it )->IsMustTow() )
			{
				pOwner->Give( *it );
				it = trucks.erase( it );
			}
			else
			{
				++it;
			}
		}
		else
		{
			++it;
		}
	}

	//
	for ( CUnitsList::iterator iter = freeUnits.begin(); iter != freeUnits.end(); )
	{
		CAIUnit *pUnit = *iter;
		const bool bValidUnit = pUnit && pUnit->IsRefValid() && pUnit->IsAlive();
		if ( !bValidUnit || theDipl.GetDiplStatusForParties( pUnit->GetParty(), pOwner->GetParty() ) == EDI_ENEMY )
		{
			if ( bValidUnit )
			{
				theGroupLogic.UnitCommand( SAIUnitCmd( ACTION_COMMAND_STOP ), pUnit, false );
			}

			iter = freeUnits.erase( iter );
		}
		else
		{
			++iter;
		}
	}

	std::list<CGeneralArtilleryTask>::iterator iter = tasks.begin();
	while ( iter != tasks.end() )
	{
		if ( iter->IsTaskFinished() )
		{
			iter = tasks.erase( iter );
		}
		else
		{
			( iter++ )->Segment();
		}
	}
}

bool CGeneralArtillery::CanBombardRegion( const CVec2 &vRegionCenter )
{
	for ( CUnitsList::iterator iter = freeUnits.begin(); iter != freeUnits.end(); ++iter )
	{
		CAIUnit *pUnit = *iter;
		CVec2 vDir = vRegionCenter - pUnit->GetCenterPlain();
		Normalize( &vDir );

		// сдвинуть центр дальше
		const CVec2 vNewCenter = vRegionCenter + vDir * 10 * SConsts::TILE_SIZE;
		if ( CanUnitBombardRegion( pUnit, vNewCenter ) )
		{
			return true;
		}
	}

	return false;
}

int CGeneralArtillery::RequestForSupport( const CVec2 &vCenter,
                                          const float fRadius,
                                          bool bIsAntiArtilleryFight,
                                          const int nCellNumber )
{
	std::list<CAIUnit *> bombardmentUnits;
	// сформировать список атакующей артиллерии
	int nMaxUnits = (std::min)( (int)freeUnits.size(), (int)NRandom::Random( 4, 8 ) );
	RecordRandomCall();
	int cnt = 0;
	for ( CUnitsList::iterator iter = freeUnits.begin(); iter != freeUnits.end() && cnt < nMaxUnits; )
	{
		CAIUnit *pUnit = *iter;
		if ( pUnit->GetParty() == pOwner->GetParty() && CanUnitBombardRegion( pUnit, vCenter ) &&
		     ( IsArtilleryFree( pUnit ) || IsArtilleryBeingTowed( pUnit ) ) )
		{
			bombardmentUnits.push_back( pUnit );
			iter = freeUnits.erase( iter );
			++cnt;
		}
		else
		{
			++iter;
		}
	}

	if ( bombardmentUnits.empty() )
	{
		return 0;
	}
	else
	{
		tasks.push_back(
				CGeneralArtilleryTask( this, bombardmentUnits, bIsAntiArtilleryFight, vCenter, fRadius, nCellNumber ) );
	}

	return 0;
}

void CGeneralArtillery::CancelRequest( int nRequestID, enum EForceType eType ) {}

void CGeneralArtillery::SetEnemyContainer( IEnemyContainer *pEnemyContainer ) {}

bool CGeneralArtillery::EnumEnemy( CAIUnit *pEnemy )
{
	return false;
}

void CGeneralArtillery::TakeTruck( CAIUnit *pUnit )
{
	trucks.push_back( pUnit );
}

void CGeneralArtillery::TakeArtillery( CAIUnit *pUnit )
{
	freeUnits.push_back( pUnit );
}

void CGeneralArtillery::SetCellInUse( const int nResistanceCell, bool bInUse )
{
	pOwner->SetCellInUse( nResistanceCell, bInUse );
}

//*******************************************************************
//*									CGeneralArtilleryGoToPosition										*
//*******************************************************************

CGeneralArtilleryGoToPosition::CGeneralArtilleryGoToPosition( CAIUnit *_pUnit,
                                                              const CVec2 &_vPos,
                                                              bool _bToReservePosition )
	: pUnit( _pUnit )
	, vPos( _vPos )
	, bToReservePosition( _bToReservePosition )
	, eState( EBS_START )
	, bFinished( false )
	, startTime( curTime )
{
	theGroupLogic.UnitCommand( SAIUnitCmd( ACTION_COMMAND_STOP ), pUnit, false );
}

void CGeneralArtilleryGoToPosition::StartState()
{
	if ( !pUnit->DoesReservePosExist() )
	{
		theGroupLogic.UnitCommand( SAIUnitCmd( ACTION_COMMAND_STOP ), pUnit, false );
		bFinished = true;
	}
	else if ( !HasTruck( pUnit ) )
	{
		eState = EBS_MOVING;
		if ( pUnit->CanCommandBeExecutedByStats( ACTION_COMMAND_SWARM_TO ) )
		{
			theGroupLogic.UnitCommand( SAIUnitCmd( ACTION_COMMAND_SWARM_TO, vPos ), pUnit, false );
		}
		else
		{
			theGroupLogic.UnitCommand( SAIUnitCmd( ACTION_COMMAND_MOVE_TO, vPos ), pUnit, false );
		}
	}
	else
	{
		eState = EBS_WAIT_FOR_TRUCK;
		startTime = curTime;
		theGroupLogic.UnitCommand(
				SAIUnitCmd( ACTION_COMMAND_TAKE_ARTILLERY, pUnit->GetUniqueId() ), pUnit->GetTruck(), false );
	}
}

void CGeneralArtilleryGoToPosition::WaitForTruck()
{
	if ( !HasTruck( pUnit ) )
	{
		eState = EBS_START;
	}
	else if ( IsArtilleryFree( pUnit->GetTruck() ) && pUnit->GetState() && IsArtilleryBeingTowed( pUnit ) )
	{
		eState = EBS_MOVING_WITH_TRUCK;
		if ( DoesGoToReservePosition() )
		{
			theGroupLogic.UnitCommand( SAIUnitCmd( ACTION_COMMAND_MOVE_TO, vPos ), pUnit->GetTruck(), false );
		}
		else
		{
			theGroupLogic.UnitCommand( SAIUnitCmd( ACTION_COMMAND_DEPLOY_ARTILLERY, vPos ), pUnit->GetTruck(), false );
		}
	}
	else if ( startTime + 2000 < curTime && pUnit->GetTruck()->GetState() &&
	          pUnit->GetTruck()->GetState()->IsRestState() )
	{
		bFinished = true;
	}
}

void CGeneralArtilleryGoToPosition::MovingWithTruck()
{
	if ( !HasTruck( pUnit ) )
	{
		eState = EBS_START;
	}
	else if ( IsArtilleryFree( pUnit ) || IsArtilleryFree( pUnit->GetTruck() ) )
	{
		if ( DoesGoToReservePosition() || !pUnit->NeedDeinstall() )
		{
			bFinished = true;
		}
		else
		{
			eState = EBS_FINISHING;
			timeOfFinish = curTime;
		}
	}
}

void CGeneralArtilleryGoToPosition::Finishing()
{
	if ( curTime - timeOfFinish >= 1500 )
	{
		NI_ASSERT( dynamic_cast_ptr<CArtillery *>( pUnit ) != 0, "Wrong unit" );
		CArtillery *pArtillery = checked_cast_ptr<CArtillery *>( pUnit );

		if ( !pArtillery->IsInInstallAction() )
		{
			bFinished = true;
		}
	}
}

void CGeneralArtilleryGoToPosition::Segment()
{
	if ( !bFinished )
	{
		switch ( eState )
		{
			case EBS_START:
				StartState();

				break;
			case EBS_MOVING:
				if ( IsArtilleryFree( pUnit ) )
				{
					bFinished = true;
				}

				break;
			case EBS_WAIT_FOR_TRUCK:
				WaitForTruck();

				break;
			case EBS_MOVING_WITH_TRUCK:
				MovingWithTruck();

				break;
			case EBS_FINISHING:
				Finishing();

				break;
			default:
				NI_ASSERT( false, "Wrong state" );
		}
	}
}

//*******************************************************************
//*							CGeneralArtilleryTask::SBombardmentUnitState				*
//*******************************************************************

CGeneralArtilleryTask::SBombardmentUnitState::SBombardmentUnitState( CAIUnit *_pUnit )
	: pUnit( _pUnit )
{
	vReservePosition = pUnit->GetCenterPlain();
}

//*******************************************************************
//*											CGeneralArtilleryTask												*
//*******************************************************************

CGeneralArtilleryTask::CGeneralArtilleryTask( CGeneralArtillery *_pOwner,
                                              std::list<CAIUnit *> &givenUnits,
                                              bool bAntiArtilleryFight,
                                              const CVec2 &vCenter,
                                              const float fRadius,
                                              const int _nCellNumber )
	: pOwner( _pOwner )
	, bIsAntiArtilleryFight( bAntiArtilleryFight )
	, bBombardmentFinished( false )
	, eState( EBS_START )
	, vBombardmentCenter( vCenter )
	, fBombardmentRadius( fRadius )
	, timeToSendAntiArtilleryAck( 0 )
	, nCellNumber( _nCellNumber )
{
	for ( std::list<CAIUnit *>::iterator iter = givenUnits.begin(); iter != givenUnits.end(); ++iter )
	{
		bombardmentUnits.push_back( SBombardmentUnitState( *iter ) );
	}

	pOwner->SetCellInUse( nCellNumber, true );
}

void CGeneralArtilleryTask::SetBombardmentFinished()
{
	bBombardmentFinished = true;
	pOwner->SetCellInUse( nCellNumber, false );
}

void CGeneralArtilleryTask::CheckEscapingUnits()
{
	for ( std::list<SBombardmentUnitState>::iterator iter = bombardmentUnits.begin(); iter != bombardmentUnits.end(); )
	{
		if ( iter->pGoToPosition )
		{
			iter->pGoToPosition->Segment();
			if ( iter->pGoToPosition->IsFinished() )
			{
				pOwner->TakeArtillery( iter->pUnit );
				iter = bombardmentUnits.erase( iter );
			}
			else
			{
				++iter;
			}
		}
		else
		{
			++iter;
		}
	}
}

void CGeneralArtilleryTask::StartBombardment()
{
	const int nUnits = bombardmentUnits.size();
	const float fShift = ( nUnits == 0 ) ? 0.0f : fBombardmentRadius / 2.0f;
	uint16_t wDir = 0;

	for ( std::list<SBombardmentUnitState>::iterator iter = bombardmentUnits.begin(); iter != bombardmentUnits.end();
	      ++iter, wDir += 65536.0f / float( nUnits ) )
	{
		const CVec2 vCenter = vBombardmentCenter + GetVectorByDirection( wDir ) * fShift;
		CAIUnit *pUnit = iter->pUnit;

		if ( CanUnitBombardRegion( pUnit, vCenter ) )
		{
			iter->vAttackPos = vCenter;
		}
		else
		{
			iter->vAttackPos = vBombardmentCenter;
		}

		CVec2 vBattlePos;
		if ( ( HasTruck( pUnit ) || pUnit->CanMove() ) && pUnit->DoesReservePosExist() )
		{
			vBattlePos = pUnit->GetBattlePos();
		}
		else
		{
			vBattlePos = pUnit->GetCenterPlain();
		}

		iter->pGoToPosition = new CGeneralArtilleryGoToPosition( pUnit, vBattlePos, false );
	}

	eState = EBS_GOING_TO_BATTLE;
}

void CGeneralArtilleryTask::GoingToBattle()
{
	bool bFinished = true;
	for ( std::list<SBombardmentUnitState>::iterator iter = bombardmentUnits.begin(); iter != bombardmentUnits.end();
	      ++iter )
	{
		if ( iter->pGoToPosition )
		{
			iter->pGoToPosition->Segment();
			if ( iter->pGoToPosition->IsFinished() )
			{
				CAIUnit *pUnit = iter->pUnit;

				// не смогла отцепиться
				if ( IsArtilleryBeingTowed( pUnit ) )
				{
					iter->pGoToPosition = new CGeneralArtilleryGoToPosition( pUnit, iter->vReservePosition, true );
				}
				else
				{
					const CVec2 vCenter = pUnit->GetCenterPlain();
					const float fDistToAttackPos2 = fabs2( vCenter - iter->vAttackPos );
					const float fFireRange2 = sqr( pUnit->GetFirstArtilleryGun()->GetFireRange( 0 ) );

					// не достреливает
					if ( fFireRange2 <= fDistToAttackPos2 )
					{
						iter->pGoToPosition = new CGeneralArtilleryGoToPosition( pUnit, iter->vReservePosition, true );
					}
					else
					{
						iter->pGoToPosition = 0;
					}
				}
			}
		}

		if ( iter->pGoToPosition && !iter->pGoToPosition->DoesGoToReservePosition() )
		{
			bFinished = false;
		}
	}

	if ( bFinished )
	{
		eState = EBS_ROTATING;
		startRotatingTime = curTime;

		for ( std::list<SBombardmentUnitState>::iterator iter = bombardmentUnits.begin(); iter != bombardmentUnits.end();
		      ++iter )
		{
			if ( iter->pGoToPosition == 0 )
			{
				CAIUnit *pUnit = iter->pUnit;
				if ( HasTruck( pUnit ) )
				{
					CAIUnit *pTruck = pUnit->GetTruck();
					theGroupLogic.UnitCommand( SAIUnitCmd( ACTION_COMMAND_RESUPPLY,
					                                       pTruck->GetCenterPlain() + pTruck->GetDirectionVector() * 2.0f *
					                                                                      pTruck->GetStats()->vAABBHalfSize.y ),
					                           pTruck,
					                           false );
				}
			}
		}
	}
}

void CGeneralArtilleryTask::Rotating()
{
	CheckEscapingUnits();

	if ( startRotatingTime + 1000 < curTime )
	{
		if ( startRotatingTime != 0 )
		{
			startRotatingTime = 0;

			for ( std::list<SBombardmentUnitState>::iterator iter = bombardmentUnits.begin(); iter != bombardmentUnits.end();
			      ++iter )
			{
				if ( !iter->pGoToPosition )
				{
					CAIUnit *pUnit = iter->pUnit;
					if ( !pUnit->GetFirstArtilleryGun()->CanShootToPointWOMove( iter->vAttackPos, 0 ) )
					{
						theGroupLogic.UnitCommand( SAIUnitCmd( ACTION_COMMAND_ROTATE_TO, iter->vAttackPos ), pUnit, false );
					}
				}
			}
		}
		else
		{
			for ( std::list<SBombardmentUnitState>::iterator iter = bombardmentUnits.begin(); iter != bombardmentUnits.end();
			      ++iter )
			{
				if ( !iter->pGoToPosition )
				{
					CAIUnit *pUnit = iter->pUnit;
					if ( pUnit->GetState() && pUnit->GetState()->GetName() == EUSN_TURN_TO_POINT )
					{
						return;
					}
					if ( !pUnit->IsInstalled() )
					{
						return;
					}
				}
			}

			eState = EBS_FIRING;
			NTimer::STime maxTimeToShoot = 0;
			CalculateTimeToSendAntiArtilleryAck();
			for ( std::list<SBombardmentUnitState>::iterator iter = bombardmentUnits.begin(); iter != bombardmentUnits.end();
			      ++iter )
			{
				if ( !iter->pGoToPosition )
				{
					CAIUnit *pUnit = iter->pUnit;

					theGroupLogic.UnitCommand( SAIUnitCmd( ACTION_COMMAND_ART_BOMBARDMENT, iter->vAttackPos ), pUnit, false );

					CBasicGun *pGun = pUnit->GetFirstArtilleryGun();
					const NTimer::STime timeToShoot = pGun->GetAimTime( false ) + pGun->GetRelaxTime( false );

					if ( timeToShoot > maxTimeToShoot )
					{
						maxTimeToShoot = timeToShoot;
					}
				}
			}

			timeToFinishBombardment = curTime + maxTimeToShoot * SGeneralConsts::SHOOTS_OF_ARTILLERY_FIRE;
		}
	}
}

void CGeneralArtilleryTask::Firing()
{
	CheckEscapingUnits();

	if ( curTime >= timeToFinishBombardment )
	{
		eState = EBS_ESCAPING;

		for ( std::list<SBombardmentUnitState>::iterator iter = bombardmentUnits.begin(); iter != bombardmentUnits.end();
		      ++iter )
		{
			if ( !iter->pGoToPosition )
			{
				iter->pGoToPosition = new CGeneralArtilleryGoToPosition( iter->pUnit, iter->vReservePosition, true );
			}
		}
	}
}

void CGeneralArtilleryTask::Escaping()
{
	bool bFinished = true;
	for ( std::list<SBombardmentUnitState>::iterator iter = bombardmentUnits.begin(); iter != bombardmentUnits.end(); )
	{
		iter->pGoToPosition->Segment();
		if ( iter->pGoToPosition->IsFinished() )
		{
			pOwner->TakeArtillery( iter->pUnit );
			iter = bombardmentUnits.erase( iter );
		}
		else
		{
			bFinished = false;
			++iter;
		}
	}

	if ( bFinished )
	{
		SetBombardmentFinished();
	}
}

void CGeneralArtilleryTask::CalculateTimeToSendAntiArtilleryAck()
{
	vAntiArtilleryAckCenter = VNULL2;
	int cnt = 0;
	NTimer::STime maxTimeToWait = 0;
	for ( std::list<SBombardmentUnitState>::iterator iter = bombardmentUnits.begin(); iter != bombardmentUnits.end();
	      ++iter )
	{
		if ( !iter->pGoToPosition )
		{
			++cnt;
			vAntiArtilleryAckCenter += iter->vAttackPos;

			//
			CAIUnit *pUnit = iter->pUnit;
			if ( CBasicGun *pGun = pUnit->GetFirstArtilleryGun() )
			{
				CPtr<IBallisticTraj> pTraj = pGun->CreateTraj( CVec3( iter->vAttackPos, 0 ) );
				const NTimer::STime localTimeToWait = pTraj->GetExplTime() - pTraj->GetStartTime() +
				                                      2 * ( pGun->GetAimTime( false ) + pGun->GetRelaxTime( false ) );

				if ( localTimeToWait > maxTimeToWait )
				{
					maxTimeToWait = localTimeToWait;
				}
			}
		}
	}

	vAntiArtilleryAckCenter /= float( cnt );
	if ( maxTimeToWait != 0 )
	{
		timeToSendAntiArtilleryAck = curTime + maxTimeToWait;
	}
	else
	{
		timeToSendAntiArtilleryAck = 0;
	}
}

void CGeneralArtilleryTask::Segment()
{
	for ( std::list<SBombardmentUnitState>::iterator iter = bombardmentUnits.begin(); iter != bombardmentUnits.end(); )
	{
		CPtr<CAIUnit> pUnit = iter->pUnit;

		bool bWrongParty = false;
		if ( IsValidObj( pUnit ) )
		{
			// The local player must not decide this in a net game: every client has to agree.
			bWrongParty = !IsArtilleryBeingTowed( pUnit ) &&
			              ( theDipl.IsNetGame() ? !theDipl.IsPlayerBot( pUnit->GetPlayer() )
			                                    : pUnit->GetParty() == theDipl.GetMyParty() );
		}

		if ( !IsValidObj( pUnit ) || bWrongParty )
		{
			if ( bWrongParty )
			{
				theGroupLogic.UnitCommand( SAIUnitCmd( ACTION_COMMAND_STOP ), pUnit, false );
			}

			iter = bombardmentUnits.erase( iter );
		}
		else
		{
			++iter;
		}
	}

	if ( bombardmentUnits.empty() )
	{
		SetBombardmentFinished();
	}
	else
	{
		switch ( eState )
		{
			case EBS_START:
				StartBombardment();

				break;
			case EBS_GOING_TO_BATTLE:
				GoingToBattle();

				break;
			case EBS_ROTATING:
				Rotating();

				break;
			case EBS_FIRING:
			{
				Firing();

				if ( bIsAntiArtilleryFight && timeToSendAntiArtilleryAck != 0 && timeToSendAntiArtilleryAck <= curTime )
				{
					NI_ASSERT( vAntiArtilleryAckCenter.x >= 0.0f && vAntiArtilleryAckCenter.y >= 0.0f,
					           fmt::format( "Wrong vAntiArtilleryAckCenter ({:g},{:g})",
					                        vAntiArtilleryAckCenter.x,
					                        vAntiArtilleryAckCenter.y ) );
					updater.AddUpdate( EFB_ENEMY_STARTED_ANTIARTILLERY,
					                   MAKELONG( vAntiArtilleryAckCenter.x, vAntiArtilleryAckCenter.y ) );
					timeToSendAntiArtilleryAck = 0;
				}
			}

			break;
			case EBS_ESCAPING:
				Escaping();

				break;
			default:
				NI_ASSERT( false, fmt::format( "Wrong bombardment state ({})", int( eState ) ) );
		}
	}
}

//*******************************************************************
//*										CGeneralBotArtillery												*
//*******************************************************************
extern CKeyBuildingBonusSystem theBonusSystem;

namespace
{
// How much of its range away from the focus a gun stands
const float INDIRECT_RANGE_SHARE = 0.75f;
const float DIRECT_RANGE_SHARE = 0.6f;
// Time a gun has to reach its position before it counts as a failed move
const int GUN_MOVE_TIMEOUT = 180000;
const int GUN_MAX_FAILED_MOVES = 3;
const int GUN_MOVE_RETRY = 60000;
// Between salvos at what is seen, and at a point the enemy holds when nothing is seen
const int GUN_FIRE_PAUSE = 8000;
const int GUN_HARASS_PAUSE = 45000;
// Rockets give their position away: after a salvo they drive this many tiles aside
const int ROCKET_SCOOT_TILES = 10;
// Fire at this many tiles from a point rather than at it: wrecking it would make it uncapturable
const int HARASS_OFFSET_TILES = 8;
} // namespace

static bool IsTowingGun( CAIUnit *pTractor, const CAIUnit *pGun )
{
	const CMilitaryCar *pCar = dynamic_cast<CMilitaryCar *>( pTractor );
	return pCar && pCar->GetTowedArtillery() == pGun;
}

CGeneralBotArtillery::CGeneralBotArtillery( CGeneral *_pOwner )
	: pOwner( _pOwner )
{
}

bool CGeneralBotArtillery::TakesUnit( CAIUnit *pUnit )
{
	const SUnitBaseRPGStats *pStats = pUnit->GetStats();
	if ( pStats->IsTransport() )
	{
		return pStats->etype == RPG_TYPE_TRN_TRACTOR || pUnit->IsTowing();
	}
	return pStats->IsArtillery() || ( pStats->IsSPG() && pUnit->GetFirstArtilleryGun() != 0 );
}

void CGeneralBotArtillery::Take( CAIUnit *pUnit )
{
	if ( pUnit->GetStats()->IsTransport() )
	{
		tractors.push_back( pUnit );
	}
	else
	{
		SGun &gun = guns.emplace_back();
		gun.pGun = pUnit;
	}
}

CGeneralBotArtillery::EGunRole CGeneralBotArtillery::GetRole( const CAIUnit *pGun )
{
	switch ( pGun->GetStats()->etype )
	{
		case RPG_TYPE_ART_AAGUN:
		case RPG_TYPE_SPG_AAGUN:
			return GR_ANTIAIR;
		case RPG_TYPE_ART_GUN:
		case RPG_TYPE_ART_HEAVY_MG:
			return GR_DIRECT;
		default:
			return pGun->GetFirstArtilleryGun() ? GR_INDIRECT : GR_DIRECT;
	}
}

float CGeneralBotArtillery::GetRange( CAIUnit *pGun )
{
	CBasicGun *pWeapon = pGun->GetFirstArtilleryGun();
	if ( !pWeapon && pGun->GetNGuns() > 0 )
	{
		pWeapon = pGun->GetGun( 0 );
	}
	return pWeapon ? pWeapon->GetFireRange( 0 ) : 0.0f;
}

float CGeneralBotArtillery::GetMinRange( CAIUnit *pGun )
{
	CBasicGun *pWeapon = pGun->GetFirstArtilleryGun();
	return pWeapon ? pWeapon->GetWeapon()->fRangeMin : 0.0f;
}

bool CGeneralBotArtillery::CanRelocateByItself( CAIUnit *pGun )
{
	return pGun->CanMove() && pGun->CanCommandBeExecutedByStats( ACTION_COMMAND_MOVE_TO );
}

bool CGeneralBotArtillery::EnumResistances( const SResistance &resistance )
{
	resistanceCenters.push_back( resistance.GetResistanceCellCenter() );
	return true;
}

bool CGeneralBotArtillery::FindFocus( const SGun &gun, const EGunRole eRole, CVec2 *pFocus ) const
{
	const CVec2 vFrom = gun.pGun->GetCenterPlain();
	const int nParty = pOwner->GetParty();
	std::vector<int> links;
	theBonusSystem.GetKeyBuildingLinks( &links );
	bool bFound = false;
	float fBest = 0.0f;
	for ( int i = 0; i < links.size(); ++i )
	{
		CBuilding *pBuilding = dynamic_cast<CBuilding *>( CLinkObject::GetObjectByLink( links[i] ) );
		if ( !pBuilding || !pBuilding->IsAlive() )
		{
			continue;
		}
		// Anti-air guns protect what the side holds, the others support taking what it doesn't
		const bool bOwn = theDipl.GetNParty( pBuilding->GetPlayer() ) == nParty;
		if ( bOwn != ( eRole == GR_ANTIAIR ) )
		{
			continue;
		}
		const CVec2 vCenter( pBuilding->GetCenter().x, pBuilding->GetCenter().y );
		const float fDistance = fabs2( vCenter - vFrom );
		if ( !bFound || fDistance < fBest )
		{
			*pFocus = vCenter;
			fBest = fDistance;
			bFound = true;
		}
	}
	if ( bFound || eRole == GR_ANTIAIR )
	{
		return bFound;
	}

	// The side holds every point: support the fight against whatever it knows about
	for ( int i = 0; i < resistanceCenters.size(); ++i )
	{
		const float fDistance = fabs2( resistanceCenters[i] - vFrom );
		if ( !bFound || fDistance < fBest )
		{
			*pFocus = resistanceCenters[i];
			fBest = fDistance;
			bFound = true;
		}
	}
	return bFound;
}

bool CGeneralBotArtillery::FindFirePoint( const SGun &gun, CVec2 *pPoint, bool *pbRealTarget ) const
{
	const CVec2 vFrom = gun.pGun->GetCenterPlain();
	const float fRange = GetRange( gun.pGun );
	const float fMinRange = GetMinRange( gun.pGun );

	// What the side knows about, nearest first
	bool bFound = false;
	float fBest = 0.0f;
	for ( int i = 0; i < resistanceCenters.size(); ++i )
	{
		const float fDistance = fabs( resistanceCenters[i] - vFrom );
		if ( NBotObjectives::IsAtFireDistance( fDistance, fRange / 0.95f, fMinRange ) && ( !bFound || fDistance < fBest ) )
		{
			*pPoint = resistanceCenters[i];
			*pbRealTarget = true;
			fBest = fDistance;
			bFound = true;
		}
	}
	if ( bFound )
	{
		return true;
	}

	// Nothing seen: keep the ground around an enemy point under occasional fire
	const int nParty = pOwner->GetParty();
	std::vector<int> links;
	theBonusSystem.GetKeyBuildingLinks( &links );
	for ( int i = 0; i < links.size(); ++i )
	{
		CBuilding *pBuilding = dynamic_cast<CBuilding *>( CLinkObject::GetObjectByLink( links[i] ) );
		if ( !pBuilding || !pBuilding->IsAlive() )
		{
			continue;
		}
		const int nOwnerParty = theDipl.GetNParty( pBuilding->GetPlayer() );
		if ( nOwnerParty == nParty || nOwnerParty == theDipl.GetNeutralParty() )
		{
			continue;
		}
		const CVec2 vCenter( pBuilding->GetCenter().x, pBuilding->GetCenter().y );
		CVec2 vDir = vCenter - vFrom;
		const float fLength = fabs( vDir );
		if ( fLength < 1.0f )
		{
			continue;
		}
		const CVec2 vPoint = vCenter - vDir / fLength * float( HARASS_OFFSET_TILES * SConsts::TILE_SIZE );
		const float fDistance = fabs( vPoint - vFrom );
		if ( NBotObjectives::IsAtFireDistance( fDistance, fRange / 0.95f, fMinRange ) && ( !bFound || fDistance < fBest ) )
		{
			*pPoint = vPoint;
			*pbRealTarget = false;
			fBest = fDistance;
			bFound = true;
		}
	}
	return bFound;
}

CAIUnit *CGeneralBotArtillery::FindTractor( const SGun &gun ) const
{
	if ( IsValidObj( gun.pTractor ) )
	{
		return gun.pTractor;
	}

	// One that already has this gun in tow, otherwise the nearest not working for another gun
	CAIUnit *pBest = 0;
	float fBest = 0.0f;
	for ( int i = 0; i < tractors.size(); ++i )
	{
		CAIUnit *pTractor = tractors[i];
		if ( !IsValidObj( pTractor ) )
		{
			continue;
		}
		if ( IsTowingGun( pTractor, gun.pGun ) )
		{
			return pTractor;
		}
		bool bBusy = pTractor->IsTowing();
		for ( int j = 0; j < guns.size() && !bBusy; ++j )
		{
			bBusy = guns[j].pTractor.GetPtr() == pTractor;
		}
		const float fDistance = fabs2( pTractor->GetCenterPlain() - gun.pGun->GetCenterPlain() );
		if ( !bBusy && ( !pBest || fDistance < fBest ) )
		{
			pBest = pTractor;
			fBest = fDistance;
		}
	}
	return pBest;
}

void CGeneralBotArtillery::MoveGun( SGun *pGun, const CVec2 &vWanted )
{
	// Stay on the map
	const float fMaxX = GetAIMap()->GetSizeX() * SConsts::TILE_SIZE - SConsts::TILE_SIZE;
	const float fMaxY = GetAIMap()->GetSizeY() * SConsts::TILE_SIZE - SConsts::TILE_SIZE;
	const float fMin = SConsts::TILE_SIZE;
	const CVec2 vDestination( (std::min)( (std::max)( vWanted.x, fMin ), fMaxX ), (std::min)( (std::max)( vWanted.y, fMin ), fMaxY ) );

	CAIUnit *pTractor = FindTractor( *pGun );
	pGun->pTractor = pTractor;
	if ( pTractor )
	{
		// The same pair of orders a gun gives its own transport (see CArtilleryMoveToState)
		if ( !IsTowingGun( pTractor, pGun->pGun ) )
		{
			theGroupLogic.UnitCommand( SAIUnitCmd( ACTION_COMMAND_TAKE_ARTILLERY, pGun->pGun->GetUniqueId() ), pTractor, false );
			theGroupLogic.UnitCommand( SAIUnitCmd( ACTION_COMMAND_DEPLOY_ARTILLERY, vDestination ), pTractor, true );
		}
		else
		{
			theGroupLogic.UnitCommand( SAIUnitCmd( ACTION_COMMAND_DEPLOY_ARTILLERY, vDestination ), pTractor, false );
		}
	}
	else if ( CanRelocateByItself( pGun->pGun ) )
	{
		theGroupLogic.UnitCommand( SAIUnitCmd( ACTION_COMMAND_MOVE_TO, vDestination ), pGun->pGun, false );
	}
	else
	{
		// Nothing can move it for now: it fires at what it can reach from here
		pGun->timeNextMove = curTime + GUN_MOVE_RETRY;
		return;
	}

	pGun->vDestination = vDestination;
	pGun->eState = GS_MOVING;
	pGun->timeStateEnd = curTime + GUN_MOVE_TIMEOUT;
}

void CGeneralBotArtillery::SegmentReady( SGun *pGun )
{
	const EGunRole eRole = GetRole( pGun->pGun );
	const CVec2 vFrom = pGun->pGun->GetCenterPlain();
	const float fRange = GetRange( pGun->pGun );
	const float fMinRange = GetMinRange( pGun->pGun );

	// A gun still in tow can't fire, even if it happens to be in range already: it has to be set down.
	const bool bInTow = IsArtilleryBeingTowed( pGun->pGun );
	CVec2 vFocus;
	if ( curTime >= pGun->timeNextMove && fRange > 0.0f && FindFocus( *pGun, eRole, &vFocus ) )
	{
		const float fShare = eRole == GR_INDIRECT ? INDIRECT_RANGE_SHARE : DIRECT_RANGE_SHARE;
		const float fDistance = fabs( vFocus - vFrom );
		// Anti-air guns just stand near the point. The others keep their distance to it.
		const float fOutermost = eRole == GR_INDIRECT ? fRange : fRange * DIRECT_RANGE_SHARE / 0.95f * 1.25f;
		if ( !NBotObjectives::IsAtFireDistance( fDistance, fOutermost, fMinRange ) )
		{
			MoveGun( pGun, NBotObjectives::FirePosition( vFocus, vFrom, fRange, fMinRange, fShare ) );
		}
		else if ( bInTow )
		{
			MoveGun( pGun, vFrom );
		}
	}
	else if ( bInTow && curTime >= pGun->timeNextMove )
	{
		MoveGun( pGun, vFrom ); // nothing to go to yet, but a gun in tow is no use to anyone
	}
	if ( pGun->eState == GS_MOVING )
	{
		return;
	}

	// Direct fire guns shoot at what comes into range by themselves
	if ( eRole != GR_INDIRECT || curTime < pGun->timeNextFire || !pGun->pGun->IsInstalled() )
	{
		return;
	}

	CVec2 vPoint;
	bool bRealTarget = false;
	if ( FindFirePoint( *pGun, &vPoint, &bRealTarget ) )
	{
		pGun->vFirePoint = vPoint;
		pGun->bAtRealTarget = bRealTarget;
		if ( !pGun->pGun->GetFirstArtilleryGun()->CanShootToPointWOMove( vPoint, 0 ) )
		{
			theGroupLogic.UnitCommand( SAIUnitCmd( ACTION_COMMAND_ROTATE_TO, vPoint ), pGun->pGun, false );
		}
		pGun->eState = GS_AIMING;
		pGun->timeStateEnd = curTime + 1500;
	}
}

void CGeneralBotArtillery::SegmentMoving( SGun *pGun )
{
	const bool bGunFree = IsArtilleryFree( pGun->pGun );
	const bool bTractorFree = !IsValidObj( pGun->pTractor ) || IsArtilleryFree( pGun->pTractor );
	const bool bTimedOut = curTime >= pGun->timeStateEnd;
	if ( ( bGunFree && bTractorFree ) || bTimedOut )
	{
		pGun->eState = GS_READY;
		if ( fabs( pGun->pGun->GetCenterPlain() - pGun->vDestination ) > 10.0f * SConsts::TILE_SIZE )
		{
			// Didn't get there. A few tries, then fire from where it is for a while.
			if ( ++pGun->nFailedMoves >= GUN_MAX_FAILED_MOVES )
			{
				pGun->nFailedMoves = 0;
				pGun->timeNextMove = curTime + GUN_MOVE_RETRY;
			}
		}
		else
		{
			pGun->nFailedMoves = 0;
		}
	}
}

void CGeneralBotArtillery::SegmentAiming( SGun *pGun )
{
	if ( curTime < pGun->timeStateEnd || !IsArtilleryFree( pGun->pGun ) || !pGun->pGun->IsInstalled() )
	{
		return;
	}

	theGroupLogic.UnitCommand( SAIUnitCmd( ACTION_COMMAND_ART_BOMBARDMENT, pGun->vFirePoint ), pGun->pGun, false );
	CBasicGun *pWeapon = pGun->pGun->GetFirstArtilleryGun();
	pGun->eState = GS_FIRING;
	pGun->timeStateEnd =
			curTime + ( pWeapon->GetAimTime( false ) + pWeapon->GetRelaxTime( false ) ) * SGeneralConsts::SHOOTS_OF_ARTILLERY_FIRE;
}

void CGeneralBotArtillery::SegmentFiring( SGun *pGun )
{
	if ( curTime < pGun->timeStateEnd )
	{
		return;
	}

	theGroupLogic.UnitCommand( SAIUnitCmd( ACTION_COMMAND_STOP ), pGun->pGun, false );
	pGun->eState = GS_READY;
	pGun->timeNextFire = curTime + ( pGun->bAtRealTarget ? GUN_FIRE_PAUSE : GUN_HARASS_PAUSE );

	// Rockets drive away from where they were seen firing
	if ( pGun->pGun->GetStats()->etype == RPG_TYPE_ART_ROCKET || pGun->pGun->GetStats()->IsSPG() )
	{
		if ( CanRelocateByItself( pGun->pGun ) && !IsValidObj( pGun->pTractor ) )
		{
			CVec2 vDir = pGun->vFirePoint - pGun->pGun->GetCenterPlain();
			Normalize( &vDir );
			// Sideways, to a side that comes out the same on every client
			const float fSide = pGun->pGun->GetUniqueId() % 2 == 0 ? 1.0f : -1.0f;
			MoveGun( pGun, pGun->pGun->GetCenterPlain() + CVec2( -vDir.y, vDir.x ) * fSide * float( ROCKET_SCOOT_TILES * SConsts::TILE_SIZE ) );
		}
	}
}

void CGeneralBotArtillery::Segment()
{
	for ( int i = guns.size() - 1; i >= 0; --i )
	{
		if ( !IsValidObj( guns[i].pGun ) || guns[i].pGun->GetPlayer() != pOwner->GetPlayer() )
		{
			guns.erase( guns.begin() + i );
		}
	}
	for ( int i = tractors.size() - 1; i >= 0; --i )
	{
		if ( !IsValidObj( tractors[i] ) )
		{
			tractors.erase( tractors.begin() + i );
		}
	}

	resistanceCenters.clear();
	pOwner->GiveResistances( this );

	for ( int i = 0; i < guns.size(); ++i )
	{
		switch ( guns[i].eState )
		{
			case GS_READY:
				SegmentReady( &guns[i] );
				break;
			case GS_MOVING:
				SegmentMoving( &guns[i] );
				break;
			case GS_AIMING:
				SegmentAiming( &guns[i] );
				break;
			case GS_FIRING:
				SegmentFiring( &guns[i] );
				break;
		}
	}
}
