#include <stdio.h>
#include <stdlib.h>
#include <gccore.h>
#include <ogc/lwp_heap.h>
#include <ogc/cache.h>
#include <string.h>
#include <wiiuse/wpad.h>
#include "pngu.h"
#include "test_bin.h"
#include "mem2_manager.h"

//loader files
#include "patches.h"
#include "loader.h"

//Bin include
#include "loader_bin.h"

#include "background_png.h"

static void *xfb = NULL;
static GXRModeObj *rmode = NULL;

extern void __exception_closeall();

static u8 *homebrewbuffer = NULL;

int CopyHomebrewMemory(const u8 *temp, u32 pos, u32 len) {
	homebrewbuffer = mem2_memalign(32, len, OTHER_AREA);
	memcpy((homebrewbuffer) + pos, temp, len);

	return 1;
}

void FreeHomebrewBuffer() {
	homebrewbuffer = NULL;
}

// void load_Stub() {
// 	char *stubLoc = (char *)0x80001800;
// 	memcpy(stubLoc, stub_bin, stub_bin_size);
// 	DCFlushRange(stubLoc, stub_bin_size);
// }
//
// u8 hbcStubAvailable() {
// 	char *sig = (char *)0x80001804;
// 	return (strncmp(sig, "STUBHAXX", 8) == 0);
// }
//
// s32 Set_Stub(u64 reqID) {
// 	if (!hbcStubAvailable())
// 		return 0;
//
// 	char *stub = (char *)0x80002662;
//
// 	stub[0] = TITLE_7(reqID);
// 	stub[1] = TITLE_6(reqID);
// 	stub[8] = TITLE_5(reqID);
// 	stub[9] = TITLE_4(reqID);
// 	stub[4] = TITLE_3(reqID);
// 	stub[5] = TITLE_2(reqID);
// 	stub[12] = TITLE_1(reqID);
// 	stub[13] = ((u8)(reqID));
//
// 	DCFlushRange(stub, 0x10);
// 	return 1;
// }

int load_dol(const u8 *dol, u32 len) {
	void* loader_addr = NULL;
	loader_t loader = NULL;

	//prepare loader
	loader_addr = mem2_memalign(32, loader_bin_size, OTHER_AREA);

	memcpy(loader_addr, loader_bin, loader_bin_size);
	DCFlushRange(loader_addr, loader_bin_size);
	ICInvalidateRange(loader_addr, loader_bin_size);
	loader = (loader_t)loader_addr;

	ICSync();
	// loader(dol, args, args != NULL, 0);
	CopyHomebrewMemory(dol, 0, len);
	__IOS_ShutdownSubsystems();
	__exception_closeall();
	loader(homebrewbuffer, NULL, false, 0);

	return 0;
}

void Video_DrawPng(IMGCTX ctx, PNGUPROP imgProp, u16 x, u16 y) {
	PNGU_DECODE_TO_COORDS_YCbYCr(ctx, x, y, imgProp.imgWidth, imgProp.imgHeight, rmode->fbWidth, rmode->xfbHeight, xfb);
}

s32 Gui_DrawPng(const void *img, u32 x, u32 y) {
	IMGCTX ctx = NULL;
	PNGUPROP imgProp;
	s32 ret = -1;

	/* Select PNG data */
	ctx = PNGU_SelectImageFromBuffer(img);
	if (!ctx) {
		ret = -1;
		goto out;
	}

	/* Get image properties */
	ret = PNGU_GetImageProperties(ctx, &imgProp);
	if (ret != PNGU_OK) {
		ret = -1;
		goto out;
	}

	/* Draw image */
	Video_DrawPng(ctx, imgProp, x, y);

	/* Success */
	ret = 0;

out:
	/* Free memory */
	if (ctx)
		PNGU_ReleaseImageContext(ctx);

	return ret;
}

//---------------------------------------------------------------------------------
int main(int argc, char **argv) {
//---------------------------------------------------------------------------------

	// Initialise the video system
	VIDEO_Init();

	// This function initialises the attached controllers
	WPAD_Init();

	// Obtain the preferred video mode from the system
	// This will correspond to the settings in the Wii menu
	rmode = VIDEO_GetPreferredMode(NULL);

	// Allocate memory for the display in the uncached region
	xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));

	// Set up the video registers with the chosen mode
	VIDEO_Configure(rmode);


	// Tell the video hardware where our display memory is
	VIDEO_SetNextFramebuffer(xfb);

	// Clear the framebuffer
	VIDEO_ClearFrameBuffer(rmode, xfb, COLOR_BLACK);

	// Initialise the console, required for printf
	CON_InitEx(rmode,48,90,544,272);
	SYS_STDIO_Report(true);

	Gui_DrawPng(background_png, 0, 0);

	// Make the display visible
	VIDEO_SetBlack(false);

	// Flush the video register changes to the hardware
	VIDEO_Flush();

	// Wait for Video setup to complete
	VIDEO_WaitVSync();
	if (rmode->viTVMode&VI_NON_INTERLACE) VIDEO_WaitVSync();

	AddMem2Area(14 * 1024 * 1024, OTHER_AREA);

	// The console understands VT terminal escape codes
	// This positions the cursor on row 2, column 0
	// we can use variables for this with format codes too
	// e.g. printf ("\x1b[%d;%dH", row, column );


	printf("loading...\n");

	while(1) {

		// Call WPAD_ScanPads each loop, this reads the latest controller states
		WPAD_ScanPads();

		// WPAD_ButtonsDown tells us which buttons were pressed in this loop
		// this is a "one shot" state which will not fire again until the button has been released
		u32 pressed = WPAD_ButtonsDown(0);

		// We return to the launcher application via exit
		if ( pressed & WPAD_BUTTON_HOME ) load_dol(test_bin, test_bin_size);

		// Wait for the next frame
		VIDEO_WaitVSync();
	}

	return 0;
}
