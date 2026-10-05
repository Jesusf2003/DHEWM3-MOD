/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 GPL Source Code ("Doom 3 Source Code").

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#ifdef ID_RADIANT_IMGUI

/*
===============================================================================

	ID_RADIANT_IMGUI: dhewm3-radiant, the Dear ImGui based level editor,
	built as its own executable (see tools/radiant/imgui/CMakeLists.txt).
	Without it, this file is the MFC Radiant that runs inside the engine.

	Uses the same backend as the engine (sys/glimp.cpp + sys/sys_imgui.cpp):
	SDL2 (or SDL3) for the window/input and the OpenGL2 renderer of ImGui,
	but with ImGui v1.92.9b-docking from vcpkg instead of libs/imgui, so the
	editor can use docking and multi-viewports.

	It doesn't link the engine (yet): the editor panels are placeholders.

===============================================================================
*/

#include <imgui.h>
#include <imgui_internal.h>	// DockBuilder API, for the default layout
#include <imgui_impl_opengl2.h>

#ifdef RADIANT_IMGUI_SDL3
	#include <SDL3/SDL.h>
	#include <SDL3/SDL_main.h>	// provides WinMain() and calls main()
	#include <SDL3/SDL_opengl.h>
	#include <imgui_impl_sdl3.h>

	#define ImGui_ImplSDLx_InitForOpenGL	ImGui_ImplSDL3_InitForOpenGL
	#define ImGui_ImplSDLx_Shutdown			ImGui_ImplSDL3_Shutdown
	#define ImGui_ImplSDLx_NewFrame			ImGui_ImplSDL3_NewFrame
	#define ImGui_ImplSDLx_ProcessEvent		ImGui_ImplSDL3_ProcessEvent
#else
	#include <SDL.h>	// renames main() to SDL_main(), WinMain() is in SDL2main
	#include <SDL_opengl.h>
	#include <imgui_impl_sdl2.h>

	#define ImGui_ImplSDLx_InitForOpenGL	ImGui_ImplSDL2_InitForOpenGL
	#define ImGui_ImplSDLx_Shutdown			ImGui_ImplSDL2_Shutdown
	#define ImGui_ImplSDLx_NewFrame			ImGui_ImplSDL2_NewFrame
	#define ImGui_ImplSDLx_ProcessEvent		ImGui_ImplSDL2_ProcessEvent
#endif

#include <stdio.h>
#include <string>

// same font as the engine's ImGui (see sys/sys_imgui.cpp)
#ifdef _MSC_VER
	// Visual C++ doesn't support string literals with more than 65535 bytes
	#include "sys/proggyvector_font.h"
#else
	#include "sys/proggyvector_font_base85.h"
#endif

namespace {

const char *		WINDOW_TITLE = "dhewm3 Radiant";
const int			WINDOW_WIDTH = 1600;
const int			WINDOW_HEIGHT = 900;

const char *		DOCKSPACE_NAME = "RadiantDockSpace";
const char *		WIN_XY = "XY View";
const char *		WIN_CAMERA = "Camera";
const char *		WIN_INSPECTOR = "Inspector";
const char *		WIN_CONSOLE = "Console";

struct radiantState_t {
	SDL_Window *	window = nullptr;
	SDL_GLContext	glContext = nullptr;
	std::string		iniPath;
	bool			quit = false;

	bool			showXY = true;
	bool			showCamera = true;
	bool			showInspector = true;
	bool			showConsole = true;
	bool			showDemo = false;
	bool			showAbout = false;
	bool			resetLayout = false;
};

/*
=================
ShowError
=================
*/
void ShowError( const char *title, const char *msg ) {
	fprintf( stderr, "%s: %s\n", title, msg );
	SDL_ShowSimpleMessageBox( SDL_MESSAGEBOX_ERROR, title, msg, nullptr );
}

/*
=================
GetContentScale

like GetDefaultScale() in sys/sys_imgui.cpp
=================
*/
float GetContentScale( SDL_Window *window ) {
#ifdef RADIANT_IMGUI_SDL3
	float scale = SDL_GetWindowDisplayScale( window );
#else
	float scale = ImGui_ImplSDL2_GetContentScaleForWindow( window );
#endif
	if ( scale <= 0.0f || scale > 10.0f ) {
		return 1.0f;
	}
	return SDL_roundf( scale * 2.0f ) * 0.5f; // round to .0 or .5
}

/*
=================
SetDhewm3StyleColors

same colors as the engine's ImGui (see sys/sys_imgui.cpp)
=================
*/
void SetDhewm3StyleColors( ImGuiStyle &style ) {
	ImGui::StyleColorsDark( &style );
	ImVec4 *colors = style.Colors;
	colors[ImGuiCol_TitleBg]		= ImVec4( 0.09f, 0.13f, 0.12f, 0.90f );
	colors[ImGuiCol_TitleBgActive]	= ImVec4( 0.03f, 0.33f, 0.33f, 1.00f );
	colors[ImGuiCol_TabHovered]		= ImVec4( 0.42f, 0.69f, 1.00f, 0.80f );
	colors[ImGuiCol_TabSelected]	= ImVec4( 0.24f, 0.51f, 0.83f, 1.00f );
}

/*
=================
CreateWindowAndContext

same OpenGL setup as GLimp_Init() in sys/glimp.cpp: legacy (compatibility)
context, which is what the Doom 3 renderer and ImGui's OpenGL2 backend use
=================
*/
bool CreateWindowAndContext( radiantState_t &state ) {
	SDL_GL_SetAttribute( SDL_GL_RED_SIZE, 8 );
	SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, 8 );
	SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, 8 );
	SDL_GL_SetAttribute( SDL_GL_ALPHA_SIZE, 8 );
	SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, 24 );
	SDL_GL_SetAttribute( SDL_GL_STENCIL_SIZE, 8 );
	SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );

#ifdef RADIANT_IMGUI_SDL3
	const SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN;
	state.window = SDL_CreateWindow( WINDOW_TITLE, WINDOW_WIDTH, WINDOW_HEIGHT, flags );
#else
	const Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN;
	state.window = SDL_CreateWindow( WINDOW_TITLE, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH, WINDOW_HEIGHT, flags );
#endif
	if ( state.window == nullptr ) {
		ShowError( "Couldn't create window", SDL_GetError() );
		return false;
	}

	state.glContext = SDL_GL_CreateContext( state.window );
	if ( state.glContext == nullptr ) {
		ShowError( "Couldn't create OpenGL context", SDL_GetError() );
		return false;
	}
	SDL_GL_MakeCurrent( state.window, state.glContext );
	SDL_GL_SetSwapInterval( 1 ); // vsync, the editor doesn't need more

#ifdef RADIANT_IMGUI_SDL3
	SDL_SetWindowPosition( state.window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED );
#endif
	SDL_ShowWindow( state.window );
	return true;
}

/*
=================
InitImGui
=================
*/
bool InitImGui( radiantState_t &state ) {
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO &io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;	// panels can be dragged out of the main window

	// keep the layout next to the engine's config (imgui.ini is the game's one)
	char *prefPath = SDL_GetPrefPath( "", "dhewm3" );
	if ( prefPath != nullptr ) {
		state.iniPath = prefPath;
		SDL_free( prefPath );
	}
	state.iniPath += "radiant_imgui.ini";
	io.IniFilename = state.iniPath.c_str();

	ImGuiStyle &style = ImGui::GetStyle();
	SetDhewm3StyleColors( style );
	const float scale = GetContentScale( state.window );
	style.ScaleAllSizes( scale );
	style.FontScaleDpi = scale;
	if ( io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable ) {
		// platform windows should look like the main one
		style.WindowRounding = 0.0f;
		style.Colors[ImGuiCol_WindowBg].w = 1.0f;
	}

#ifdef _MSC_VER
	io.Fonts->AddFontFromMemoryCompressedTTF( ProggyVector_compressed_data, ProggyVector_compressed_size );
#else
	io.Fonts->AddFontFromMemoryCompressedBase85TTF( ProggyVector_compressed_data_base85 );
#endif

	if ( !ImGui_ImplSDLx_InitForOpenGL( state.window, state.glContext ) ) {
		ShowError( "ImGui", "Failed to initialize ImGui SDL platform backend!" );
		return false;
	}
	if ( !ImGui_ImplOpenGL2_Init() ) {
		ImGui_ImplSDLx_Shutdown();
		ShowError( "ImGui", "Failed to initialize ImGui OpenGL renderer backend!" );
		return false;
	}
	return true;
}

/*
=================
ProcessEvents
=================
*/
void ProcessEvents( radiantState_t &state ) {
	SDL_Event ev;
	while ( SDL_PollEvent( &ev ) ) {
		ImGui_ImplSDLx_ProcessEvent( &ev );
#ifdef RADIANT_IMGUI_SDL3
		if ( ev.type == SDL_EVENT_QUIT ) {
			state.quit = true;
		} else if ( ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && ev.window.windowID == SDL_GetWindowID( state.window ) ) {
			state.quit = true;
		}
#else
		if ( ev.type == SDL_QUIT ) {
			state.quit = true;
		} else if ( ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_CLOSE && ev.window.windowID == SDL_GetWindowID( state.window ) ) {
			state.quit = true;
		}
#endif
	}
}

/*
=================
BuildDefaultLayout

classic Radiant layout: XY view on the left, camera and inspector on the right,
console at the bottom. Used when there's no saved layout or on "Reset Layout"
=================
*/
void BuildDefaultLayout( ImGuiID dockspaceId, const ImVec2 &size ) {
	ImGui::DockBuilderRemoveNode( dockspaceId );
	ImGui::DockBuilderAddNode( dockspaceId, ImGuiDockNodeFlags_DockSpace );
	ImGui::DockBuilderSetNodeSize( dockspaceId, size );

	ImGuiID mainId = dockspaceId;
	ImGuiID consoleId = ImGui::DockBuilderSplitNode( mainId, ImGuiDir_Down, 0.25f, nullptr, &mainId );
	ImGuiID rightId = ImGui::DockBuilderSplitNode( mainId, ImGuiDir_Right, 0.40f, nullptr, &mainId );
	ImGuiID inspectorId = ImGui::DockBuilderSplitNode( rightId, ImGuiDir_Down, 0.50f, nullptr, &rightId );

	ImGui::DockBuilderDockWindow( WIN_XY, mainId );
	ImGui::DockBuilderDockWindow( WIN_CAMERA, rightId );
	ImGui::DockBuilderDockWindow( WIN_INSPECTOR, inspectorId );
	ImGui::DockBuilderDockWindow( WIN_CONSOLE, consoleId );
	ImGui::DockBuilderFinish( dockspaceId );
}

/*
=================
DrawMainMenu
=================
*/
void DrawMainMenu( radiantState_t &state ) {
	if ( !ImGui::BeginMainMenuBar() ) {
		return;
	}
	if ( ImGui::BeginMenu( "File" ) ) {
		// TODO: hook up to the map code
		ImGui::MenuItem( "New Map", "Ctrl+N", false, false );
		ImGui::MenuItem( "Open...", "Ctrl+O", false, false );
		ImGui::MenuItem( "Save", "Ctrl+S", false, false );
		ImGui::Separator();
		if ( ImGui::MenuItem( "Exit" ) ) {
			state.quit = true;
		}
		ImGui::EndMenu();
	}
	if ( ImGui::BeginMenu( "View" ) ) {
		ImGui::MenuItem( WIN_XY, nullptr, &state.showXY );
		ImGui::MenuItem( WIN_CAMERA, nullptr, &state.showCamera );
		ImGui::MenuItem( WIN_INSPECTOR, nullptr, &state.showInspector );
		ImGui::MenuItem( WIN_CONSOLE, nullptr, &state.showConsole );
		ImGui::Separator();
		if ( ImGui::MenuItem( "Reset Layout" ) ) {
			state.resetLayout = true;
		}
		ImGui::EndMenu();
	}
	if ( ImGui::BeginMenu( "Help" ) ) {
		ImGui::MenuItem( "ImGui Demo", nullptr, &state.showDemo );
		ImGui::MenuItem( "About", nullptr, &state.showAbout );
		ImGui::EndMenu();
	}
	ImGui::EndMainMenuBar();
}

/*
=================
DrawPanels

placeholders for the editor views
=================
*/
void DrawPanels( radiantState_t &state ) {
	if ( state.showXY ) {
		if ( ImGui::Begin( WIN_XY, &state.showXY ) ) {
			ImGui::TextDisabled( "2D grid view (TODO)" );
		}
		ImGui::End();
	}
	if ( state.showCamera ) {
		if ( ImGui::Begin( WIN_CAMERA, &state.showCamera ) ) {
			ImGui::TextDisabled( "3D camera view (TODO)" );
		}
		ImGui::End();
	}
	if ( state.showInspector ) {
		if ( ImGui::Begin( WIN_INSPECTOR, &state.showInspector ) ) {
			ImGui::TextDisabled( "Entity / texture inspector (TODO)" );
		}
		ImGui::End();
	}
	if ( state.showConsole ) {
		if ( ImGui::Begin( WIN_CONSOLE, &state.showConsole ) ) {
			ImGui::Text( "Dear ImGui %s", ImGui::GetVersion() );
			ImGui::Text( "OpenGL %s", (const char *)glGetString( GL_VERSION ) );
			ImGui::Text( "%.1f FPS", ImGui::GetIO().Framerate );
		}
		ImGui::End();
	}
	if ( state.showDemo ) {
		ImGui::ShowDemoWindow( &state.showDemo );
	}
	if ( state.showAbout ) {
		if ( ImGui::Begin( "About", &state.showAbout, ImGuiWindowFlags_AlwaysAutoResize ) ) {
			ImGui::Text( "%s", WINDOW_TITLE );
			ImGui::Separator();
			ImGui::Text( "Dear ImGui %s (docking)", ImGui::GetVersion() );
		}
		ImGui::End();
	}
}

/*
=================
Frame
=================
*/
void Frame( radiantState_t &state ) {
	ImGui_ImplOpenGL2_NewFrame();
	ImGui_ImplSDLx_NewFrame();
	ImGui::NewFrame();

	DrawMainMenu( state );

	const ImGuiViewport *viewport = ImGui::GetMainViewport();
	const ImGuiID dockspaceId = ImGui::GetID( DOCKSPACE_NAME );
	if ( state.resetLayout || ImGui::DockBuilderGetNode( dockspaceId ) == nullptr ) {
		BuildDefaultLayout( dockspaceId, viewport->WorkSize );
		state.resetLayout = false;
	}
	ImGui::DockSpaceOverViewport( dockspaceId, viewport );

	DrawPanels( state );

	ImGui::Render();

	ImGuiIO &io = ImGui::GetIO();
	glViewport( 0, 0, (int)( io.DisplaySize.x * io.DisplayFramebufferScale.x ), (int)( io.DisplaySize.y * io.DisplayFramebufferScale.y ) );
	glClearColor( 0.1f, 0.1f, 0.1f, 1.0f );
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
	ImGui_ImplOpenGL2_RenderDrawData( ImGui::GetDrawData() );

	// panels dragged out of the main window have their own GL context
	if ( io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable ) {
		SDL_Window *backupWindow = SDL_GL_GetCurrentWindow();
		SDL_GLContext backupContext = SDL_GL_GetCurrentContext();
		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();
		SDL_GL_MakeCurrent( backupWindow, backupContext );
	}

	SDL_GL_SwapWindow( state.window );
}

/*
=================
Shutdown
=================
*/
void Shutdown( radiantState_t &state ) {
	if ( ImGui::GetCurrentContext() != nullptr ) {
		if ( ImGui::GetIO().BackendRendererUserData != nullptr ) {
			ImGui_ImplOpenGL2_Shutdown();
		}
		if ( ImGui::GetIO().BackendPlatformUserData != nullptr ) {
			ImGui_ImplSDLx_Shutdown();
		}
		ImGui::DestroyContext();
	}
	if ( state.glContext != nullptr ) {
#ifdef RADIANT_IMGUI_SDL3
		SDL_GL_DestroyContext( state.glContext );
#else
		SDL_GL_DeleteContext( state.glContext );
#endif
	}
	if ( state.window != nullptr ) {
		SDL_DestroyWindow( state.window );
	}
	SDL_Quit();
}

} // anonymous namespace

/*
=================
main
=================
*/
int main( int argc, char *argv[] ) {
	(void)argc;
	(void)argv;

#ifdef RADIANT_IMGUI_SDL3
	if ( !SDL_Init( SDL_INIT_VIDEO | SDL_INIT_EVENTS ) ) {
#else
	if ( SDL_Init( SDL_INIT_VIDEO | SDL_INIT_EVENTS ) != 0 ) {
#endif
		ShowError( "Couldn't initialize SDL", SDL_GetError() );
		return 1;
	}

	radiantState_t state;
	if ( !CreateWindowAndContext( state ) || !InitImGui( state ) ) {
		Shutdown( state );
		return 1;
	}

	while ( !state.quit ) {
		ProcessEvents( state );
		if ( SDL_GetWindowFlags( state.window ) & SDL_WINDOW_MINIMIZED ) {
			SDL_Delay( 10 );
			continue;
		}
		Frame( state );
	}

	Shutdown( state );
	return 0;
}

#else // !ID_RADIANT_IMGUI: MFC Radiant

#include "tools/edit_gui_common.h"


#include "qe3.h"
#include "radiant.h"
#include "MainFrm.h"
#include "lightdlg.h"

#include <process.h>    // for _beginthreadex and _endthreadex
#include <ddeml.h>  // for MSGF_DDEMGR

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

idCVar radiant_entityMode( "radiant_entityMode", "0", CVAR_TOOL | CVAR_ARCHIVE, "" );

/////////////////////////////////////////////////////////////////////////////
// CRadiantApp

BEGIN_MESSAGE_MAP(CRadiantApp, CWinApp)
	//{{AFX_MSG_MAP(CRadiantApp)
	ON_COMMAND(ID_HELP, OnHelp)
	//}}AFX_MSG_MAP
	// Standard file based document commands
	ON_COMMAND(ID_FILE_NEW, CWinApp::OnFileNew)
	ON_COMMAND(ID_FILE_OPEN, CWinApp::OnFileOpen)
	// Standard print setup command
	ON_COMMAND(ID_FILE_PRINT_SETUP, CWinApp::OnFilePrintSetup)
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CRadiantApp construction

CRadiantApp::CRadiantApp()
{
	// TODO: add construction code here,
	// Place all significant initialization in InitInstance
}

/////////////////////////////////////////////////////////////////////////////
// The one and only CRadiantApp object

CRadiantApp theApp;
HINSTANCE g_DoomInstance = NULL;
bool g_editorAlive = false;

void RadiantPrint( const char *text ) {
	if ( g_editorAlive && g_Inspectors ) {
		if (g_Inspectors->consoleWnd.GetSafeHwnd()) {
			g_Inspectors->consoleWnd.AddText( text );
		}
	}
}

void RadiantShutdown( void ) {
	theApp.ExitInstance();
}

/*
=================
RadiantInit

This is also called when you 'quit' in doom
=================
*/
void RadiantInit( void ) {

	// make sure the renderer is initialized
	if ( !renderSystem->IsOpenGLRunning() ) {
		common->Printf( "no OpenGL running\n" );
		return;
	}

	g_editorAlive = true;

	// allocate a renderWorld and a soundWorld
	if ( g_qeglobals.rw == NULL ) {
		g_qeglobals.rw = renderSystem->AllocRenderWorld();
		g_qeglobals.rw->InitFromMap( NULL );
	}
	if ( g_qeglobals.sw == NULL ) {
		g_qeglobals.sw = soundSystem->AllocSoundWorld( g_qeglobals.rw );
	}

	if ( g_DoomInstance ) {
		if ( ::IsWindowVisible( win32.hWnd ) ) {
			::ShowWindow( win32.hWnd, SW_HIDE );
			g_pParentWnd->ShowWindow( SW_SHOW );
			g_pParentWnd->SetFocus();
		}
	} else {
		Sys_GrabMouseCursor( false );

		g_DoomInstance = win32.hInstance;

		InitAfx();

		CWinApp* pApp = AfxGetApp();
		CWinThread *pThread = AfxGetThread();

		// App global initializations (rare)
		pApp->InitApplication();

		// Perform specific initializations
		pThread->InitInstance();

		qglFinish();
		//qwglMakeCurrent(0, 0);
		qwglMakeCurrent(win32.hDC, win32.hGLRC);

		// hide the doom window by default
		::ShowWindow( win32.hWnd, SW_HIDE );
	}
}


extern void Map_VerifyCurrentMap(const char *map);

void RadiantSync( const char *mapName, const idVec3 &viewOrg, const idAngles &viewAngles ) {
	if ( g_DoomInstance == NULL ) {
		RadiantInit();
	}

	if ( g_DoomInstance ) {
		idStr osPath;
		osPath = fileSystem->RelativePathToOSPath( mapName );
		Map_VerifyCurrentMap( osPath );
		idAngles flip = viewAngles;
		flip.pitch = -flip.pitch;
		g_pParentWnd->GetCamera()->SetView( viewOrg, flip );
		g_pParentWnd->SetFocus();
		Sys_UpdateWindows( W_ALL );
		g_pParentWnd->RoutineProcessing();
	}
}

void RadiantRun( void ) {
	static bool exceptionErr = false;
	int show = ::IsWindowVisible(win32.hWnd);

	try {
		if (!exceptionErr && !show) {
			//qglPushAttrib(GL_ALL_ATTRIB_BITS);
			qglDepthMask(true);
			theApp.Run();
			//qglPopAttrib();
			//qwglMakeCurrent(0, 0);
			if (win32.hDC != NULL && win32.hGLRC != NULL)
				qwglMakeCurrent(win32.hDC, win32.hGLRC);
		}
	}
	catch( idException &ex ) {
		::MessageBox(NULL, ex.error, "Exception error", MB_OK);
		RadiantShutdown();
	}
}

/////////////////////////////////////////////////////////////////////////////
// CRadiantApp initialization

HINSTANCE g_hOpenGL32 = NULL;
HINSTANCE g_hOpenGL = NULL;
bool g_bBuildList = false;

BOOL CRadiantApp::InitInstance()
{
  //g_hOpenGL32 = ::LoadLibrary("opengl32.dll");
	// AfxEnableControlContainer();

	// Standard initialization
	// If you are not using these features and wish to reduce the size
	//  of your final executable, you should remove from the following
	//  the specific initialization routines you do not need.
  //AfxEnableMemoryTracking(FALSE);

#ifdef _AFXDLL
	//Enable3dControls();			// Call this when using MFC in a shared DLL
#else
	//Enable3dControlsStatic();	// Call this when linking to MFC statically
#endif

	// If there's a .INI file in the directory use it instead of registry

	char RadiantPath[_MAX_PATH];
	GetModuleFileName( NULL, RadiantPath, _MAX_PATH );

	// search for exe
	CFileFind Finder;
	Finder.FindFile( RadiantPath );
	Finder.FindNextFile();
	// extract root
	CString Root = Finder.GetRoot();
	// build root\*.ini
	CString IniPath = Root + "\\REGISTRY.INI";
	// search for ini file
	Finder.FindNextFile();
	if (Finder.FindFile( IniPath ))
	{
		Finder.FindNextFile();
		// use the .ini file instead of the registry
		free((void*)m_pszProfileName);
		m_pszProfileName=_tcsdup(_T(Finder.GetFilePath()));
		// look for the registry key for void* buffers storage ( these can't go into .INI files )
		int i=0;
		CString key;
		HKEY hkResult;
		DWORD dwDisp;
		DWORD type;
		char iBuf[3];
		do
		{
			sprintf( iBuf, "%d", i );
			key = "Software\\Q3Radiant\\IniPrefs" + CString(iBuf);
			// does this key exists ?
			if ( RegOpenKeyEx( HKEY_CURRENT_USER, key, 0, KEY_ALL_ACCESS, &hkResult ) != ERROR_SUCCESS )
			{
				// this key doesn't exist, so it's the one we'll use
				strcpy( g_qeglobals.use_ini_registry, key.GetBuffer(0) );
				RegCreateKeyEx( HKEY_CURRENT_USER, key, 0, NULL,
					REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hkResult, &dwDisp );
				RegSetValueEx( hkResult, "RadiantName", 0, REG_SZ, reinterpret_cast<CONST BYTE *>(RadiantPath), strlen( RadiantPath )+1 );
				RegCloseKey( hkResult );
				break;
			}
			else
			{
				char RadiantAux[ _MAX_PATH ];
				unsigned long size = _MAX_PATH;
				// the key exists, is it the one we are looking for ?
				RegQueryValueEx( hkResult, "RadiantName", 0, &type, reinterpret_cast<BYTE *>(RadiantAux), &size );
				RegCloseKey( hkResult );
				if ( !strcmp( RadiantAux, RadiantPath ) )
				{
					// got it !
					strcpy( g_qeglobals.use_ini_registry, key.GetBuffer(0) );
					break;
				}
			}
			i++;
		} while (1);
		g_qeglobals.use_ini = true;
	}
	else
	{
		// Change the registry key under which our settings are stored.
		SetRegistryKey( EDITOR_REGISTRY_KEY );
		g_qeglobals.use_ini = false;
	}

	LoadStdProfileSettings();  // Load standard INI file options (including MRU)


	// Register the application's document templates.  Document templates
	//  serve as the connection between documents, frame windows and views.

//	CMultiDocTemplate* pDocTemplate;
//	pDocTemplate = new CMultiDocTemplate(
//		IDR_RADIANTYPE,
//		RUNTIME_CLASS(CRadiantDoc),
//		RUNTIME_CLASS(CMainFrame), // custom MDI child frame
//		RUNTIME_CLASS(CRadiantView));
//	AddDocTemplate(pDocTemplate);

	// create main MDI Frame window

	g_PrefsDlg.LoadPrefs();

	qglEnableClientState( GL_VERTEX_ARRAY );

	CString strTemp = m_lpCmdLine;
	strTemp.MakeLower();
	if (strTemp.Find("builddefs") >= 0) {
		g_bBuildList = true;
	}

	CMainFrame* pMainFrame = new CMainFrame;
	if (!pMainFrame->LoadFrame(IDR_MENU_QUAKE3)) {
		return FALSE;
	}

	if (pMainFrame->m_hAccelTable) {
		::DestroyAcceleratorTable(pMainFrame->m_hAccelTable);
	}

	pMainFrame->LoadAccelTable(MAKEINTRESOURCE(IDR_MINIACCEL));

	m_pMainWnd = pMainFrame;

	// The main window has been initialized, so show and update it.
	pMainFrame->ShowWindow(m_nCmdShow);
	pMainFrame->UpdateWindow();

	return TRUE;
}

/////////////////////////////////////////////////////////////////////////////
// CRadiantApp commands

int CRadiantApp::ExitInstance()
{
	common->Shutdown();
	g_pParentWnd = NULL;
	int ret = CWinApp::ExitInstance();
	ExitProcess(0);
	return ret;
}


BOOL CRadiantApp::OnIdle(LONG lCount) {
	if (g_pParentWnd) {
		g_pParentWnd->RoutineProcessing();
	}
	return FALSE;
	//return CWinApp::OnIdle(lCount);
}

void CRadiantApp::OnHelp()
{
	ShellExecute(m_pMainWnd->GetSafeHwnd(), "open", "http://www.idDevNet.com", NULL, NULL, SW_SHOW);
}

int CRadiantApp::Run( void )
{
	BOOL bIdle = TRUE;
	LONG lIdleCount = 0;


#if _MSC_VER >= 1300
	MSG *msg = AfxGetCurrentMessage();			// TODO Robert fix me!!
#else
	MSG *msg = &m_msgCur;
#endif

	// phase1: check to see if we can do idle work
	while (bIdle &&	!::PeekMessage(msg, NULL, NULL, NULL, PM_NOREMOVE)) {
		// call OnIdle while in bIdle state
		if (!OnIdle(lIdleCount++)) {
			bIdle = FALSE; // assume "no idle" state
		}
	}

	// phase2: pump messages while available
	do {
		// pump message, but quit on WM_QUIT
		if (!PumpMessage()) {
			return ExitInstance();
		}

		// reset "no idle" state after pumping "normal" message
		if (IsIdleMessage(msg)) {
			bIdle = TRUE;
			lIdleCount = 0;
		}

	} while (::PeekMessage(msg, NULL, NULL, NULL, PM_NOREMOVE));

	return 0;
}


/*
=============================================================

REGISTRY INFO

=============================================================
*/

bool SaveRegistryInfo(const char *pszName, void *pvBuf, long lSize)
{
	SetCvarBinary(pszName, pvBuf, lSize);
	common->WriteFlaggedCVarsToFile( "editor.cfg", CVAR_TOOL, "sett" );
	return true;
}

bool LoadRegistryInfo(const char *pszName, void *pvBuf, long *plSize)
{
	return GetCvarBinary(pszName, pvBuf, *plSize);
}

bool SaveWindowState(HWND hWnd, const char *pszName)
{
	RECT rc;
	GetWindowRect(hWnd, &rc);
	if (hWnd != g_pParentWnd->GetSafeHwnd()) {
		if (::GetParent(hWnd) != g_pParentWnd->GetSafeHwnd()) {
		  ::SetParent(hWnd, g_pParentWnd->GetSafeHwnd());
		}
		MapWindowPoints(NULL, g_pParentWnd->GetSafeHwnd(), (POINT *)&rc, 2);
	}
	return SaveRegistryInfo(pszName, &rc, sizeof(rc));
}


bool LoadWindowState(HWND hWnd, const char *pszName)
{
	RECT rc;
	LONG lSize = sizeof(rc);

	if (LoadRegistryInfo(pszName, &rc, &lSize))
	{
		if (rc.left < 0)
			rc.left = 0;
		if (rc.top < 0)
			rc.top = 0;
		if (rc.right < rc.left + 16)
			rc.right = rc.left + 16;
		if (rc.bottom < rc.top + 16)
			rc.bottom = rc.top + 16;

		MoveWindow(hWnd, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, FALSE);
		return true;
	}

	return false;
}

/*
===============================================================

  STATUS WINDOW

===============================================================
*/

void Sys_UpdateStatusBar( void )
{
	extern int   g_numbrushes, g_numentities;

	char numbrushbuffer[100] = "";

	sprintf( numbrushbuffer, "Brushes: %d Entities: %d", g_numbrushes, g_numentities );
	Sys_Status( numbrushbuffer, 2 );
}

void Sys_Status(const char *psz, int part )
{
	if ( part < 0 ) {
		common->Printf("%s", psz);
		part = 0;
	}
	g_pParentWnd->SetStatusText(part, psz);
}

#endif // ID_RADIANT_IMGUI
