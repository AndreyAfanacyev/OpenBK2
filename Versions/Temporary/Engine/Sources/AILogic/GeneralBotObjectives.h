#pragma once

// How a net game bot splits its units between objectives. Kept apart from the
// units themselves so that the rules can be tested without a running simulation.
// Runs inside the deterministic simulation: no unordered containers, and ties
// always go to the lower index.

#include <algorithm>
#include <vector>

namespace NBotObjectives
{
struct SWorker
{
	CVec2 vPosition;
	int nTarget = -1; // index in targets; -1 needs a target
};

inline int FindTarget( const std::vector<CVec2> &targets, const CVec2 &vTarget )
{
	for ( int i = 0; i < targets.size(); ++i )
	{
		if ( targets[i] == vTarget )
		{
			return i;
		}
	}
	return -1;
}

// No target is left without units while another has more than its share. A
// worker whose target is still valid keeps it unless that target is over its
// share, so that units don't turn back and forth every time the list changes.
inline void AssignTargets( const std::vector<CVec2> &targets, std::vector<SWorker> *pWorkers )
{
	if ( targets.empty() )
	{
		return;
	}

	std::vector<SWorker> &workers = *pWorkers;
	const int nShare = ( int( workers.size() ) + int( targets.size() ) - 1 ) / int( targets.size() );
	std::vector<int> counts( targets.size(), 0 );
	for ( int i = 0; i < workers.size(); ++i )
	{
		if ( workers[i].nTarget >= 0 )
		{
			if ( counts[workers[i].nTarget] >= nShare )
			{
				workers[i].nTarget = -1;
			}
			else
			{
				++counts[workers[i].nTarget];
			}
		}
	}

	for ( int i = 0; i < workers.size(); ++i )
	{
		if ( workers[i].nTarget >= 0 )
		{
			continue;
		}

		int nBest = -1;
		float fBest = 0.0f;
		for ( int j = 0; j < targets.size(); ++j )
		{
			if ( counts[j] < nShare )
			{
				const float fDistance = fabs2( targets[j] - workers[i].vPosition );
				if ( nBest < 0 || fDistance < fBest )
				{
					nBest = j;
					fBest = fDistance;
				}
			}
		}
		workers[i].nTarget = nBest;
		++counts[nBest];
	}
}

// ---- Garrisons ----

// Units left to hold each captured point. A small army can't spare more than one, and
// a third of the army at most stays behind, so that there is still something to attack with.
inline int GarrisonPerPoint( const int nUnits, const int nOwnPoints )
{
	if ( nOwnPoints <= 0 )
	{
		return 0;
	}
	const int nWanted = nUnits >= 8 ? 2 : 1;
	return (std::min)( nWanted, nUnits / ( 3 * nOwnPoints ) );
}

// ---- Waves ----

const int WAVE_SIZE_CONTESTED = 6;
const int WAVE_SIZE_MINIMUM = 3;
const int WAVE_SIZE_FREE_POINT = 2;
const int WAVE_WAIT_FREE_POINT = 15000;
const int WAVE_WAIT_SHORT = 60000;
const int WAVE_WAIT_LONG = 120000;

// When units gathered at the rally point set off. A point about to be lost can't wait, nobody
// has to wait long for a point nobody defends, and a defended one is worth a real group, though
// not forever: a bot that never attacks because it keeps losing reinforcements is no opponent.
inline bool ShouldLaunchWave( const int nGathered, const bool bContested, const bool bUrgent, const int nWaited )
{
	if ( nGathered <= 0 )
	{
		return false;
	}
	if ( bUrgent )
	{
		return true;
	}
	if ( !bContested )
	{
		return nGathered >= WAVE_SIZE_FREE_POINT || nWaited >= WAVE_WAIT_FREE_POINT;
	}
	return nGathered >= WAVE_SIZE_CONTESTED || ( nGathered >= WAVE_SIZE_MINIMUM && nWaited >= WAVE_WAIT_SHORT ) ||
	       nWaited >= WAVE_WAIT_LONG;
}

// Share of the gathered units for one wave when several points are free to attack
inline int WaveSize( const int nGathered, const int nTargetsLeft )
{
	return nTargetsLeft <= 1 ? nGathered : (std::max)( 1, nGathered / nTargetsLeft );
}

// ---- Retreat ----

const float RETREAT_HEALTH = 0.35f;
const float RETURN_HEALTH = 0.7f;

// A wave that sees over twice its own worth in front of it pulls back and gathers again.
inline bool ShouldWaveRetreat( const float fOwnStrength, const float fEnemyStrength )
{
	return fOwnStrength > 0.0f && fEnemyStrength > fOwnStrength * 2.0f;
}

// ---- What to call next ----

enum EForceCategory
{
	FC_INFANTRY,
	FC_ARMOR,
	FC_ANTITANK,
	FC_ARTILLERY,
	FC_ANTIAIR,
	FC_COUNT,
	FC_NOT_COMBAT = FC_COUNT
};

struct SForces
{
	float own[FC_COUNT] = {};
	float enemyInfantry = 0.0f;
	float enemyArmor = 0.0f;
	float enemyAir = 0.0f;
};

// The mix a bot wants, shifted by what it has seen of the enemy.
inline void WantedMix( const SForces &forces, float *pWanted )
{
	pWanted[FC_INFANTRY] = 3.0f;
	pWanted[FC_ARMOR] = 4.0f;
	pWanted[FC_ANTITANK] = 1.0f;
	pWanted[FC_ARTILLERY] = 1.5f;
	pWanted[FC_ANTIAIR] = 0.5f;

	const float fEnemy = forces.enemyInfantry + forces.enemyArmor + forces.enemyAir;
	if ( fEnemy > 0.0f )
	{
		pWanted[FC_ANTITANK] += 3.0f * forces.enemyArmor / fEnemy;
		pWanted[FC_ARMOR] += 1.0f * forces.enemyArmor / fEnemy;
		pWanted[FC_INFANTRY] += 1.5f * forces.enemyInfantry / fEnemy;
		pWanted[FC_ARTILLERY] += 1.0f * forces.enemyInfantry / fEnemy;
		pWanted[FC_ANTIAIR] += 3.0f * forces.enemyAir / fEnemy;
	}
}

// The category furthest below its wanted share among those that can be called; ties go to the lower one.
inline int ChooseCategory( const SForces &forces, const bool *pAvailable )
{
	float wanted[FC_COUNT];
	WantedMix( forces, wanted );
	float fWantedSum = 0.0f;
	float fOwnSum = 0.0f;
	for ( int i = 0; i < FC_COUNT; ++i )
	{
		fWantedSum += wanted[i];
		fOwnSum += forces.own[i];
	}

	int nBest = FC_NOT_COMBAT;
	float fBest = 0.0f;
	for ( int i = 0; i < FC_COUNT; ++i )
	{
		if ( pAvailable[i] )
		{
			const float fShortage = wanted[i] / fWantedSum - ( fOwnSum > 0.0f ? forces.own[i] / fOwnSum : 0.0f );
			if ( nBest == FC_NOT_COMBAT || fShortage > fBest )
			{
				nBest = i;
				fBest = fShortage;
			}
		}
	}
	return nBest;
}

// Infantry counts by squads, everything else by vehicles and guns.
inline float CombatUnits( const SForces &forces )
{
	float fUnits = 0.0f;
	for ( int i = 0; i < FC_COUNT; ++i )
	{
		fUnits += forces.own[i];
	}
	return fUnits;
}

// Engineers can capture nothing by themselves, so a bot with little to fight with calls fighters first.
const float MIN_COMBAT_UNITS_FOR_ENGINEERS = 4.0f;

inline bool MayCallEngineers( const SForces &forces )
{
	return CombatUnits( forces ) >= MIN_COMBAT_UNITS_FOR_ENGINEERS;
}

// ---- Artillery ----

// Where a gun should stand to reach vFocus coming from vFrom: most of its range away, so that it
// stays behind whatever fights there, and never closer than it can fire.
inline CVec2 FirePosition( const CVec2 &vFocus, const CVec2 &vFrom, const float fRange, const float fMinRange, const float fShare )
{
	CVec2 vDir = vFrom - vFocus;
	const float fLength = fabs( vDir );
	if ( fLength < 1.0f )
	{
		vDir = CVec2( 1.0f, 0.0f );
	}
	else
	{
		vDir = vDir / fLength;
	}
	return vFocus + vDir * (std::max)( fRange * fShare, fMinRange * 1.2f );
}

inline bool IsAtFireDistance( const float fDistance, const float fRange, const float fMinRange )
{
	return fDistance <= fRange * 0.95f && fDistance >= fMinRange * 1.05f;
}
} // namespace NBotObjectives
