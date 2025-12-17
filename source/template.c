#include <stdio.h>
#include <stdlib.h>
#include <gccore.h>
#include <string.h>
#include <wiiuse/wpad.h>
#include "pngu.h"
#include "cios_bin.h"
#include "app_booter_bin.h"
#include "stub_bin.h"
#include "background_png.h"

// #define ARAMSTART 0x8000
#define EXECUTE_ADDR ((u8 *)0x92000000)
#define BOOTER_ADDR ((u8 *)0x93000000)
#define ARGS_ADDR ((u8 *)0x93200000)
#define TITLE_1(x)	  ((u8)((x) >> 8))
#define TITLE_2(x)	  ((u8)((x) >> 16))
#define TITLE_3(x)	  ((u8)((x) >> 24))
#define TITLE_4(x)	  ((u8)((x) >> 32))
#define TITLE_5(x)	  ((u8)((x) >> 40))
#define TITLE_6(x)	  ((u8)((x) >> 48))
#define TITLE_7(x)	  ((u8)((x) >> 56))

static void *xfb = NULL;
static GXRModeObj *rmode = NULL;

typedef void (*entrypoint) (void);

extern void __exception_closeall();

static u8 *homebrewbuffer = EXECUTE_ADDR;
static u32 homebrewsize = 0;

int CopyHomebrewMemory(const u8 *temp, u32 pos, u32 len) {
	homebrewsize += len;
	memcpy((homebrewbuffer) + pos, temp, len);

	return 1;
}

void FreeHomebrewBuffer() {
	homebrewbuffer = EXECUTE_ADDR;
	homebrewsize = 0;

	// Arguments.clear();
}

static inline bool IsDolLZ(const u8 *buf) {
	return (buf[0x100] == 0x3C);
}

static inline bool IsSpecialELF(const u8 *buf) {
	return (*(u32 *)buf == 0x7F454C46 && buf[0x24] == 0);
}

void load_Stub() {
	char *stubLoc = (char *)0x80001800;
	memcpy(stubLoc, stub_bin, stub_bin_size);
	DCFlushRange(stubLoc, stub_bin_size);
}

u8 hbcStubAvailable() {
	char *sig = (char *)0x80001804;
	return (strncmp(sig, "STUBHAXX", 8) == 0);
}

s32 Set_Stub(u64 reqID) {
	if (!hbcStubAvailable())
		return 0;

	char *stub = (char *)0x80002662;

	stub[0] = TITLE_7(reqID);
	stub[1] = TITLE_6(reqID);
	stub[8] = TITLE_5(reqID);
	stub[9] = TITLE_4(reqID);
	stub[4] = TITLE_3(reqID);
	stub[5] = TITLE_2(reqID);
	stub[12] = TITLE_1(reqID);
	stub[13] = ((u8)(reqID));

	DCFlushRange(stub, 0x10);
	return 1;
}

int load_dol(const unsigned char *dol, unsigned int size_dol) {
	CopyHomebrewMemory(dol, 0, size_dol);

	DCFlushRange(homebrewbuffer, homebrewsize);
	ICInvalidateRange(homebrewbuffer, homebrewsize);

	memcpy(BOOTER_ADDR, app_booter_bin, app_booter_bin_size);
	DCFlushRange(BOOTER_ADDR, app_booter_bin_size);
	ICInvalidateRange(BOOTER_ADDR, app_booter_bin_size);

	entrypoint entry = (entrypoint)BOOTER_ADDR;

	load_Stub();
	Set_Stub(0x100000002LL);

	u32 level = IRQ_Disable();
	__exception_closeall();
	entry();
	IRQ_Restore(level);

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
	CON_InitEx(xfb,48,90,88,17);
	//SYS_STDIO_Report(true);
	Gui_DrawPng(background_png, 0, 0);

	// Make the display visible
	VIDEO_SetBlack(false);

	// Flush the video register changes to the hardware
	VIDEO_Flush();

	// Wait for Video setup to complete
	VIDEO_WaitVSync();
	if(rmode->viTVMode&VI_NON_INTERLACE) VIDEO_WaitVSync();


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
		if ( pressed & WPAD_BUTTON_HOME ) load_dol(cios_bin, cios_bin_size);

		// Wait for the next frame
		VIDEO_WaitVSync();
	}

	return 0;
}
