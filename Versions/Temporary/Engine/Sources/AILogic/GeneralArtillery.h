
#pragma once

#include "GeneralInternalInterfaces.h"

class CAntiArtillery;
class CAIUnit;
class CGeneralArtillery;
class CGeneral;

class CGeneralArtilleryGoToPosition : public CAIObjectBase
{
	OBJECT_BASIC_METHODS( CGeneralArtilleryGoToPosition );

	enum EBombardmentState
	{
		EBS_START,
		EBS_MOVING,
		EBS_WAIT_FOR_TRUCK,
		EBS_MOVING_WITH_TRUCK,
		EBS_FINISHING
	};

	ZDATA
	CPtr<CAIUnit> pUnit;
	// Defaults cover construction before Init/load; explicit constructor values still take precedence.
	EBombardmentState eState = {};

	CVec2 vPos;
	bool bToReservePosition = {};
	bool bFinished = {};
	NTimer::STime timeOfFinish = {};
	NTimer::STime startTime = {};

	ZEND int operator&( IBinSaver &f ) override
	{
		f.Add( 2, &pUnit );
		f.Add( 3, &eState );
		f.Add( 4, &vPos );
		f.Add( 5, &bToReservePosition );
		f.Add( 6, &bFinished );
		f.Add( 7, &timeOfFinish );
		f.Add( 8, &startTime );
		return 0;
	}

	//
	void StartState();
	void WaitForTruck();
	void MovingWithTruck();
	void Finishing();

public:
	CGeneralArtilleryGoToPosition() {}

	CGeneralArtilleryGoToPosition( CAIUnit *pUnit, const CVec2 &vPos, bool bToReservePosition );

	void Segment();

	bool IsFinished()
	{
		return bFinished;
	}

	bool DoesGoToReservePosition() const
	{
		return bToReservePosition;
	}
};

class CGeneralArtilleryTask
{

	struct SBombardmentUnitState
	{
	public:
		ZDATA
		CVec2 vReservePosition;
		CVec2 vAttackPos;

		CPtr<CAIUnit> pUnit;
		CPtr<CGeneralArtilleryGoToPosition> pGoToPosition;

		ZEND int operator&( IBinSaver &f )
		{
			f.Add( 2, &vReservePosition );
			f.Add( 3, &vAttackPos );
			f.Add( 4, &pUnit );
			f.Add( 5, &pGoToPosition );
			return 0;
		}

	public:
		SBombardmentUnitState() {}

		explicit SBombardmentUnitState( CAIUnit *pUnit );
	};

	enum EBombardmentState
	{
		EBS_START,
		EBS_ROTATING,
		EBS_GOING_TO_BATTLE,
		EBS_FIRING,
		EBS_ESCAPING
	};

	ZDATA
	CPtr<CGeneralArtillery> pOwner;

	bool bBombardmentFinished = {};

	CVec2 vBombardmentCenter;
	float fBombardmentRadius = {};
	NTimer::STime timeToFinishBombardment = {};
	NTimer::STime timeToSendAntiArtilleryAck = {};
	CVec2 vAntiArtilleryAckCenter;
	bool bIsAntiArtilleryFight = {};
	NTimer::STime startRotatingTime = {};

	EBombardmentState eState = {};

	std::list<SBombardmentUnitState> bombardmentUnits;

	int nCellNumber = {};

public:
	ZEND int operator&( IBinSaver &f )
	{
		f.Add( 2, &pOwner );
		f.Add( 3, &bBombardmentFinished );
		f.Add( 4, &vBombardmentCenter );
		f.Add( 5, &fBombardmentRadius );
		f.Add( 6, &timeToFinishBombardment );
		f.Add( 7, &timeToSendAntiArtilleryAck );
		f.Add( 8, &vAntiArtilleryAckCenter );
		f.Add( 9, &bIsAntiArtilleryFight );
		f.Add( 10, &startRotatingTime );
		f.Add( 11, &eState );
		f.Add( 12, &bombardmentUnits );
		f.Add( 13, &nCellNumber );
		return 0;
	}

	//
	// начало обстрела - дать команды подцепиться к грузовикам и ехать
	void StartBombardment();
	// как только приехали, дать команду развернуться к врагу
	void GoingToBattle();
	// как только развернулись, дать команду стрелять
	void Rotating();
	// когда отстрелялись, дать команду уезжать на резервные позиции
	void Firing();
	// когда приехали на резервные позиции, закончить артобстрел
	void Escaping();
	// если часть юнитов по какой-то причине не смогла провести артобстрел, проследить,
	// чтобы они вернулись на резервные позиции
	void CheckEscapingUnits();

	void CalculateTimeToSendAntiArtilleryAck();

	void SetBombardmentFinished();

public:
	CGeneralArtilleryTask()
		: bBombardmentFinished( true )
		, bIsAntiArtilleryFight( false )
	{
	}

	CGeneralArtilleryTask( CGeneralArtillery *pOwner,
	                       std::list<CAIUnit *> &givenUnits,
	                       bool bAntiArtilleryFight,
	                       const CVec2 &vCenter,
	                       const float fRadius,
	                       const int nCellNumber );

	void Segment();

	bool IsTaskFinished() const
	{
		return bBombardmentFinished;
	}
};

class CGeneralArtillery : public CAIObjectBase, public IEnemyEnumerator
{
	OBJECT_BASIC_METHODS( CGeneralArtillery );

	typedef std::list<CPtr<CAIUnit>> CUnitsList;

	ZDATA
	CPtr<CGeneral> pOwner;
	CUnitsList freeUnits;
	CUnitsList trucks;

	std::list<CGeneralArtilleryTask> tasks;

public:
	ZEND int operator&( IBinSaver &f ) override
	{
		f.Add( 2, &pOwner );
		f.Add( 3, &freeUnits );
		f.Add( 4, &trucks );
		f.Add( 5, &tasks );
		return 0;
	}

	void OnSerialize( IBinSaver &saver );

public:
	CGeneralArtillery() {}

	CGeneralArtillery( CGeneral *pGeneralOwner );

	// дать генералу юнит для использования в качестве артиллерии
	void TakeArtillery( CAIUnit *pUnit );
	// the same for trucks
	void TakeTruck( CAIUnit *pUnit );

	void Segment();
	bool CanBombardRegion( const CVec2 &vRegionCenter );

	int RequestForSupport( const CVec2 &vCenter, const float fRadius, bool bIsAntiArtilleryFight, const int nCellNumber );
	void CancelRequest( int nRequestID, enum EForceType eType );

	void SetEnemyContainer( IEnemyContainer *pEnemyContainer );

	//IEnemyEnumerator
	bool EnumEnemy( class CAIUnit *pEnemy ) override;

	const int GetNFreeUnits() const
	{
		return freeUnits.size();
	}

	void SetCellInUse( const int nResistanceCell, bool bInUse );
};

// ****
// Net game bots. The single player general only fires guns that a mission placed at prepared
// positions. On a multiplayer map guns arrive far from any fight, so a bot has to bring them
// within range itself: by tractor, under their own power, or not at all if they can't move.
// ****
class CGeneralBotArtillery : public CAIObjectBase, public IEnemyEnumerator
{
	OBJECT_BASIC_METHODS( CGeneralBotArtillery );

	enum EGunRole
	{
		GR_INDIRECT, // howitzers, mortars, rockets: far behind the front, firing at areas
		GR_DIRECT,   // anti-tank guns and heavy machine guns: close behind the front, fire by themselves
		GR_ANTIAIR,  // stands at a point of ours
	};

	enum EGunState
	{
		GS_READY,
		GS_MOVING,
		GS_AIMING,
		GS_FIRING,
	};

	struct SGun
	{
		ZDATA
		CPtr<CAIUnit> pGun;
		CPtr<CAIUnit> pTractor;
		EGunState eState = GS_READY;
		NTimer::STime timeStateEnd = {};
		NTimer::STime timeNextFire = {};
		NTimer::STime timeNextMove = {};
		CVec2 vFirePoint;
		CVec2 vDestination;
		bool bAtRealTarget = {};
		int nFailedMoves = {};

		ZEND int operator&( IBinSaver &f )
		{
			f.Add( 2, &pGun );
			f.Add( 3, &pTractor );
			f.Add( 4, &eState );
			f.Add( 5, &timeStateEnd );
			f.Add( 6, &timeNextFire );
			f.Add( 7, &timeNextMove );
			f.Add( 8, &vFirePoint );
			f.Add( 9, &vDestination );
			f.Add( 10, &bAtRealTarget );
			f.Add( 11, &nFailedMoves );
			return 0;
		}
	};

	ZDATA
	CPtr<CGeneral> pOwner;
	std::vector<SGun> guns;
	std::vector<CPtr<CAIUnit>> tractors;

	ZEND int operator&( IBinSaver &f ) override
	{
		f.Add( 2, &pOwner );
		f.Add( 3, &guns );
		f.Add( 4, &tractors );
		return 0;
	}

	std::vector<CVec2> resistanceCenters; // refilled every segment

	static EGunRole GetRole( const CAIUnit *pGun );
	static float GetRange( CAIUnit *pGun );
	static float GetMinRange( CAIUnit *pGun );
	static bool CanRelocateByItself( CAIUnit *pGun );

	bool FindFocus( const SGun &gun, EGunRole eRole, CVec2 *pFocus ) const;
	bool FindFirePoint( const SGun &gun, CVec2 *pPoint, bool *pbRealTarget ) const;
	CAIUnit *FindTractor( const SGun &gun ) const;
	void MoveGun( SGun *pGun, const CVec2 &vDestination );
	void SegmentReady( SGun *pGun );
	void SegmentMoving( SGun *pGun );
	void SegmentAiming( SGun *pGun );
	void SegmentFiring( SGun *pGun );

public:
	CGeneralBotArtillery() {}

	CGeneralBotArtillery( CGeneral *_pOwner );

	// Guns, and tractors that move them
	static bool TakesUnit( CAIUnit *pUnit );
	void Take( CAIUnit *pUnit );
	void Segment();

	int GetGunCount() const
	{
		return guns.size();
	}

	// IEnemyEnumerator
	bool EnumEnemy( class CAIUnit *pEnemy ) override
	{
		return false;
	}

	bool EnumResistances( const struct SResistance &resistance ) override;
};
