/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <engine/shared/config.h>

#include <game/mapitems.h>

#include "entities/character.h"
#include "entities/pickup.h"
#include "gamecontext.h"
#include "gamecontroller.h"
#include "player.h"

#include "entities/light.h"
#include "entities/dragger.h"
#include "entities/gun.h"
#include "entities/projectile.h"
#include "entities/plasma.h"
#include "entities/door.h"
#include <game/layers.h>


IGameController::IGameController(CGameContext *pGameServer)
{
	m_pGameServer = pGameServer;
	m_pServer = m_pGameServer->Server();

	// game
	m_GameStartTick = Server()->Tick();
	m_MatchCount = 0;
	m_RoundCount = 0;
	m_SuddenDeath = 0;

	// info
	m_GameFlags = 0;
	m_pGameType = "unknown";

	m_GameInfo.m_MatchCurrent = 0;
	m_GameInfo.m_MatchNum = 0;
	m_GameInfo.m_ScoreLimit = g_Config.m_SvScorelimit;
	m_GameInfo.m_TimeLimit = g_Config.m_SvTimelimit;

	// map
	m_aMapWish[0] = 0;

	// spawn
	m_aNumSpawnPoints[0] = 0;
	m_aNumSpawnPoints[1] = 0;
	m_aNumSpawnPoints[2] = 0;

	// commands
	CommandsManager()->OnInit();

	// DDrace
	m_CurrentRecord = 0;
}

bool IGameController::CanSpawn(int Team, vec2 *pOutPos) const
{
	// spectators can't spawn
	if(Team == TEAM_SPECTATORS)
		return false;

	CSpawnEval Eval;
	Eval.m_RandomSpawn = false;

	EvaluateSpawnType(&Eval, 0);

	*pOutPos = Eval.m_Pos;
	return Eval.m_Got;
}

float IGameController::EvaluateSpawnPos(CSpawnEval *pEval, vec2 Pos) const
{
	float Score = 0.0f;
	CCharacter *pC = static_cast<CCharacter *>(GameServer()->m_World.FindFirst(CGameWorld::ENTTYPE_CHARACTER));
	for(; pC; pC = (CCharacter *)pC->TypeNext())
	{
		// team mates are not as dangerous as enemies
		float Scoremod = 1.0f;
		if(pEval->m_FriendlyTeam != -1 && pC->GetPlayer()->GetTeam() == pEval->m_FriendlyTeam)
			Scoremod = 0.5f;

		float d = distance(Pos, pC->GetPos());
		Score += Scoremod * (d == 0 ? 1000000000.0f : 1.0f/d);
	}

	return Score;
}

void IGameController::EvaluateSpawnType(CSpawnEval *pEval, int Type) const
{
	// get spawn point
	for(int i = 0; i < m_aNumSpawnPoints[Type]; i++)
	{
		// check if the position is occupado
		CCharacter *aEnts[MAX_CLIENTS];
		int Num = GameServer()->m_World.FindEntities(m_aaSpawnPoints[Type][i], 64, (CEntity**)aEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
		vec2 Positions[5] = { vec2(0.0f, 0.0f), vec2(-32.0f, 0.0f), vec2(0.0f, -32.0f), vec2(32.0f, 0.0f), vec2(0.0f, 32.0f) };	// start, left, up, right, down
		int Result = -1;
		for(int Index = 0; Index < 5 && Result == -1; ++Index)
		{
			Result = Index;
			for(int c = 0; c < Num; ++c)
				if(GameServer()->Collision()->CheckPoint(m_aaSpawnPoints[Type][i]+Positions[Index]) ||
					distance(aEnts[c]->GetPos(), m_aaSpawnPoints[Type][i]+Positions[Index]) <= aEnts[c]->GetProximityRadius())
				{
					Result = -1;
					break;
				}
		}
		if(Result == -1)
			continue;	// try next spawn point

		vec2 P = m_aaSpawnPoints[Type][i]+Positions[Result];
		float S = pEval->m_RandomSpawn ? random_int() : EvaluateSpawnPos(pEval, P);
		if(!pEval->m_Got || pEval->m_Score > S)
		{
			pEval->m_Got = true;
			pEval->m_Score = S;
			pEval->m_Pos = P;
		}
	}
}

int IGameController::OnCharacterDeath(CCharacter *pVictim, CPlayer *pKiller, int Weapon)
{
	return 0;
}

void IGameController::OnCharacterSpawn(CCharacter *pChr)
{
	// default health
	pChr->IncreaseHealth(10);
	pChr->IncreaseArmor(10);

	// give default weapons
	pChr->GiveWeapon(WEAPON_HAMMER);
	pChr->GiveWeapon(WEAPON_GUN);
}

bool IGameController::OnEntity(int Index, vec2 Pos, int Layer, int Flags, int Number)
{
	if (Index < 0)
		return false;

	int Type = -1;
	int SubType = 0;

	int x,y;
	x=(Pos.x-16.0f)/32.0f;
	y=(Pos.y-16.0f)/32.0f;
	int sides[8];
	sides[0]=GameServer()->Collision()->Entity(x,y+1, Layer);
	sides[1]=GameServer()->Collision()->Entity(x+1,y+1, Layer);
	sides[2]=GameServer()->Collision()->Entity(x+1,y, Layer);
	sides[3]=GameServer()->Collision()->Entity(x+1,y-1, Layer);
	sides[4]=GameServer()->Collision()->Entity(x,y-1, Layer);
	sides[5]=GameServer()->Collision()->Entity(x-1,y-1, Layer);
	sides[6]=GameServer()->Collision()->Entity(x-1,y, Layer);
	sides[7]=GameServer()->Collision()->Entity(x-1,y+1, Layer);

	if(Index == ENTITY_SPAWN)
		m_aaSpawnPoints[0][m_aNumSpawnPoints[0]++] = Pos;
	else if(Index == ENTITY_SPAWN_RED)
		m_aaSpawnPoints[1][m_aNumSpawnPoints[1]++] = Pos;
	else if(Index == ENTITY_SPAWN_BLUE)
		m_aaSpawnPoints[2][m_aNumSpawnPoints[2]++] = Pos;

	else if(Index == ENTITY_DOOR)
	{
		for(int i = 0; i < 8;i++)
		{
			if (sides[i] >= ENTITY_LASER_SHORT && sides[i] <= ENTITY_LASER_LONG)
			{
				new CDoor
				(
					&GameServer()->m_World, //GameWorld
					Pos, //Pos
					pi / 4 * i, //Rotation
					32 * 3 + 32 *(sides[i] - ENTITY_LASER_SHORT) * 3, //Length
					Number //Number
				);
			}
		}
	}
	else if(Index == ENTITY_CRAZY_SHOTGUN_EX)
	{
		int Dir;
		if(!Flags)
			Dir = 0;
		else if(Flags == ROTATION_90)
			Dir = 1;
		else if(Flags == ROTATION_180)
			Dir = 2;
		else
			Dir = 3;
		float Deg = Dir * (pi / 2);
		CProjectile *bullet = new CProjectile
			(
			&GameServer()->m_World,
			WEAPON_SHOTGUN, //Type
			-1, //Owner
			Pos, //Pos
			vec2(sin(Deg), cos(Deg)), //Dir
			-2, //Span
			true, //Freeze
			true, //Explosive
			0, //Force
			(g_Config.m_SvShotgunBulletSound)?SOUND_GRENADE_EXPLODE:-1,//SoundImpact
			Layer,
			Number
			);
		bullet->SetBouncing(2 - (Dir % 2));
	}
	else if(Index == ENTITY_CRAZY_SHOTGUN)
	{
		int Dir;
		if(!Flags)
			Dir=0;
		else if(Flags == (TILEFLAG_ROTATE))
			Dir = 1;
		else if(Flags == (TILEFLAG_VFLIP|TILEFLAG_HFLIP))
			Dir = 2;
		else
			Dir = 3;
		float Deg = Dir * ( pi / 2);
		CProjectile *bullet = new CProjectile
			(
			&GameServer()->m_World,
			WEAPON_SHOTGUN, //Type
			-1, //Owner
			Pos, //Pos
			vec2(sin(Deg), cos(Deg)), //Dir
			-2, //Span
			true, //Freeze
			false, //Explosive
			0,
			SOUND_GRENADE_EXPLODE,
			Layer,
			Number
			);
		bullet->SetBouncing(2 - (Dir % 2));
	}

	if(Index == ENTITY_ARMOR_1)
		Type = POWERUP_ARMOR;
	else if(Index == ENTITY_HEALTH_1)
		Type = POWERUP_HEALTH;
	else if(Index == ENTITY_WEAPON_SHOTGUN)
	{
		Type = POWERUP_WEAPON;
		SubType = WEAPON_SHOTGUN;
	}
	else if(Index == ENTITY_WEAPON_GRENADE)
	{
		Type = POWERUP_WEAPON;
		SubType = WEAPON_GRENADE;
	}
	else if(Index == ENTITY_WEAPON_LASER)
	{
		Type = POWERUP_WEAPON;
		SubType = WEAPON_LASER;
	}
	else if(Index == ENTITY_POWERUP_NINJA)
	{
		Type = POWERUP_NINJA;
		SubType = WEAPON_NINJA;
	}
	else if(Index >= ENTITY_LASER_FAST_CCW && Index <= ENTITY_LASER_FAST_CW)
	{
		int sides2[8];
		sides2[0]=GameServer()->Collision()->Entity(x, y + 2, Layer);
		sides2[1]=GameServer()->Collision()->Entity(x + 2, y + 2, Layer);
		sides2[2]=GameServer()->Collision()->Entity(x + 2, y, Layer);
		sides2[3]=GameServer()->Collision()->Entity(x + 2, y - 2, Layer);
		sides2[4]=GameServer()->Collision()->Entity(x,y - 2, Layer);
		sides2[5]=GameServer()->Collision()->Entity(x - 2, y - 2, Layer);
		sides2[6]=GameServer()->Collision()->Entity(x - 2, y, Layer);
		sides2[7]=GameServer()->Collision()->Entity(x - 2, y + 2, Layer);

		float AngularSpeed = 0.0f;
		int Ind=Index-ENTITY_LASER_STOP;
		int M;
		if( Ind < 0)
		{
			Ind = -Ind;
			M = 1;
		}
		else if(Ind == 0)
			M = 0;
		else
			M = -1;


		if(Ind == 0)
			AngularSpeed = 0.0f;
		else if(Ind == 1)
			AngularSpeed = pi / 360;
		else if(Ind == 2)
			AngularSpeed = pi / 180;
		else if(Ind == 3)
			AngularSpeed = pi / 90;
		AngularSpeed *= M;

		for(int i=0; i<8;i++)
		{
			if(sides[i] >= ENTITY_LASER_SHORT && sides[i] <= ENTITY_LASER_LONG)
			{
				CLight *Lgt = new CLight(&GameServer()->m_World, Pos, pi / 4 * i, 32 * 3 + 32 * (sides[i] - ENTITY_LASER_SHORT) * 3, Layer, Number);
				Lgt->m_AngularSpeed = AngularSpeed;
				if(sides2[i] >= ENTITY_LASER_C_SLOW && sides2[i] <= ENTITY_LASER_C_FAST)
				{
					Lgt->m_Speed = 1 + (sides2[i] - ENTITY_LASER_C_SLOW) * 2;
					Lgt->m_CurveLength = Lgt->m_Length;
				}
				else if(sides2[i] >= ENTITY_LASER_O_SLOW && sides2[i] <= ENTITY_LASER_O_FAST)
				{
					Lgt->m_Speed = 1 + (sides2[i] - ENTITY_LASER_O_SLOW) * 2;
					Lgt->m_CurveLength = 0;
				}
				else
					Lgt->m_CurveLength = Lgt->m_Length;
			}
		}

	}
	else if(Index >= ENTITY_DRAGGER_WEAK && Index <= ENTITY_DRAGGER_STRONG)
	{
		CDraggerTeam(&GameServer()->m_World, Pos, Index - ENTITY_DRAGGER_WEAK + 1, false, Layer, Number);
	}
	else if(Index >= ENTITY_DRAGGER_WEAK_NW && Index <= ENTITY_DRAGGER_STRONG_NW)
	{
		CDraggerTeam(&GameServer()->m_World, Pos, Index - ENTITY_DRAGGER_WEAK_NW + 1, true, Layer, Number);
	}
	else if(Index == ENTITY_PLASMAE)
	{
		new CGun(&GameServer()->m_World, Pos, false, true, Layer, Number);
	}
	else if(Index == ENTITY_PLASMAF)
	{
		new CGun(&GameServer()->m_World, Pos, true, false, Layer, Number);
	}
	else if(Index == ENTITY_PLASMA)
	{
		new CGun(&GameServer()->m_World, Pos, true, true, Layer, Number);
	}
	else if(Index == ENTITY_PLASMAU)
	{
		new CGun(&GameServer()->m_World, Pos, false, false, Layer, Number);
	}

	if(Type != -1)
	{
		new CPickup(&GameServer()->m_World, Type, Pos, SubType, Layer, Number);
		return true;
	}

	return false;
}

void IGameController::ResetGame()
{
	GameServer()->m_World.m_ResetRequested = true;
}

void IGameController::StartRound()
{
	ResetGame();

	m_RoundCount = 0;
	m_SuddenDeath = 0;
	GameServer()->m_World.m_Paused = false;
	Server()->DemoRecorder_HandleAutoStart();
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "start match type='%s' teamplay='%d'", m_pGameType, m_GameFlags & GAMEFLAG_TEAMS);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
}

void IGameController::Snap(int SnappingClient)
{
	CNetObj_GameData *pGameData = static_cast<CNetObj_GameData *>(Server()->SnapNewItem(NETOBJTYPE_GAMEDATA, 0, sizeof(CNetObj_GameData)));
	if(!pGameData)
		return;

	CCharacter* pSnappingChar = GameServer()->GetPlayerChar(SnappingClient);
	CPlayer* pSnap = GameServer()->m_apPlayers[SnappingClient];
	CCharacter* pSpectator = (pSnap && (pSnap->GetTeam() == TEAM_SPECTATORS || pSnap->IsPaused())) ? GameServer()->GetPlayerChar(pSnap->GetSpectatorID()) : 0;

	if (pSpectator && pSpectator->m_DDRaceState == DDRACE_STARTED)
		pGameData->m_GameStartTick = pSpectator->m_StartTime;
	else if (!pSpectator && pSnappingChar && pSnappingChar->m_DDRaceState == DDRACE_STARTED)
		pGameData->m_GameStartTick = pSnappingChar->m_StartTime;
	else
		pGameData->m_GameStartTick = m_GameStartTick;

	pGameData->m_GameStateFlags = 0;
	pGameData->m_GameStateEndTick = 0; // no timer/infinite = 0, on end = GameEndTick, otherwise = GameStateEndTick
	if(m_SuddenDeath)
		pGameData->m_GameStateFlags |= GAMESTATEFLAG_SUDDENDEATH;

	CNetObj_GameDataRace* pGameDataRace = static_cast<CNetObj_GameDataRace*>(Server()->SnapNewItem(NETOBJTYPE_GAMEDATARACE, 0, sizeof(CNetObj_GameDataRace)));
	if (!pGameDataRace)
		return;

	pGameDataRace->m_BestTime = m_CurrentRecord == 0 ? -1 : m_CurrentRecord * 1000.0f;
	pGameDataRace->m_Precision = 0;
	pGameDataRace->m_RaceFlags = 0;

	// demo recording
	if(SnappingClient == -1)
	{
		CNetObj_De_GameInfo *pGameInfo = static_cast<CNetObj_De_GameInfo *>(Server()->SnapNewItem(NETOBJTYPE_DE_GAMEINFO, 0, sizeof(CNetObj_De_GameInfo)));
		if(!pGameInfo)
			return;

		pGameInfo->m_GameFlags = m_GameFlags;
		pGameInfo->m_ScoreLimit = 0;
		pGameInfo->m_TimeLimit = 0;
		pGameInfo->m_MatchNum = 0;
		pGameInfo->m_MatchCurrent = 0;
	}
}

void IGameController::Tick()
{
	// check for inactive players
	if (g_Config.m_SvInactiveKickTime > 0)
	{
		for (int i = 0; i < MAX_CLIENTS; ++i)
		{
#ifdef CONF_DEBUG
			if (g_Config.m_DbgDummies)
			{
				if (i >= MAX_CLIENTS - g_Config.m_DbgDummies)
					break;
			}
#endif
			if (GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS && Server()->GetAuthedState(i) == AUTHED_NO)
			{
				if (Server()->Tick() > GameServer()->m_apPlayers[i]->m_LastActionTick + g_Config.m_SvInactiveKickTime * Server()->TickSpeed() * 60)
				{
					switch (g_Config.m_SvInactiveKick)
					{
					case 0:
					{
						// move player to spectator
						GameServer()->m_apPlayers[i]->SetTeam(TEAM_SPECTATORS);
					}
					break;
					case 1:
					{
						// move player to spectator if the reserved slots aren't filled yet, kick him otherwise
						int Spectators = 0;
						for (int j = 0; j < MAX_CLIENTS; ++j)
							if (GameServer()->m_apPlayers[j] && GameServer()->m_apPlayers[j]->GetTeam() == TEAM_SPECTATORS)
								++Spectators;
						if (Spectators >= g_Config.m_SvSpectatorSlots)
							Server()->Kick(i, "Kicked for inactivity");
						else
							GameServer()->m_apPlayers[i]->SetTeam(TEAM_SPECTATORS);
					}
					break;
					case 2:
					{
						// kick the player
						Server()->Kick(i, "Kicked for inactivity");
					}
					}
				}
			}
		}
	}
}

void IGameController::UpdateGameInfo(int ClientID)
{
	CNetMsg_Sv_GameInfo GameInfoMsg;
	GameInfoMsg.m_GameFlags = m_GameFlags;
	GameInfoMsg.m_ScoreLimit = m_GameInfo.m_ScoreLimit;
	GameInfoMsg.m_TimeLimit = m_GameInfo.m_TimeLimit;
	GameInfoMsg.m_MatchNum = m_GameInfo.m_MatchNum;
	GameInfoMsg.m_MatchCurrent = m_GameInfo.m_MatchCurrent;

	CNetMsg_Sv_GameInfo GameInfoMsgNoRace = GameInfoMsg;
	GameInfoMsgNoRace.m_GameFlags &= ~GAMEFLAG_RACE;

	if(ClientID == -1)
	{
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(!GameServer()->m_apPlayers[i] || !Server()->ClientIngame(i))
				continue;

			CNetMsg_Sv_GameInfo *pInfoMsg = (Server()->GetClientVersion(i) < CGameContext::MIN_RACE_CLIENTVERSION) ? &GameInfoMsgNoRace : &GameInfoMsg;
			Server()->SendPackMsg(pInfoMsg, MSGFLAG_VITAL|MSGFLAG_NORECORD, i);
		}
	}
	else
	{
		CNetMsg_Sv_GameInfo *pInfoMsg = (Server()->GetClientVersion(ClientID) < CGameContext::MIN_RACE_CLIENTVERSION) ? &GameInfoMsgNoRace : &GameInfoMsg;
		Server()->SendPackMsg(pInfoMsg, MSGFLAG_VITAL|MSGFLAG_NORECORD, ClientID);
	}
}

void IGameController::ChangeMap(const char *pToMap)
{
	str_copy(g_Config.m_SvMap, pToMap, sizeof(g_Config.m_SvMap));
}

bool IGameController::CanJoinTeam(int Team, int NotThisID) const
{
	if (Team == TEAM_SPECTATORS || (GameServer()->m_apPlayers[NotThisID] && GameServer()->m_apPlayers[NotThisID]->GetTeam() != TEAM_SPECTATORS))
		return true;

	int aNumplayers[2] = { 0,0 };
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		if (GameServer()->m_apPlayers[i] && i != NotThisID)
		{
			if (GameServer()->m_apPlayers[i]->GetTeam() >= TEAM_RED && GameServer()->m_apPlayers[i]->GetTeam() <= TEAM_BLUE)
				aNumplayers[GameServer()->m_apPlayers[i]->GetTeam()]++;
		}
	}

	return (aNumplayers[0] + aNumplayers[1]) < Server()->MaxClients() - g_Config.m_SvSpectatorSlots;
}

int IGameController::ClampTeam(int Team) const
{
	if (Team < TEAM_RED)
		return TEAM_SPECTATORS;
	return TEAM_RED;
}

const char* IGameController::GetTeamName(int Team)
{
	if (Team == TEAM_RED)
		return "game";
	return "spectators";
}

IGameController::CChatCommands::CChatCommands()
{
	mem_zero(m_aCommands, sizeof(m_aCommands));
}

void IGameController::CChatCommands::AddCommand(const char *pName, const char *pArgsFormat, const char *pHelpText, COMMAND_CALLBACK pfnCallback)
{
	if(GetCommand(pName))
		return;

	for(int i = 0; i < MAX_COMMANDS; i++)
	{
		if(!m_aCommands[i].m_Used)
		{
			mem_zero(&m_aCommands[i], sizeof(CChatCommand));

			str_copy(m_aCommands[i].m_aName, pName, sizeof(m_aCommands[i].m_aName));
			str_copy(m_aCommands[i].m_aHelpText, pHelpText, sizeof(m_aCommands[i].m_aHelpText));
			str_copy(m_aCommands[i].m_aArgsFormat, pArgsFormat, sizeof(m_aCommands[i].m_aArgsFormat));

			m_aCommands[i].m_pfnCallback = pfnCallback;
			m_aCommands[i].m_Used = true;
			break;
		}
	}
}

void IGameController::CChatCommands::SendRemoveCommand(IServer *pServer, const char *pName, int ID)
{
	CNetMsg_Sv_CommandInfoRemove Msg;
	Msg.m_pName = pName;

	pServer->SendPackMsg(&Msg, MSGFLAG_VITAL, ID);
}

void IGameController::CChatCommands::RemoveCommand(const char *pName)
{
	CChatCommand *pCommand = GetCommand(pName);

	if(pCommand)
	{
		mem_zero(pCommand, sizeof(CChatCommand));
	}
}

IGameController::CChatCommand *IGameController::CChatCommands::GetCommand(const char *pName)
{
	for(int i = 0; i < MAX_COMMANDS; i++)
	{
		if(m_aCommands[i].m_Used && str_comp(m_aCommands[i].m_aName, pName) == 0)
		{
			return &m_aCommands[i];
		}
	}
	return 0;
}

void IGameController::CChatCommands::OnPlayerConnect(IServer *pServer, CPlayer *pPlayer)
{
	// Remove the clientside commands (expect w)
	{
		SendRemoveCommand(pServer, "all", pPlayer->GetCID());
		SendRemoveCommand(pServer, "friend", pPlayer->GetCID());
		SendRemoveCommand(pServer, "m", pPlayer->GetCID());
		SendRemoveCommand(pServer, "mute", pPlayer->GetCID());
		SendRemoveCommand(pServer, "r", pPlayer->GetCID());
		SendRemoveCommand(pServer, "team", pPlayer->GetCID());
		SendRemoveCommand(pServer, "whisper", pPlayer->GetCID());
	}

	for(int i = 0; i < MAX_COMMANDS; i++)
	{
		CChatCommand *pCommand = &m_aCommands[i];

		if(pCommand->m_Used)
		{
			CNetMsg_Sv_CommandInfo Msg;
			Msg.m_pName = pCommand->m_aName;
			Msg.m_HelpText = pCommand->m_aHelpText;
			Msg.m_ArgsFormat = pCommand->m_aArgsFormat;

			pServer->SendPackMsg(&Msg, MSGFLAG_VITAL, pPlayer->GetCID());
		}
	}
}

void IGameController::OnPlayerCommand(CPlayer *pPlayer, const char *pCommandName, const char *pCommandArgs)
{
	// TODO: Add a argument parser?
	CChatCommand *pCommand = CommandsManager()->GetCommand(pCommandName);

	if(pCommand && pCommand->m_pfnCallback)
		pCommand->m_pfnCallback(GameServer(), pPlayer->GetCID(), pCommandArgs);
	else
		GameServer()->ExecuteChatCommand(pCommandArgs, pPlayer->GetCID());
}

void IGameController::CChatCommands::OnInit()
{
	// Add some important commands, client wont sort alphabetically!
	AddCommand("cmdlist", "", "List all commands which are accessible for you", 0);
	AddCommand("credits", "", "Shows the credits of the F-DDrace mod", 0);
	AddCommand("info", "", "Shows info about this server", 0);
	AddCommand("For a full list of commands:", "", "/cmdlist", Com_CmdList);
}

void IGameController::Com_CmdList(CGameContext* pGameServer, int ClientID, const char* pArgs)
{
	pGameServer->ExecuteChatCommand("/cmdlist", ClientID);
}
