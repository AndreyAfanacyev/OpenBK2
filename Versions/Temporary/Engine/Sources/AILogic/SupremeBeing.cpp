#include "stdafx.h"

#include "AIUnit.h"
#include "AIUnitInfoForGeneral.h"
#include "Diplomacy.h"
#include "GeneralInternal.h"
#include "Scripts.h"

CSupremeBeing theSupremeBeing;

extern NTimer::STime curTime;
extern CDiplomacy theDipl;
extern CScripts *pScripts;

//*******************************************************************
//*											CSupremeBeing																*
//*******************************************************************

struct IEnemyContainer *CSupremeBeing::GetEnemyConatiner( int nParty )
{
	return generals[nParty];
}

void CSupremeBeing::SetAAVisible( class CAIUnit *pUnit, const int nGeneralParty, const bool bVisible )
{
	for ( Generals::iterator it = generals.begin(); it != generals.end(); ++it )
	{
		if ( it->second->GetParty() == nGeneralParty )
		{
			it->second->SetAAVisible( pUnit, bVisible );
		}
	}
}

void CSupremeBeing::SetUnitVisible( class CAIUnit *pUnit, const int nGeneralParty, const bool bVisible )
{
	for ( Generals::iterator it = generals.begin(); it != generals.end(); ++it )
	{
		if ( it->second->GetParty() == nGeneralParty )
		{
			it->second->SetUnitVisible( pUnit, bVisible );
		}
	}
}

bool CSupremeBeing::MustShootToObstacles( const int nPlayer )
{
	return false;
}

void CSupremeBeing::Clear()
{
	delayedTasks.clear();
	generals.clear();
}

void CSupremeBeing::Init( const NDb::SMapInfo *pMapInfo )
{
	if ( theDipl.IsNetGame() )
	{
		// One general per bot, in every client's simulation. The "nogeneral" console
		// variable is deliberately not read here: it is per-client and would desync.
		for ( int i = 0; i < pMapInfo->players.size(); ++i )
		{
			if ( theDipl.IsPlayerBot( i ) )
			{
				generals[i] = new CGeneral( theDipl.GetNParty( i ), i );
				generals[i]->Init( pMapInfo->players[i].general );
			}
		}
	}
	else if ( !NGlobal::GetVar( "nogeneral", 0 ) )
	{
		for ( int i = 0; i < pMapInfo->players.size(); ++i )
		{
			const int nParty = theDipl.GetNParty( i );
			if ( generals.find( nParty ) == generals.end() && nParty == 1 )
			{
				//generals.insert( std::pair<int, CPtr<CGeneral> >( nParty, new CGeneral( nParty ) ) );
				generals[nParty] = new CGeneral( nParty );
				generals[nParty]->Init( pMapInfo->players[i].general );
			}
		}
	}
}

void CSupremeBeing::GiveNewUnitsToGenerals( const std::list<CCommonUnit *> &pUnits, bool bFromReinforcement )
{
	if ( theDipl.IsNetGame() )
	{
		// Each bot filters out its own player's units.
		for ( Generals::iterator it = generals.begin(); it != generals.end(); ++it )
		{
			it->second->GiveNewUnits( pUnits, true );
		}
	}
	else if ( !NGlobal::GetVar( "nogeneral", 0 ) )
	{
		generals[1]->GiveNewUnits( pUnits, bFromReinforcement );
	}
}

void CSupremeBeing::AddIronman( const int nScriptGroup )
{
	ironmans.insert( nScriptGroup );
}

bool CSupremeBeing::IsIronman( const int nScriptGroup ) const
{
	return ironmans.find( nScriptGroup ) != ironmans.end();
}

bool CSupremeBeing::IsMobileReinforcement( int nParty, int nGroup ) const
{
	for ( Generals::const_iterator it = generals.begin(); it != generals.end(); ++it )
	{
		if ( it->second->GetParty() == nParty && it->second->IsMobileReinforcement( nGroup ) )
		{
			return true;
		}
	}
	return false;
}

bool CSupremeBeing::HasGeneralInParty( const int nParty ) const
{
	for ( Generals::const_iterator it = generals.begin(); it != generals.end(); ++it )
	{
		if ( it->second->GetParty() == nParty )
		{
			return true;
		}
	}
	return false;
}

bool CSupremeBeing::HasGeneralForPlayer( const int nPlayer ) const
{
	return generals.find( nPlayer ) != generals.end();
}

CGeneral *CSupremeBeing::GetGeneralOfUnit( const CCommonUnit *pUnit )
{
	// Single player: the one general owns its whole party. Net game: a bot owns only its player's units.
	const Generals::iterator it = generals.find( theDipl.IsNetGame() ? pUnit->GetPlayer() : pUnit->GetParty() );
	return it == generals.end() ? 0 : it->second.GetPtr();
}

void CSupremeBeing::AddReinforcement( class CAIUnit *pUnit )
{
	if ( generals.find( pUnit->GetPlayer() ) != generals.end() )
	{
		generals[pUnit->GetPlayer()]->Give( pUnit );
	}
}

void CSupremeBeing::Segment()
{
	if ( theDipl.IsNetGame() || !NGlobal::GetVar( "temp.nogeneral_sript", 0 ) )
	{
		// Empty in a net game without bots, so such a match runs exactly as before.
		{
			// запуск отложенных заждач
			for ( DelayedTasks::iterator it = delayedTasks.begin(); it != delayedTasks.end(); )
			{
				IGeneralDelayedTask *pTask = *it;
				if ( pTask && pTask->IsTimeToRun() )
				{
					pTask->Run();
					it = delayedTasks.erase( it );
				}
				else
				{
					++it;
				}
			}

			// вызывать не каждый сегмент и разнести по сегментам.
			for ( Generals::iterator it = generals.begin(); it != generals.end(); ++it )
			{
				it->second->Segment();
			}
		}
	}
}

void CSupremeBeing::RegisterDelayedTask( IGeneralDelayedTask *pTask )
{
	delayedTasks.push_back( pTask );
}

void CSupremeBeing::UpdateEnemyUnitInfo( CAIUnitInfoForGeneral *pInfo,
                                         const NTimer::STime lastVisibleTimeDelta,
                                         const CVec2 &vLastVisiblePos,
                                         const NTimer::STime lastAntiArtTimeDelta,
                                         const CVec2 &vLastVisibleAntiArtCenter,
                                         const float fDistToLastVisibleAntiArt )
{
	const int nGeneralParty = 1 - pInfo->GetOwner()->GetParty();

	for ( Generals::iterator it = generals.begin(); it != generals.end(); ++it )
	{
		if ( it->second->GetParty() == nGeneralParty )
		{
			it->second->UpdateEnemyUnitInfo( pInfo,
			                                 lastVisibleTimeDelta,
			                                 vLastVisiblePos,
			                                 lastAntiArtTimeDelta,
			                                 vLastVisibleAntiArtCenter,
			                                 fDistToLastVisibleAntiArt );
		}
	}
}

void CSupremeBeing::UnitDied( class CCommonUnit *pUnit )
{
	for ( Generals::iterator it = generals.begin(); it != generals.end(); ++it )
	{
		it->second->UnitDied( pUnit );
	}
}

void CSupremeBeing::UnitDied( CAIUnitInfoForGeneral *pInfo )
{
	for ( Generals::iterator it = generals.begin(); it != generals.end(); ++it )
	{
		it->second->UnitDied( pInfo );
	}
}

void CSupremeBeing::UnitChangedPosition( class CCommonUnit *pUnit, const CVec2 &vNewPos )
{
	if ( AICellsTiles::GetGeneralCell( pUnit->GetCenterPlain() ) != AICellsTiles::GetGeneralCell( vNewPos ) )
	{
		if ( CGeneral *pGeneral = GetGeneralOfUnit( pUnit ) )
		{
			pGeneral->UnitChangedPosition( pUnit, vNewPos );
		}
	}
}

void CSupremeBeing::UnitAskedForResupply( class CCommonUnit *pUnit, const EResupplyType eType, const bool bSet )
{
	if ( !pScripts || IsIronman( pScripts->GetScriptID( pUnit ) ) )
	{
		return;
	}

	if ( pUnit->GetParty() == theDipl.GetNeutralParty() )
	{
		for ( Generals::iterator it = generals.begin(); it != generals.end(); ++it )
		{
			it->second->UnitAskedForResupply( pUnit, eType, bSet );
		}
	}
	else
	{
		if ( CGeneral *pGeneral = GetGeneralOfUnit( pUnit ) )
		{
			pGeneral->UnitAskedForResupply( pUnit, eType, bSet );
		}
	}
}

void CSupremeBeing::UnitChangedParty( CAIUnit *pUnit, const int nNewParty )
{
	//	const int nGeneralParty = 1 - pUnit->GetParty();
	//if ( nGeneralParty == 1 && generals.find( nGeneralParty ) != generals.end() )
	//generals[nGeneralParty]->UnitChangedParty( pUnit, nNewParty );
	for ( Generals::iterator it = generals.begin(); it != generals.end(); ++it )
	{
		it->second->UnitChangedParty( pUnit, nNewParty );
	}
}

bool CSupremeBeing::IsInResistanceCircle( const CVec2 &vPoint, const int nGeneralParty )
{
	for ( Generals::iterator it = generals.begin(); it != generals.end(); ++it )
	{
		if ( it->second->GetParty() == nGeneralParty && it->second->IsInResistanceCircle( vPoint ) )
		{
			return true;
		}
	}
	return false;
}
