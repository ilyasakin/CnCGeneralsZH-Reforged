/*
 * Link-time stand-ins for the pieces of the game that do not live in
 * gameengine.lib.
 *
 * Two kinds of thing end up here:
 *
 *  - MapObject's render half on Windows.  The well-known Dict keys and MapObject's
 *    data are gameengine's since B6 (Common/WellKnownKeys.cpp, Common/MapObject.cpp),
 *    so the tests link the real ones.  The three members that hold a RenderObjClass
 *    live beside the W3D terrain code in gameenginedevice, which this test does not
 *    link; off Windows gameengine's MapObjectRenderPosix.cpp has them.
 *
 *  - Device/exe callbacks the engine calls out to: the W3D shader manager, the
 *    CD manager, the Win32 message boxes, WinMain.  None of them are reachable
 *    from the tests, so they are stubs; when GameEngineDevice lands in Phase 4
 *    the real definitions take over and this file shrinks.
 */

#include "Common/MapObject.h"
#include "Common/OSDisplay.h"
#include "Common/CDManager.h"
#include "Common/GameLOD.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/Shadow.h"
#include "GameLogic/TerrainLogic.h"

#include <windows.h>

//////////////////////////////////////////////////////////////////////////////
// MapObject's render half, Windows only (see the top of this file)
//////////////////////////////////////////////////////////////////////////////

#if defined(_WIN32)
void MapObject::setRenderObj( RenderObjClass *pObj ) { m_renderObj = pObj; }
void MapObject::setBridgeRenderObject( BridgeTowerType type, RenderObjClass *renderObj )
{
	if( type >= 0 && type < BRIDGE_MAX_TOWERS )
		m_bridgeTowers[ type ] = renderObj;
}
RenderObjClass *MapObject::getBridgeRenderObject( BridgeTowerType type )
{
	return ( type >= 0 && type < BRIDGE_MAX_TOWERS ) ? m_bridgeTowers[ type ] : NULL;
}
#endif

//////////////////////////////////////////////////////////////////////////////
// Device layer
//////////////////////////////////////////////////////////////////////////////

HWND ApplicationHWnd = NULL;

/* Debug.cpp names its log file after this; WinMain.cpp owns it in the real exe. */
char *gAppPrefix = "test_";
ProjectedShadowManager *TheProjectedShadowManager = NULL;

const Char *g_strFile = "data\\Generals.str";
const Char *g_csfFile = "data\\%s\\Generals.csf";

CDManagerInterface *CreateCDManager( void ) { return NULL; }

OSDisplayButtonType OSDisplayWarningBox( AsciiString, AsciiString, UnsignedInt, UnsignedInt )
{
	return OSDBT_ERROR;
}

WindowMsgHandledType MOTDSystem( GameWindow *, UnsignedInt, WindowMsgData, WindowMsgData )
{
	return MSG_IGNORED;
}

Bool testMinimumRequirements( ChipsetType *videoChipType, CpuType *cpuType, Int *cpuFreq,
															Int *numRAM, Real *intBenchIndex, Real *floatBenchIndex,
															Real *memBenchIndex )
{
	if( videoChipType )		*videoChipType = DC_UNKNOWN;
	if( cpuType )					*cpuType = XX;
	if( cpuFreq )					*cpuFreq = 0;
	if( numRAM )					*numRAM = 0;
	if( intBenchIndex )		*intBenchIndex = 0.0f;
	if( floatBenchIndex )	*floatBenchIndex = 0.0f;
	if( memBenchIndex )		*memBenchIndex = 0.0f;
	return FALSE;
}

void ReloadAllTextures( void ) {}
void oversizeTheTerrain( Int ) {}
void doSkyBoxSet( Bool ) {}

int getQR2HostingStatus( void ) { return 0; }

/* StackDump takes WinMain's address to work out where the exe's own code
   starts; the test is a console app and never gets here. */
int WINAPI WinMain( HINSTANCE, HINSTANCE, LPSTR, int ) { return 0; }
