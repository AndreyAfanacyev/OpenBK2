
#pragma once

#include "GeneralInternalInterfaces.h"
#include "Resistance.h"
#include "Stats_B2_M1/DBMapInfo.h"

// ****
// защита патча.
// ****
class CGeneral;

class CGeneralTaskToDefendPatch : public IGeneralTask, public IWorkerEnumerator, public IEnemyEnumerator
{
	OBJECT_BASIC_METHODS( CGeneralTaskToDefendPatch );

	ZDATA
	CPtr<CGeneral> pOwner;

	NDb::SAIGeneralParcel patchInfo;
	// Defaults cover construction before Init/load; explicit constructor values still take precedence.
	int nCurReinforcePoint = {};

	float fSeverity = {};                                                   // current severity of this task
	float fEnemyForce = {}, fFriendlyForce = {}, fFriendlyMobileForce = {}; //
	float fMaxSeverity = {};

	bool bFinished = {};
	bool bWaitForFinish = {}; // wait for finish the task
	NTimer::STime timeLastUpdate = {};

	CommonUnits infantryInTrenches; // списки юнитов по типам
	CommonUnits infantryFree;
	CommonUnits tanksMobile;
	CommonUnits stationaryUnits;
	CommonUnits enemyForces;
	bool bResistanceRemoved = {};

	CommonUnits newTanks; // to take tanks not 1 by 1

	ZEND int operator&( IBinSaver &f ) override
	{
		f.Add( 2, &pOwner );
		f.Add( 3, &patchInfo );
		f.Add( 4, &nCurReinforcePoint );
		f.Add( 5, &fSeverity );
		f.Add( 6, &fEnemyForce );
		f.Add( 7, &fFriendlyForce );
		f.Add( 8, &fFriendlyMobileForce );
		f.Add( 9, &fMaxSeverity );
		f.Add( 10, &bFinished );
		f.Add( 11, &bWaitForFinish );
		f.Add( 12, &timeLastUpdate );
		f.Add( 13, &infantryInTrenches );
		f.Add( 14, &infantryFree );
		f.Add( 15, &tanksMobile );
		f.Add( 16, &stationaryUnits );
		f.Add( 17, &enemyForces );
		f.Add( 18, &bResistanceRemoved );
		f.Add( 19, &newTanks );
		return 0;
	}

	void CalcSeverity( const bool bEnemyUpdated, const bool bFriendlyUpdated );
	void InitTanks( class CCommonUnit *pUnit );
	void InitInfantryInTrenches( class CCommonUnit *pUnit );

public:
	CGeneralTaskToDefendPatch();
	void Init( const struct NDb::SAIGeneralParcel &_patchInfo, class CGeneral *pOwner );

	//ITask
	ETaskName GetName() const override;
	void AskForWorker( ICommander *pManager, const float _fMaxSeverity, const bool bInit = false ) override;
	void ReleaseWorker( ICommander *pManager, const float _fMinSeverity ) override;
	float GetSeverity() const override;
	bool IsFinished() const override;
	void CancelTask( ICommander *pManager ) override;
	void Segment() override;

	//IEnumerator
	bool EnumWorker( class CCommonUnit *pUnit, const enum EForceType eType ) override;
	bool EvaluateWorker( CCommonUnit *pUnit, const enum EForceType eType ) const override;

	//IEnemyEnumerator
	bool EnumEnemy( class CAIUnit *pEnemy ) override;

	int GetWorkerCount() override;
};

// ****
// hold reinforcemen
// ****
class CGeneralTaskToHoldReinforcement : public IGeneralTask, public IWorkerEnumerator
{
	OBJECT_BASIC_METHODS( CGeneralTaskToHoldReinforcement );
	typedef det_map<int, CVec2> UnitsPositions;
	ZDATA
	CommonUnits tanksFree;
	NDb::SAIGeneralParcel patchInfo;
	UnitsPositions unitsPositions;

	float fSeverity = {}; // current severity of this task
	int nCurReinforcePoint = {};

	ZEND int operator&( IBinSaver &f ) override
	{
		f.Add( 2, &tanksFree );
		f.Add( 3, &patchInfo );
		f.Add( 4, &unitsPositions );
		f.Add( 5, &fSeverity );
		f.Add( 6, &nCurReinforcePoint );
		return 0;
	}

public:
	CGeneralTaskToHoldReinforcement();
	void Init( const struct NDb::SAIGeneralParcel &_patchInfo );

	//empty reinforcement parcell. uses unit's initial positions as reinforcements positions.
	void Init()
	{
		fSeverity = 0;
	}

	//ITask
	ETaskName GetName() const override
	{
		return ETN_HOLD_REINFORCEMENT;
	}

	void AskForWorker( ICommander *pManager, const float fMaxSeverity, const bool bInit = false ) override;
	void ReleaseWorker( ICommander *pManager, const float fMinSeverity ) override;
	float GetSeverity() const override;

	bool IsFinished() const override
	{
		return false;
	}

	void CancelTask( ICommander *pManager ) override;
	void Segment() override;

	int NeedNBest( const enum EForceType eType ) const override
	{
		return true;
	}

	//IEnumerator
	bool EnumWorker( class CCommonUnit *pUnit, const enum EForceType eType ) override;
	bool EvaluateWorker( CCommonUnit *pUnit, const enum EForceType eType ) const override;
	int GetWorkerCount() override;
};

class CGeneralTaskRecaptureStorage : public IGeneralTask, public IWorkerEnumerator
{
	OBJECT_BASIC_METHODS( CGeneralTaskRecaptureStorage );
	ZDATA
	CommonUnits tanksFree;
	CVec2 vReinforcePoint;
	float fSeverity = {}; // current severity of this task

	bool bFinished = {};

	ZEND int operator&( IBinSaver &f ) override
	{
		f.Add( 2, &tanksFree );
		f.Add( 3, &vReinforcePoint );
		f.Add( 4, &fSeverity );
		f.Add( 5, &bFinished );
		return 0;
	}

public:
	CGeneralTaskRecaptureStorage() {}

	CGeneralTaskRecaptureStorage( const CVec2 &vReinforcePoint );

	//ITask
	ETaskName GetName() const override
	{
		return ETN_HOLD_REINFORCEMENT;
	}

	void AskForWorker( ICommander *pManager, const float fMaxSeverity, const bool bInit = false ) override;
	void ReleaseWorker( ICommander *pManager, const float fMinSeverity ) override;

	float GetSeverity() const override
	{
		return fSeverity;
	}

	bool IsFinished() const override;
	void CancelTask( ICommander *pManager ) override;
	void Segment() override;

	//IEnumerator
	bool EnumWorker( class CCommonUnit *pUnit, const enum EForceType eType ) override;
	bool EvaluateWorker( CCommonUnit *pUnit, const enum EForceType eType ) const override;
};

class CGeneralTaskToSwarmToPoint : public IGeneralTask, public IWorkerEnumerator, public IEnemyEnumerator
{
	OBJECT_BASIC_METHODS( CGeneralTaskToSwarmToPoint );

	enum ESwarmState
	{
		ESS_PREPEARING,
		ESS_ATTACKING,
		ESS_REST,
		ESS_WAIT_FOR_WORKERS,
	};

	typedef std::vector<CPtr<CCommonUnit>> CTanks;

	ZDATA
	ESwarmState eState = {};
	CVec2 vPrepearCenter;

	SResistance curResistanceToAttack;
	float fSeverity = {}; // severity of this task
	float fMaxSeverity = {};
	float fMinSeverity = {};
	int nAdditionalIterations = {}; // number of additional iterations to try swarm to
	bool bFinished = {};
	bool bReleaseWorkers = {}; // this task does not always release workers
	NTimer::STime timeNextCheck = {};

	CPtr<CGeneral> pOwner;

	CTanks swarmingTanks;
	bool bResistanesBusyByUs = {};

	CVec2 vTanksPosition;    // center of tanks formation
	float fCurDistance = {}; // distance to nearest resistansce ( during enumeration )

	ZEND int operator&( IBinSaver &f ) override
	{
		f.Add( 2, &eState );
		f.Add( 3, &vPrepearCenter );
		f.Add( 4, &curResistanceToAttack );
		f.Add( 5, &fSeverity );
		f.Add( 6, &fMaxSeverity );
		f.Add( 7, &fMinSeverity );
		f.Add( 8, &nAdditionalIterations );
		f.Add( 9, &bFinished );
		f.Add( 10, &bReleaseWorkers );
		f.Add( 11, &timeNextCheck );
		f.Add( 12, &pOwner );
		f.Add( 13, &swarmingTanks );
		f.Add( 14, &bResistanesBusyByUs );
		f.Add( 15, &vTanksPosition );
		f.Add( 16, &fCurDistance );
		return 0;
	}

	void ClearResistanceToAcceptNewTask();
	bool IsTimeToRun() const;
	void Run();
	void SendToGroupPoint();

public:
	CGeneralTaskToSwarmToPoint();
	CGeneralTaskToSwarmToPoint( class CGeneral *pOwner );

	//ITask
	ETaskName GetName() const override
	{
		return ETN_SWARM_TO_POINT;
	}

	void AskForWorker( ICommander *pManager, const float _fMaxSeverity, const bool bInit = false ) override;
	void ReleaseWorker( ICommander *pManager, const float fMinSeverity ) override;
	float GetSeverity() const override;
	bool IsFinished() const override;
	void CancelTask( ICommander *pManager ) override;
	void Segment() override;

	//IEnumerator
	bool EnumWorker( class CCommonUnit *pUnit, const enum EForceType eType ) override;
	bool EvaluateWorker( CCommonUnit *pUnit, const enum EForceType eType ) const override;

	int NeedNBest( const enum EForceType eType ) const override
	{
		return 10000;
	}

	float EvaluateWorkerRating( CCommonUnit *pUnit, const enum EForceType eType ) const override;

	bool EnumResistances( const struct SResistance &resistance ) override;

	bool EnumEnemy( class CAIUnit *pEnemy ) override
	{
		NI_ASSERT( false, "didn't asked to" );
		return false;
	}

	int GetWorkerCount() override;

	friend class CGeneralSwarmWaitForReady;
};

class CGeneralSwarmWaitForReady : public IGeneralDelayedTask
{
	OBJECT_BASIC_METHODS( CGeneralSwarmWaitForReady );

	ZDATA
	CPtr<CGeneralTaskToSwarmToPoint> pGeneralTask;

	ZEND int operator&( IBinSaver &f ) override
	{
		f.Add( 2, &pGeneralTask );
		return 0;
	}

public:
	CGeneralSwarmWaitForReady() {}

	CGeneralSwarmWaitForReady( class CGeneralTaskToSwarmToPoint *pTask )
		: pGeneralTask( pTask )
	{
	}

	bool IsTimeToRun() const override
	{
		return pGeneralTask->IsTimeToRun();
	}

	void Run() override
	{
		pGeneralTask->Run();
	}
};

// ****
// Net game bots: take the key points that decide a match, and fight what is known about on the way.
// ****
class CGeneralTaskCaptureKeyPoints : public IGeneralTask, public IWorkerEnumerator, public IEnemyEnumerator
{
	OBJECT_BASIC_METHODS( CGeneralTaskCaptureKeyPoints );

	enum EWorkerRole
	{
		WR_GATHERING, // on its way to the rally point, or waiting there for enough others
		WR_WAVE,      // attacking with its group
		WR_GARRISON,  // holding a key point the side has taken
		WR_RETREAT,   // too damaged to fight: back to the rally point, where trucks can reach it
	};

	struct SWorker
	{
		ZDATA
		CPtr<CCommonUnit> pUnit;
		CVec2 vTarget;
		bool bHasTarget = {};
		EWorkerRole eRole = WR_GATHERING;
		int nWave = -1;
		NTimer::STime timeRetreatEnd = {};
		bool bDugIn = {};

		ZEND int operator&( IBinSaver &f )
		{
			f.Add( 2, &pUnit );
			f.Add( 3, &vTarget );
			f.Add( 4, &bHasTarget );
			f.Add( 5, &eRole );
			f.Add( 6, &nWave );
			f.Add( 7, &timeRetreatEnd );
			f.Add( 8, &bDugIn );
			return 0;
		}
	};

	struct SWave
	{
		ZDATA
		int nID = {};
		CVec2 vTarget;

		ZEND int operator&( IBinSaver &f )
		{
			f.Add( 2, &nID );
			f.Add( 3, &vTarget );
			return 0;
		}
	};

	// What the task knows about one objective during a round of orders
	struct STarget
	{
		CVec2 vCenter;
		bool bContested = false; // enemies hold it or stand near it
		bool bUrgent = false;    // a point of ours under attack
	};

	// An engineer truck at work on a damaged key point
	struct SRepair
	{
		ZDATA
		CPtr<CCommonUnit> pTruck;
		int nLink = {};
		int nOrders = {}; // how many times it has been sent; a truck that can't get there gives up

		ZEND int operator&( IBinSaver &f )
		{
			f.Add( 2, &pTruck );
			f.Add( 3, &nLink );
			f.Add( 4, &nOrders );
			return 0;
		}
	};

	ZDATA
	CPtr<CGeneral> pOwner;
	std::vector<SWorker> workers;
	NTimer::STime timeNextOrders = {};
	std::vector<SRepair> repairs;
	std::vector<int> pointsToRepair; // links of damaged key points with no truck on the way
	det_map<int, NTimer::STime> unreachablePoints; // link -> when to try repairing it again
	std::vector<SWave> waves;
	int nNextWaveID = {};
	NTimer::STime timeGatheringStarted = {};
	CVec2 vRallyPoint;
	bool bHasRallyPoint = {};
	bool bGatheringStarted = {};

	ZEND int operator&( IBinSaver &f ) override
	{
		f.Add( 2, &pOwner );
		f.Add( 3, &workers );
		f.Add( 4, &timeNextOrders );
		f.Add( 5, &repairs );
		f.Add( 6, &pointsToRepair );
		f.Add( 7, &unreachablePoints );
		f.Add( 8, &waves );
		f.Add( 9, &nNextWaveID );
		f.Add( 10, &timeGatheringStarted );
		f.Add( 11, &vRallyPoint );
		f.Add( 12, &bHasRallyPoint );
		f.Add( 13, &bGatheringStarted );
		return 0;
	}

	std::vector<CVec2> resistanceCenters; // only filled while enumerating, see EnumResistances

	void CollectTargets( std::vector<STarget> *pTargets, std::vector<CVec2> *pOwnPoints );
	void UpdateRallyPoint( const std::vector<STarget> &targets, const std::vector<CVec2> &ownPoints );
	void UpdateRetreats();
	void UpdateGarrisons( const std::vector<CVec2> &ownPoints );
	void UpdateWaves( const std::vector<STarget> &targets );
	void LaunchWaves( const std::vector<STarget> &targets );
	void SendWorkers();
	void GiveOrders();
	void UpdateRepairs();
	float GetEnemyStrengthNear( const CVec2 &vCenter, const float fRadius ) const;

public:
	CGeneralTaskCaptureKeyPoints() {}

	CGeneralTaskCaptureKeyPoints( class CGeneral *_pOwner )
		: pOwner( _pOwner )
	{
	}

	ETaskName GetName() const override
	{
		return ETN_CAPTURE_KEY_POINTS;
	}

	void AskForWorker( ICommander *pManager, const float fMaxSeverity, const bool bInit = false ) override;

	// Nothing to give back: on a multiplayer map every unit has a key point to go to
	void ReleaseWorker( ICommander *pManager, const float fMinSeverity ) override {}

	float GetSeverity() const override
	{
		return 0.0f;
	}

	bool IsFinished() const override
	{
		return false;
	}

	void CancelTask( ICommander *pManager ) override;
	void Segment() override;

	int GetWorkerCount() override
	{
		return workers.size();
	}

	// IWorkerEnumerator
	bool EnumWorker( class CCommonUnit *pUnit, const enum EForceType eType ) override;

	bool EvaluateWorker( CCommonUnit *pUnit, const enum EForceType eType ) const override;

	// Asking for "the best several" makes the commander sort by rating. Unique IDs leave
	// no ties for std::sort to order differently from one platform to another.
	// Trucks go one at a time, the nearest to the point waiting for repair.
	int NeedNBest( const enum EForceType eType ) const override
	{
		return eType == FT_TRUCK_REPAIR_BUILDING ? 1 : 2;
	}

	float EvaluateWorkerRating( CCommonUnit *pUnit, const enum EForceType eType ) const override;

	// IEnemyEnumerator
	bool EnumEnemy( class CAIUnit *pEnemy ) override
	{
		return false;
	}

	bool EnumResistances( const struct SResistance &resistance ) override;
};
