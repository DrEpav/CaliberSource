//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
// STORY CHARACTER
// Purpose: A humble, god-fearing dixie man that got (un)lucky enough to be in the same 
//			plane as Joe.
//
//=============================================================================//
//
// Base behavior files
#include "cbase.h"
#include "ai_default.h"
#include "ai_task.h"
#include "ai_schedule.h"
#include "ai_node.h"
#include "ai_hull.h"
#include "ai_hint.h"
#include "ai_squad.h"
#include "ai_senses.h"
#include "ai_navigator.h"
#include "ai_motor.h"
#include "ai_behavior.h"
#include "ai_baseactor.h"
#include "ai_behavior_lead.h"
#include "ai_behavior_follow.h"
#include "ai_behavior_standoff.h"
#include "ai_behavior_assault.h"
#include "ai_behavior_functank.h"
// Base engine files
#include "soundent.h"
#include "game.h"
#include "npcevent.h"
//#include "entitylist.h"
#include "activitylist.h"
//#include "ai_basenpc.h"
#include "engine/IEngineSound.h"
#include "vstdlib/random.h"
#include "sceneentity.h"
// Extras
#include "npc_playercompanion.h"
#include "info_darknessmode_lightsource.h"
#include "weapon_flaregun.h"

#include "env_debughistory.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Define stuff
#define SOUTH_MODEL "models/Barney.mdl"

#define SOUTH_MIN_MOB_DIST_SQR Square(120)		// Any enemy closer than this adds to the 'mob' 
#define SOUTH_MIN_CONSIDER_DIST Square(1200)	// Only enemies within this range are counted and considered to generate AI speech

#define SOUTH_MIN_ENEMY_DIST_TO_CROUCH			384			// Minimum distance that enemy must be to crouch
#define SOUTH_MIN_ENEMY_HEALTH_TO_CROUCH			20 		// Dont crouch for small enemies like headcrabs and the like
#define SOUTH_CROUCH_DELAY						5			// Time after crouching before South will consider crouching again

ConVar	sk_south_health( "sk_south_health","0");

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
class CNPC_South : public CNPC_PlayerCompanion
{
public:
	DECLARE_CLASS( CNPC_South, CNPC_PlayerCompanion );
	DECLARE_SERVERCLASS();

	virtual void Precache()
	{
		// Prevents a warning
		SelectModel( );
		BaseClass::Precache();

		// Reusing boston's sounds for now
		PrecacheScriptSound( "npc_boston.FootstepLeft" );
		PrecacheScriptSound( "npc_boston.FootstepRight" );
		PrecacheScriptSound( "npc_boston.die" );

		PrecacheInstancedScene( "scenes/Expressions/TomIdle.vcd" );
		PrecacheInstancedScene( "scenes/Expressions/TomAlert.vcd" );
		PrecacheInstancedScene( "scenes/Expressions/TomCombat.vcd" );
	}

	void	Spawn( void );
	void	SelectModel();
	Class_T Classify( void );
	void	Weapon_Equip( CBaseCombatWeapon *pWeapon );

	bool CreateBehaviors( void );
	void Activate();
	void PrescheduleThink( void );

	void HandleAnimEvent( animevent_t *pEvent );
	int	GetSoundInterests ( void );
	bool ShouldLookForBetterWeapon() { return true; }

	void OnChangeRunningBehavior( CAI_BehaviorBase *pOldBehavior,  CAI_BehaviorBase *pNewBehavior );

	void DeathSound( const CTakeDamageInfo &info );
	void PainSound( const CTakeDamageInfo &info );
	
	void GatherConditions();
	void UseFunc( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );
	
	bool	OnBeginMoveAndShoot();
	void	SpeakAttacking( void );
	void	EnemyIgnited( CAI_BaseNPC *pVictim );
	virtual void	BarnacleDeathSound( void );

	// Custom AI
	void	DoCustomCombatAI( void );
	void	DoMobbedCombatAI( void );
//	void	DoCustomSpeechAI( void );

	// Blinding
	virtual void PlayerHasIlluminatedNPC( CBasePlayer *pPlayer, float flDot );
	void		 CheckBlindedByFlare( void );
	bool		 CanBeBlindedByFlashlight( bool bCheckLightSources );
	bool		 PlayerFlashlightOnMyEyes( CBasePlayer *pPlayer );
	bool		 BlindedByFlare( void );

	PassengerState_e	GetPassengerState( void );
	bool	RunningPassengerBehavior( void );

	CAI_FuncTankBehavior		m_FuncTankBehavior;
	COutputEvent				m_OnPlayerUse;
	
private:
	CHandle<CAI_Hint>	m_hStealthLookTarget;
	float   m_fCombatStartTime;
	float	m_fCombatEndTime;
	float	m_flNextCrouchTime;
	bool	m_bIsFlashlightBlind;
	float	m_fStayBlindUntil;
	float	m_flDontBlindUntil;
	string_t m_iszCurrentBlindScene;
	WeaponProficiency_t CalcWeaponProficiency( CBaseCombatWeapon *pWeapon );
	
	DECLARE_DATADESC();
	DEFINE_CUSTOM_AI;
};

LINK_ENTITY_TO_CLASS( npc_south, CNPC_South );
//IMPLEMENT_CUSTOM_AI( npc_conscript, CNPC_South );

//---------------------------------------------------------
IMPLEMENT_SERVERCLASS_ST(CNPC_South, DT_NPC_South)
END_SEND_TABLE()

//---------------------------------------------------------
// Save/Restore
//---------------------------------------------------------
BEGIN_DATADESC( CNPC_South )
//					m_FuncTankBehavior
	DEFINE_FIELD( m_fCombatStartTime, FIELD_TIME ),
	DEFINE_FIELD( m_fCombatEndTime, FIELD_TIME ),
	DEFINE_FIELD( m_flNextCrouchTime, FIELD_TIME ),
	DEFINE_FIELD( m_hStealthLookTarget, FIELD_EHANDLE ),
	DEFINE_FIELD( m_bIsFlashlightBlind, FIELD_BOOLEAN ),
	DEFINE_FIELD( m_fStayBlindUntil, FIELD_TIME ),
	DEFINE_FIELD( m_flDontBlindUntil, FIELD_TIME ),
	DEFINE_FIELD( m_iszCurrentBlindScene, FIELD_STRING ),

	DEFINE_OUTPUT( m_OnPlayerUse, "OnPlayerUse" ),

	DEFINE_USEFUNC( Use ),
	
END_DATADESC()

//-----------------------------------------------------------------------------
// Classify - indicates this NPC's place in the 
// relationship table.
//-----------------------------------------------------------------------------
Class_T	CNPC_South::Classify( void )
{
	return	CLASS_PLAYER_ALLY_VITAL;
}

void CNPC_South::Activate()
{
	// Avoids problems with players saving the game in places where he dies immediately afterwards.
	m_iHealth = sk_south_health.GetFloat();
	
	BaseClass::Activate();
	
	// Assume tom has already said hello
	SetSpokeConcept( TLK_HELLO, NULL, false );
}

//-----------------------------------------------------------------------------
// HandleAnimEvent - catches the NPC-specific messages
// that occur when tagged animation frames are played.
//-----------------------------------------------------------------------------
void CNPC_South::HandleAnimEvent( animevent_t *pEvent )
{
	
	switch( pEvent->event )
	{
	case NPC_EVENT_LEFTFOOT:
		{
			EmitSound( "npc_boston.FootstepLeft", pEvent->eventtime );
		}
		break;
	case NPC_EVENT_RIGHTFOOT:
		{
			EmitSound( "npc_boston.FootstepRight", pEvent->eventtime );
		}
		break;

	default:
		BaseClass::HandleAnimEvent( pEvent );
		break;
	}
}

//---------------------------------------------------------------------------------
// GetSoundInterests - South can hear some things, but will only comment about it.
//---------------------------------------------------------------------------------
int CNPC_South::GetSoundInterests( void )
{
	return	SOUND_WORLD | SOUND_COMBAT | SOUND_PLAYER | SOUND_DANGER | SOUND_PHYSICS_DANGER | SOUND_BULLET_IMPACT | SOUND_MOVE_AWAY;
}

//-----------------------------------------------------------------------------
// Spawn
//-----------------------------------------------------------------------------
void CNPC_South::Spawn()
{
	// Allow custom model usage (mostly for monitors)
//	char *szModel = (char *)STRING( GetModelName() );
//	if (!szModel || !*szModel)
//	{
//		szModel = "models/South.mdl";
//		SetModelName( AllocPooledString(szModel) );
//	}

	Precache();

	BaseClass::Spawn();

	SetHullType(HULL_HUMAN);
	SetHullSizeNormal();

	SetSolid( SOLID_BBOX );
	AddSolidFlags( FSOLID_NOT_STANDABLE );
	SetMoveType( MOVETYPE_STEP );
	SetBloodColor( BLOOD_COLOR_RED );
	
	m_iszIdleExpression = MAKE_STRING("scenes/Expressions/TomIdle.vcd");
	m_iszAlertExpression = MAKE_STRING("scenes/Expressions/TomAlert.vcd");
	m_iszCombatExpression = MAKE_STRING("scenes/Expressions/TomCombat.vcd");
	
	m_iHealth			= sk_south_health.GetFloat();
	m_flFieldOfView		= 0.2;// indicates the width of this NPC's forward view cone ( as a dotproduct result )
	m_NPCState			= NPC_STATE_NONE;
	
	// Basic Capabilities are defined in the base AI
	CapabilitiesAdd( bits_CAP_MOVE_JUMP | bits_CAP_OPEN_DOORS );
	CapabilitiesAdd( bits_CAP_MOVE_SHOOT );

	AddEFlags( EFL_NO_DISSOLVE | EFL_NO_MEGAPHYSCANNON_RAGDOLL | EFL_NO_PHYSCANNON_INTERACTION );

	NPCInit();
	
	SetUse( &CNPC_South::UseFunc );
	
	m_fCombatStartTime = 0.0f;
	m_fCombatEndTime   = 0.0f;

	// Dont set too low, otherwise he wont shut-up
	m_AnnounceAttackTimer.Set( 4, 8 );
}

void CNPC_South::PainSound( const CTakeDamageInfo &info )
{
	SpeakIfAllowed( TLK_SHOT );
}

void CNPC_South::DeathSound( const CTakeDamageInfo &info )
{
	// Sentences don't play on dead NPCs
	SentenceStop();

	EmitSound( "npc_south.die" );
}

void CNPC_South::BarnacleDeathSound( void )
{
	Speak( TLK_SELF_IN_BARNACLE );
}

//-----------------------------------------------------------------------------
// Precache - precaches all resources/functions this NPC needs
//-----------------------------------------------------------------------------
void CNPC_South::SelectModel()
{
	SetModelName( AllocPooledString( SOUTH_MODEL ) );
}

void CNPC_South::Weapon_Equip( CBaseCombatWeapon *pWeapon )
{
	BaseClass::Weapon_Equip( pWeapon );

	if( hl2_episodic.GetBool() && FClassnameIs( pWeapon, "weapon_smg1" ) )
	{
		pWeapon->m_fMinRange1 = 0.0f;
	}
}

bool CNPC_South::CreateBehaviors( void )
{
	AddBehavior( &m_FuncTankBehavior );
	BaseClass::CreateBehaviors();

	return true;
}

void CNPC_South::OnChangeRunningBehavior( CAI_BehaviorBase *pOldBehavior,  CAI_BehaviorBase *pNewBehavior )
{
	if ( pNewBehavior == &m_FuncTankBehavior )
	{
		m_bReadinessCapable = false;
	}
	else if ( pOldBehavior == &m_FuncTankBehavior )
	{
		m_bReadinessCapable = IsReadinessCapable();
	}

	BaseClass::OnChangeRunningBehavior( pOldBehavior, pNewBehavior );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNPC_South::GatherConditions()
{
	BaseClass::GatherConditions();

	if( HasCondition( COND_HEAR_DANGER ) )
	{
		// Don't worry about combat sounds if in panic. 
		ClearCondition( COND_HEAR_COMBAT );
	}

	// Custom combat AI
	if ( m_NPCState == NPC_STATE_COMBAT )
	{
		DoCustomCombatAI();
	}

//	if ( (GetFlags() & FL_FLY) && m_NPCState != NPC_STATE_SCRIPT && !m_ActBusyBehavior.IsActive() && !m_PassengerBehavior.IsEnabled() )
//	{
//		Warning( "Removed FL_FLY from Alyx, who wasn't running a script or actbusy. Time %.2f, map %s.\n", gpGlobals->curtime, STRING(gpGlobals->mapname) );
//		RemoveFlag( FL_FLY );
//	}
}

PassengerState_e CNPC_South::GetPassengerState( void )
{
	return m_PassengerBehavior.GetPassengerState();
}

//-----------------------------------------------------------------------------
// Basic thinks - Good for simple checks and actions
//-----------------------------------------------------------------------------
void CNPC_South::PrescheduleThink( void )
{
	BaseClass::PrescheduleThink();

	// More simple stuff copied from alyx
	// If we're in stealth mode, and we can still see the stealth node, keep using it
	if ( GetReadinessLevel() == AIRL_STEALTH )
	{
		if ( m_hStealthLookTarget && !m_hStealthLookTarget->IsDisabled() )
		{
			if ( m_hStealthLookTarget->IsInNodeFOV(this) && FVisible( m_hStealthLookTarget ) )
				return;
		}

		// Break out of stealth mode
		SetReadinessLevel( AIRL_STIMULATED, true, true );
		ClearLookTarget( m_hStealthLookTarget );
		m_hStealthLookTarget = NULL;
	}

	// If we're being blinded by the flashlight, see if we should stop
	if ( m_bIsFlashlightBlind )
	{
		if ( m_fStayBlindUntil < gpGlobals->curtime )
		{
 			CBasePlayer *pPlayer = UTIL_PlayerByIndex(1);
 			if ( pPlayer && (!CanBeBlindedByFlashlight( true ) || !pPlayer->IsIlluminatedByFlashlight(this, NULL ) || !PlayerFlashlightOnMyEyes( pPlayer )) &&
				!BlindedByFlare() )
			{
				// Remove the actor from the flashlight scene
				ADD_DEBUG_HISTORY( HISTORY_SOUTH_BLIND, UTIL_VarArgs( "(%0.2f) South: end blind scene '%s'\n", gpGlobals->curtime, STRING(m_iszCurrentBlindScene) ) );
				RemoveActorFromScriptedScenes( this, true, false, STRING(m_iszCurrentBlindScene) );

				// Allow firing again, but prevent myself from firing until I'm done
				GetShotRegulator()->EnableShooting();
				GetShotRegulator()->FireNoEarlierThan( gpGlobals->curtime + 1.0 );
				
				m_bIsFlashlightBlind = false;
				m_flDontBlindUntil = gpGlobals->curtime + RandomFloat( 1, 3 );
			}
		}
	}
	else
	{
		CheckBlindedByFlare();
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNPC_South::UseFunc( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	// if I'm in the vehicle, the player is probably trying to use the vehicle
	if ( GetPassengerState() == PASSENGER_STATE_INSIDE && pActivator->IsPlayer() && GetParent() )
	{
		GetParent()->Use( pActivator, pCaller, useType, value );
		return;
	}
	m_bDontUseSemaphore = true;
	SpeakIfAllowed( TLK_USE );
	m_bDontUseSemaphore = false;

	m_OnPlayerUse.FireOutput( pActivator, pCaller );
}

//-----------------------------------------------------------------------------
// Purpose: Player has illuminated this NPC with the flashlight
//-----------------------------------------------------------------------------
void CNPC_South::PlayerHasIlluminatedNPC( CBasePlayer *pPlayer, float flDot )
{
 	if ( m_bIsFlashlightBlind )
		return;

	if ( !CanBeBlindedByFlashlight( true ) )
		return;

	// Ignore the flashlight if it's not shining at my eyes
	if ( PlayerFlashlightOnMyEyes( pPlayer ) )
	{
		char szResponse[AI_Response::MAX_RESPONSE_NAME];

		// Only say the blinding speech if it's time to
		if ( SpeakIfAllowed( "TLK_FLASHLIGHT_ILLUM", NULL, false, szResponse, AI_Response::MAX_RESPONSE_NAME  ) )
		{
			m_iszCurrentBlindScene = AllocPooledString( szResponse );
			ADD_DEBUG_HISTORY( HISTORY_SOUTH_BLIND, UTIL_VarArgs( "(%0.2f) South: start flashlight blind scene '%s'\n", gpGlobals->curtime, STRING(m_iszCurrentBlindScene) ) );
			GetShotRegulator()->DisableShooting();
			m_bIsFlashlightBlind = true;
			m_fStayBlindUntil = gpGlobals->curtime + 0.1f;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input:   bCheckLightSources - if true, checks if any light darkness lightsources are near
//-----------------------------------------------------------------------------
bool CNPC_South::CanBeBlindedByFlashlight( bool bCheckLightSources )
{
	// Can't be blinded if we're not in alyx darkness mode
 	/*
	if ( !HL2GameRules()->IsAlyxInDarknessMode() )
		return false;
	*/

	// Can't be blinded if I'm in a script, or in combat
	if ( IsInAScript() || GetState() == NPC_STATE_COMBAT || GetState() == NPC_STATE_SCRIPT )
		return false;
	if ( IsSpeaking() )
		return false;

	// can't be blinded if South is near a light source
	if ( bCheckLightSources && DarknessLightSourceWithinRadius( this, 500 ) )
		return false;

	// Not during an actbusy
	if ( m_ActBusyBehavior.IsActive() )
		return false;
	if ( m_OperatorBehavior.IsRunning() )
		return false;

	// Can't be blinded if I've been in combat recently, to fix anim snaps
	if ( GetLastEnemyTime() != 0.0 )
	{
		if ( (gpGlobals->curtime - GetLastEnemyTime()) < 2 )
			return false;
	}

	// Can't be blinded if I'm reloading
	if ( IsCurSchedule(SCHED_RELOAD, false) )
		return false;

	// Can't be blinded right after being blind, to prevent oscillation
	if ( gpGlobals->curtime < m_flDontBlindUntil )
		return false;

	return true;
}

//-----------------------------------------------------------------------------
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CNPC_South::PlayerFlashlightOnMyEyes( CBasePlayer *pPlayer )
{
	Vector vecEyes, vecPlayerForward;
 	vecEyes = EyePosition();
 	pPlayer->EyeVectors( &vecPlayerForward );

	Vector vecToEyes = (vecEyes - pPlayer->EyePosition());
	float flDist = VectorNormalize( vecToEyes ); 

	// We can be blinded in daylight, but only at close range
	if ( HL2GameRules()->IsAlyxInDarknessMode() == false )
	{
		if ( flDist > (8*12.0f) )
			return false;
	}

	float flDot = DotProduct( vecPlayerForward, vecToEyes );
	if ( flDot < 0.98 )
		return false;

	// Check facing to ensure we're in front of him
 	Vector los = ( pPlayer->EyePosition() - vecEyes );
	los.z = 0;
	VectorNormalize( los );
	Vector facingDir = EyeDirection2D();
 	flDot = DotProduct( los, facingDir );
	return ( flDot > 0.3 );
}

//-----------------------------------------------------------------------------
// Purpose: Check if player has illuminated this NPC with a flare
//-----------------------------------------------------------------------------
void CNPC_South::CheckBlindedByFlare( void )
{
	if ( m_bIsFlashlightBlind )
		return;

	if ( !CanBeBlindedByFlashlight( false ) )
		return;

	// Ignore the flare if it's not too close
	if ( BlindedByFlare() )
	{
		char szResponse[AI_Response::MAX_RESPONSE_NAME];

		// Only say the blinding speech if it's time to
		if ( SpeakIfAllowed( "TLK_FLASHLIGHT_ILLUM", NULL, false, szResponse, AI_Response::MAX_RESPONSE_NAME ) )
		{
			m_iszCurrentBlindScene = AllocPooledString( szResponse );
			ADD_DEBUG_HISTORY( HISTORY_SOUTH_BLIND, UTIL_VarArgs( "(%0.2f) South: start flare blind scene '%s'\n", gpGlobals->curtime, 
				STRING(m_iszCurrentBlindScene) ) );
			GetShotRegulator()->DisableShooting();
			m_bIsFlashlightBlind = true;
			m_fStayBlindUntil = gpGlobals->curtime + 0.1f;
		}
	}
}

//-----------------------------------------------------------------------------
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CNPC_South::BlindedByFlare( void )
{
	Vector vecEyes = EyePosition();

	Vector los;
	Vector vecToEyes;
	Vector facingDir = EyeDirection2D();

	// use a wider radius when he's already blind to help with edge cases
	// where he flickers back and forth due to animation
	float fBlindDist = ( m_bIsFlashlightBlind ) ? 35.0f : 30.0f;

	CFlare *pFlare = CFlare::GetActiveFlares();
	while( pFlare != NULL )
	{
		vecToEyes = (vecEyes - pFlare->GetAbsOrigin());
		float fDist = VectorNormalize( vecToEyes ); 
		if ( fDist < fBlindDist )
		{
			// Check facing to ensure we're in front of him
			los = ( pFlare->GetAbsOrigin() - vecEyes );
			los.z = 0;
			VectorNormalize( los );
			float flDot = DotProduct( los, facingDir );
			if ( ( flDot > 0.3 ) && FVisible( pFlare ) )
			{
				return true;
			}
		}

		pFlare = pFlare->GetNextFlare();
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Called by enemy NPC's when they are ignited
// Input  : pVictim - entity that was ignited
//-----------------------------------------------------------------------------
void CNPC_South::EnemyIgnited( CAI_BaseNPC *pVictim )
{
	if ( FVisible( pVictim ) )
	{
		SpeakIfAllowed( TLK_ENEMY_BURNING );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Custom AI for South while in combat
//-----------------------------------------------------------------------------
void CNPC_South::DoCustomCombatAI( void )
{
	// Only run the following code if we're not in a vehicle
	if ( RunningPassengerBehavior() == false )
	{
		// Do mobbed by enemies logic
		DoMobbedCombatAI();
	}

	CBaseEntity *pEnemy = GetEnemy();

	if( HasCondition( COND_LOW_PRIMARY_AMMO ) )
	{
		if( pEnemy )
		{
			if( GetAbsOrigin().DistToSqr( pEnemy->GetAbsOrigin() ) < Square( 80.0f ) )
			{
				// Don't reload if an enemy is right in my face.
				ClearCondition( COND_LOW_PRIMARY_AMMO );
			}
		}
	}
}

bool CNPC_South::RunningPassengerBehavior( void )
{
	// Must be active and not outside the vehicle
	if ( m_PassengerBehavior.IsRunning() && m_PassengerBehavior.GetPassengerState() != PASSENGER_STATE_OUTSIDE )
		return true;

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Handle "mobbed" combat condition when South is overwhelmed by force
//-----------------------------------------------------------------------------
void CNPC_South::DoMobbedCombatAI( void )
{
	AIEnemiesIter_t iter;

	float visibleEnemiesScore = 0.0f;
	float closeEnemiesScore = 0.0f;

	for ( AI_EnemyInfo_t *pEMemory = GetEnemies()->GetFirst(&iter); pEMemory != NULL; pEMemory = GetEnemies()->GetNext(&iter) )
	{
		if ( IRelationType( pEMemory->hEnemy ) != D_NU && IRelationType( pEMemory->hEnemy ) != D_LI && pEMemory->hEnemy->GetAbsOrigin().DistToSqr(GetAbsOrigin()) <= SOUTH_MIN_CONSIDER_DIST )
		{
			if( pEMemory->hEnemy && pEMemory->hEnemy->IsAlive() && gpGlobals->curtime - pEMemory->timeLastSeen <= 0.5f && pEMemory->hEnemy->Classify() != CLASS_BULLSEYE )
			{
				if( pEMemory->hEnemy->GetAbsOrigin().DistToSqr(GetAbsOrigin()) <= SOUTH_MIN_MOB_DIST_SQR )
				{
					closeEnemiesScore += 1.0f;
				}
				else
				{
					visibleEnemiesScore += 1.0f;
				}
			}
		}
	}

	if( closeEnemiesScore > 2 )
	{
		SetCondition( COND_MOBBED_BY_ENEMIES );

		// mark anyone in the mob as having mobbed me
		for ( AI_EnemyInfo_t *pEMemory = GetEnemies()->GetFirst(&iter); pEMemory != NULL; pEMemory = GetEnemies()->GetNext(&iter) )
		{
			if ( pEMemory->bMobbedMe )
				continue;

			if ( IRelationType( pEMemory->hEnemy ) != D_NU && IRelationType( pEMemory->hEnemy ) != D_LI && pEMemory->hEnemy->GetAbsOrigin().DistToSqr(GetAbsOrigin()) <= SOUTH_MIN_CONSIDER_DIST )
			{
				if( pEMemory->hEnemy && pEMemory->hEnemy->IsAlive() && gpGlobals->curtime - pEMemory->timeLastSeen <= 0.5f && pEMemory->hEnemy->Classify() != CLASS_BULLSEYE )
				{
					if( pEMemory->hEnemy->GetAbsOrigin().DistToSqr(GetAbsOrigin()) <= SOUTH_MIN_MOB_DIST_SQR )
					{
						pEMemory->bMobbedMe = true;
					}
				}
			}
		}
	}
	else
	{
		ClearCondition( COND_MOBBED_BY_ENEMIES );
	}

	// South conviently doesnt need to reload when mobbed in cqc.
	if( HasCondition( COND_MOBBED_BY_ENEMIES ) )
	{
		ClearCondition( COND_LOW_PRIMARY_AMMO );
	}

	// Scream some combat speech
	if( HasCondition( COND_MOBBED_BY_ENEMIES ) )
	{
		SpeakIfAllowed( TLK_MOBBED );
	}
	else if( visibleEnemiesScore > 4 )
	{
		SpeakIfAllowed( TLK_MANY_ENEMIES );
	}
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
WeaponProficiency_t CNPC_South::CalcWeaponProficiency( CBaseCombatWeapon *pWeapon )
{

	// Main Weapon
	if( FClassnameIs( pWeapon, "weapon_smg1" ) )
	{
		return WEAPON_PROFICIENCY_VERY_GOOD;
	}

	if( FClassnameIs( pWeapon, "weapon_pistol" ) )
	{
		return WEAPON_PROFICIENCY_AVERAGE;
	}
	else if( FClassnameIs( pWeapon, "weapon_357" ) )
	{
		return WEAPON_PROFICIENCY_VERY_GOOD;
	}

	if( FClassnameIs( pWeapon, "weapon_ar2" ) )
	{
		return WEAPON_PROFICIENCY_GOOD;
	}

	// Shotgun is calculated differently
	//	if( FClassnameIs( pWeapon, "weapon_shotgun" ) )
	//	{
	//		return WEAPON_PROFICIENCY_VERY_GOOD;
	//	}

	return BaseClass::CalcWeaponProficiency( pWeapon );
}

//-----------------------------------------------------------------------------
// Purpose: Use custom generic attacking speech
//-----------------------------------------------------------------------------
bool CNPC_South::OnBeginMoveAndShoot()
{
	if ( BaseClass::OnBeginMoveAndShoot() )
	{
		// Might want to change this to a new concept, like TLK_ATTACKMOVE?
		SpeakAttacking();
		return true;
	}

	return false;
}

void CNPC_South::SpeakAttacking( void )
{
	if ( GetActiveWeapon() && m_AnnounceAttackTimer.Expired() )
	{
		// Also gets the weapon used
		SpeakIfAllowed( TLK_ATTACKING, UTIL_VarArgs("attacking_with_weapon:%s", GetActiveWeapon()->GetClassname()) );
		m_AnnounceAttackTimer.Set( 4, 8 );
	}
}

//------------------------------------------------------------------------------
// Purpose: Declare all the stuff at the end of init
//------------------------------------------------------------------------------

AI_BEGIN_CUSTOM_NPC( npc_south, CNPC_South )
	
	// Looks kind of like this for more advanced NPCs
	//DECLARE_TASK( TASK_ALYX_FALL_TO_GROUND )
	//
	//DECLARE_ANIMEVENT( COMBINE_AE_ALTFIRE )
	//
	//DECLARE_CONDITION( COND_ALYX_IN_DARK )
	//
	//--------------------------------------------------------------------------
	//	DEFINE_SCHEDULE
	//	(
	//		SCHED_ALYX_PREPARE_TO_INTERACT_WITH_TARGET,
	//
	//		"	Tasks"
	//		"		TASK_STOP_MOVING						0"
	//		"		TASK_PLAY_SEQUENCE						ACTIVITY:ACT_ALYX_DRAW_TOOL"
	//		"		TASK_SET_ACTIVITY						ACTIVITY:ACT_ALYX_IDLE_TOOL"
	//		"		TASK_FACE_PLAYER						0"
	//		""
	//		"	Interrupts"
	//		"	COND_LIGHT_DAMAGE"
	//		"	COND_HEAVY_DAMAGE"
	//	)
	//
	//--------------------------------------------------------------------------
	
AI_END_CUSTOM_NPC()

//=============================================================================