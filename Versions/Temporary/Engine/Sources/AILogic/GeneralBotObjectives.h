#pragma once

// How a net game bot splits its units between objectives. Kept apart from the
// units themselves so that the rules can be tested without a running simulation.
// Runs inside the deterministic simulation: no unordered containers, and ties
// always go to the lower index.

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
} // namespace NBotObjectives
