// =================================================================================================================================
//
// Copyright (C) 2016 Jarmo Nikkanen
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation 
// files (the "Software"), to use, copy, modify, merge, publish, distribute, interact with the Software and sublicense
// copies of the Software, subject to the following conditions:
//
// a) You do not sell, rent or auction the Software.
// b) You do not collect distribution fees.
// c) You do not remove or alter any copyright notices contained within the Software.
// d) This copyright notice must be included in all copies or substantial portions of the Software.
//
// If the Software is distributed in an object code form then in addition to conditions above:
// e) It must inform that the source code is available and how to obtain it.
// f) It must display "NO WARRANTY" and "DISCLAIMER OF LIABILITY" statements on behalf of all contributors like the one below.
//
// The accompanying materials such as artwork, if any, are provided under the terms of this license unless otherwise noted. 
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
// OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
// IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
// =================================================================================================================================

#define STRICT
#define ORBITER_MODULE

#include "windows.h"
#include "orbitersdk.h"
#include "MFD.h"
#include "gcAPI.h"
#include "Sketchpad2.h"

// ============================================================================================================
// Global variables

int g_MFDmode; // identifier for new MFD mode


// ============================================================================================================
// API interface

DLLCLBK void InitModule (HINSTANCE hDLL)
{
	static char *name = "Generic Camera";   // MFD mode name
	MFDMODESPECEX spec;
	spec.name = name;
	spec.key = OAPI_KEY_C;                // MFD mode selection key
	spec.context = NULL;
	spec.msgproc = CameraMFD::MsgProc;  // MFD mode callback function

	// Register the new MFD mode with Orbiter
	g_MFDmode = oapiRegisterMFDMode (spec);
}

// ============================================================================================================
//
DLLCLBK void ExitModule (HINSTANCE hDLL)
{
	// Unregister the custom MFD mode when the module is unloaded
	oapiUnregisterMFDMode (g_MFDmode);
}


// ============================================================================================================
// MFD class implementation

CameraMFD::CameraMFD(DWORD w, DWORD h, VESSEL *vessel)
: MFD2 (w, h, vessel)
{
	font = oapiCreateFont (w/20, true, "Arial", (FontStyle)(FONT_BOLD | FONT_ITALIC), 450);
	
	hRenderSrf = NULL;
	hTexture = NULL;
	hCamera = NULL;
	hDock = NULL;
	hAttach = NULL;
	hFocus = vessel;
	hVessel = vessel;

	index = 0;
	type = 0;
	fov = 30.0;	// fov (i.e. Aparture) which is 1/2 of the vertical fov see oapiCameraAperture()
	offset = 0.0;
	bParent = false;
	bNightVis = false;

	if (gcInitialize()) {

		hTexture = oapiLoadTexture("DG/dg_instr.dds");

		// Create 3D render target
		hRenderSrf = oapiCreateSurfaceEx(w, h, OAPISURFACE_RENDER3D|OAPISURFACE_TEXTURE|OAPISURFACE_RENDERTARGET|OAPISURFACE_NOMIPMAPS);

		// Clear the surface
		oapiClearSurface(hRenderSrf);	
	}

	SelectVessel(hVessel, 1);
}

// ============================================================================================================
//
CameraMFD::~CameraMFD()
{
	oapiReleaseFont(font);

	// Attention, Always delete the camera before the surface !!!
	if (hCamera) gcDeleteCustomCamera(hCamera);
	if (hRenderSrf) oapiDestroySurface(hRenderSrf);
	if (hTexture) oapiReleaseTexture(hTexture);
}

// ============================================================================================================
//
void CameraMFD::SelectVessel(VESSEL *hVes, int _type)
{
	VECTOR3 pos, dir, rot;

	pos = _V(0, 0, 0);
	dir = _V(1, 0, 0);
	rot = _V(0, 1, 0);

	type = _type;

	if (hVes != hVessel) {
		// New Vessel Selected
		offset = 0.0;
		index = 0;
		type = 0;
	}

	hVessel = hVes;



	int nDock = hVessel->DockCount();
	int nAtch = hVessel->AttachmentCount(bParent);

	if (nDock == 0 && nAtch == 0) return;

	if (type == 0 && nAtch == 0) type = 1;
	if (type == 1 && nDock == 0) type = 0;

	if (fov < 5.0) fov = 5.0;
	if (fov > 70.0) fov = 70.0;

	// Attachemnts
	if (type == 0) {

		if (index < 0) index = nAtch - 1;
		if (index >= nAtch) index = 0;

		hAttach = hVessel->GetAttachmentHandle(bParent, index);

		if (hAttach) {
			hVessel->GetAttachmentParams(hAttach, pos, dir, rot);
			pos += dir * offset;
		}
		else return;
	}

	// Docking ports
	if (type == 1) {

		if (index < 0) index = nDock - 1;
		if (index >= nDock) index = 0;

		hDock = hVessel->GetDockHandle(index);

		if (hDock) {
			hVessel->GetDockParams(hDock, pos, dir, rot);
			pos += dir * offset;
		}
		else return;
	}

	// Actual rendering of the camera view into hRenderSrf will occur when the client is ready for it and 
	// a lagg of a few frames may occur depending about graphics/performance options.
	// Update will continue untill the camera is turned off via ogciCustomCameraOnOff() or deleted via ogciDeleteCustomCamera()
	// Camera orientation can be changed by calling this function again with an existing camera handle instead of NULL.

	hCamera = gcSetupCustomCamera(hCamera, hVessel->GetHandle(), pos, dir, rot, fov*PI / 180.0, hRenderSrf, 0xFF);
}


// ============================================================================================================
//
char *CameraMFD::ButtonLabel (int bt)
{
	// The labels for the two buttons used by our MFD mode
	static char *label[] = {"NA", "PA", "ND", "PD", "FWD", "BWD", "VES", "NV", "ZM+", "ZM-", "PAR"};
	return (bt < ARRAYSIZE(label) ? label[bt] : 0);
}


// ============================================================================================================
//
int CameraMFD::ButtonMenu (const MFDBUTTONMENU **menu) const
{
	// The menu descriptions for the two buttons
	static const MFDBUTTONMENU mnu[] = {
		{ "Next attachment", 0, '1' },
		{ "Prev attachment", 0, '2' },
		{ "Next dockport", 0, '3' },
		{ "Prev dockport", 0, '4' },
		{ "Move Forward", 0, '5' },
		{ "Move Backwards", 0, '6' },
		{ "Select Vessel", 0, '7' },
		{ "Night Vision", 0, '8' },
		{ "Zoom In", 0, '9' },
		{ "Zoom Out", 0, '0' },
		{ "Parent Mode", 0, 'B' }
	};

	if (menu) *menu = mnu;

	return ARRAYSIZE(mnu); // return the number of buttons used
}


// ============================================================================================================
//
bool CameraMFD::Update(oapi::Sketchpad *skp)
{

	InvalidateDisplay();

	// Call to update attachments
	SelectVessel(hVessel, type);

	int nDock = hVessel->DockCount();
	int nAtch = hVessel->AttachmentCount(bParent);

	RECT sr;
	sr.left = 0; sr.top = 0;
	sr.right = W - 2; sr.bottom = H - 2;

	if (hRenderSrf && gcSketchpadVersion(skp) == 2) {

		oapi::Sketchpad3 *pSkp2 = (oapi::Sketchpad3 *)skp;

		if (bNightVis) {
			pSkp2->SetBrightness(&_FVECTOR4(0.0, 4.0, 0.0, 1));
			pSkp2->SetRenderParam(SKP3_PRM_GAMMA, &_FVECTOR4(0.5f, 0.5f, 0.5f, 1.0f));
			pSkp2->SetRenderParam(SKP3_PRM_NOISE, &_FVECTOR4(0.0f, 0.3f, 0.0f, 0.0f));
		}


		// Blit the camera view into the sketchpad.
		pSkp2->CopyRect(hRenderSrf, &sr, 1, 1);


		if (bNightVis) {
			pSkp2->SetBrightness(NULL);
			pSkp2->SetRenderParam(SKP3_PRM_GAMMA, NULL);
			pSkp2->SetRenderParam(SKP3_PRM_NOISE, NULL);
		}

		// The source texture would need to have an alpha channel to get this work properly.
		//pSkp2->RotateRect(hTexture, &sr, W/2, H/2, float(pV->GetYaw()), 1.0f, 1.0f);
	}
	else {
		static char *msg = { "No Graphics API" };
		skp->SetTextAlign(Sketchpad::TAlign_horizontal::CENTER);
		skp->Text(W / 2, H / 2, msg, strlen(msg));
		return true;
	}

	if (nDock == 0 && nAtch == 0) {
		static char *msg = { "No Dock/Attachment points" };
		skp->SetTextAlign(Sketchpad::TAlign_horizontal::CENTER);
		skp->Text(W / 2, H / 2, msg, strlen(msg));
		return true;
	}

	skp->SetTextAlign();

	char text[256];
	static char* mode[] = { "Attach(", "Dock(" };
	static char* paci[] = { "Child", "Parent" };

	sprintf_s(text, 256, "Viewing %s %s%d)", hVessel->GetName(), mode[type], index);

	if (gcSketchpadVersion(skp) == 2) {
		oapi::Sketchpad2 *pSkp2 = (oapi::Sketchpad2 *)skp;
		pSkp2->QuickBrush(0x80000000);
		pSkp2->QuickPen(0);
		pSkp2->Rectangle(1, 1, W - 1, 25);
		pSkp2->Rectangle(1, H - 25, W - 1, H - 1);
	}

	Title (skp, text);

	sprintf_s(text, 256, "[%s] FOV=%0.0f� Ofs=%2.2f[m]", paci[bParent], fov*2.0, offset);

	skp->Text(10, H - 25, text, strlen(text));
	
	return true;
}


// ============================================================================================================
//
bool CameraMFD::ConsumeKeyBuffered(DWORD key)
{
	
	switch (key) {


	case OAPI_KEY_1:	// Next Attachment
		index++;
		SelectVessel(hVessel, 0);
		return true;


	case OAPI_KEY_2:	// Prev Attachment
		index--;
		SelectVessel(hVessel, 0);
		return true;


	case OAPI_KEY_3:	// Next dock
		index++;
		SelectVessel(hVessel, 1);
		return true;


	case OAPI_KEY_4:	// Prev dock
		index--;
		SelectVessel(hVessel, 1);
		return true;


	case OAPI_KEY_5:	// Move forward
		offset += 0.1;
		SelectVessel(hVessel, type);
		return true;


	case OAPI_KEY_6:	// Move backwards
		offset -= 0.1;
		SelectVessel(hVessel, type);
		return true;


	case OAPI_KEY_7:	// Select Vessel
		oapiOpenInputBox("Keyboard Input:", DataInput, 0, 32, (void*)this);
		return true;


	case OAPI_KEY_8:	// Night vision toggle
		bNightVis = !bNightVis;
		return true;


	case OAPI_KEY_9:	// Zoom in
		fov -= 5.0;
		SelectVessel(hVessel, type);
		return true;


	case OAPI_KEY_0:	// Zoom out
		fov += 5.0;
		SelectVessel(hVessel, type);
		return true;

	case OAPI_KEY_B:	// Parent/Child
		bParent = !bParent;
		SelectVessel(hVessel, type);
		return true;

	}

	return false;
}


// ============================================================================================================
//
bool CameraMFD::ConsumeButton(int bt, int event)
{
	static const DWORD btkey[12] = { OAPI_KEY_1, OAPI_KEY_2, OAPI_KEY_3, OAPI_KEY_4, OAPI_KEY_5, OAPI_KEY_6,
		OAPI_KEY_7, OAPI_KEY_8, OAPI_KEY_9, OAPI_KEY_0, OAPI_KEY_B, OAPI_KEY_X };

	if (event&PANEL_MOUSE_LBDOWN) {					
		return ConsumeKeyBuffered(btkey[bt]);
	}

	return false;
}


// ============================================================================================================
//
bool CameraMFD::DataInput(void *id, char *str)
{
	OBJHANDLE hObj = oapiGetVesselByName(str);

	if (hObj) {
		SelectVessel(oapiGetVesselInterface(hObj), type);
		return true;
	}

	return false;
}


// ============================================================================================================
// MFD message parser
int CameraMFD::MsgProc (UINT msg, UINT mfd, WPARAM wparam, LPARAM lparam)
{
	switch (msg) {
	case OAPI_MSG_MFD_OPENED:
		// Our new MFD mode has been selected, so we create the MFD and
		// return a pointer to it.
		return (int)(new CameraMFD(LOWORD(wparam), HIWORD(wparam), (VESSEL*)lparam));
	}
	return 0;
}


// ============================================================================================================
//
bool CameraMFD::DataInput(void *id, char *str, void *data)
{
	return ((CameraMFD*)data)->DataInput(id, str);
}

