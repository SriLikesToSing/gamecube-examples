
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <math.h>
#include <gccore.h>

#define DEFAULT_FIFO_SIZE	(256*1024)

struct camera
{
	Vector pos;
	Vector up;
	Vector view;
};
s16 square[] ATTRIBUTE_ALIGN(32) =
{
	// x y z
	-30,  30,  0, 	// 0
	 30,  30,  0, 	// 1
	 30, -30,  0, 	// 2
	-30, -30,  0, 	// 3
};

// color data
u8 colors[] ATTRIBUTE_ALIGN(32) =
{
	// r, g, b, a
	0,  255, 0, 0, // 0 purple
	240,   0,   0, 255,	// 1 red
	255, 180,   0, 255,	// 2 orange
	255, 255,   0, 255, // 3 yellow
	 10, 120,  40, 255, // 4 green
	  0,  20, 100, 255  // 5 blue
};

struct tex
{
	u8 *data;
	long width;
	long height;
	long fmt;
};

extern tex texture_test;

static float rotby=0;


static void *xfb = NULL;
static u32 do_copy = GX_FALSE;
GXRModeObj *rmode;
GXTexObj texObj;
camera cam = {{0.0F, 0.0F, 0.0F},
			  {0.0F, 1.0F, 0.0F},
		  	  {0.0F, 0.0F, -1.0F}};

void draw_init();
void draw_vert(u8 pos, u8 c, f32 s, f32 t);
void draw_square(Mtx v);
static void copy_to_xfb(u32 count);
void movecamera(float speed);


int main()
{
	Mtx v,p; // view and perspective matrices
	PADStatus pad[4];
	GXColor background = {0, 0, 0, 0xff};

	VIDEO_Init();

	switch(VIDEO_GetCurrentTvMode())
	{
		case VI_NTSC:
			rmode = &TVNtsc480IntDf;
			break;
		case VI_PAL:
			rmode = &TVPal528IntDf;
			break;
		case VI_MPAL:
			rmode = &TVMpal480IntDf;
			break;
		default:
			rmode = &TVNtsc480IntDf;
			break;
	}

	PAD_Init();
	xfb = memalign(VIDEO_PadFramebufferWidth(rmode->fbWidth)*rmode->xfbHeight*VI_DISPLAY_PIX_SZ,32);

	VIDEO_Configure(rmode);
	VIDEO_SetNextFramebuffer(xfb);
	VIDEO_SetPostRetraceCallback(copy_to_xfb);
	VIDEO_SetBlack(FALSE);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if(rmode->viTVMode&VI_NON_INTERLACE) VIDEO_WaitVSync();

	void *gp_fifo = NULL;
	gp_fifo = MEM_K0_TO_K1(memalign(DEFAULT_FIFO_SIZE,32));
	memset(gp_fifo,0,DEFAULT_FIFO_SIZE);

	GX_Init(gp_fifo,DEFAULT_FIFO_SIZE);

	GX_SetCopyClear(background, 0x00ffffff);

	GX_SetViewport(0,0,rmode->fbWidth,rmode->efbHeight,0,1);
	GX_SetDispCopyYScale((f32)rmode->xfbHeight/(f32)rmode->efbHeight);
	GX_SetScissor(0,0,rmode->fbWidth,rmode->efbHeight);
	GX_SetDispCopySrc(0,0,rmode->fbWidth,rmode->efbHeight);
	GX_SetDispCopyDst(rmode->fbWidth,rmode->xfbHeight);
	GX_SetCopyFilter(rmode->aa,rmode->sample_pattern,GX_TRUE,rmode->vfilter);
	GX_SetFieldMode(rmode->field_rendering,((rmode->viHeight==2*rmode->xfbHeight)?GX_ENABLE:GX_DISABLE));

	if (rmode->aa)
        GX_SetPixelFmt(GX_PF_RGB565_Z16, GX_ZC_LINEAR);
    else
        GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);

	GX_SetCullMode(GX_CULL_NONE);
	GX_CopyDisp(xfb,GX_TRUE);
	GX_SetDispCopyGamma(GX_GM_1_0);

	guPerspective(p, 60, 1.33F, 10.0F, 1000.0F);
	GX_LoadProjectionMtx(p, GX_PERSPECTIVE);

	draw_init();

	while(1)
	{
		guLookAt(v, &cam.pos, &cam.up, &cam.view);

		GX_SetViewport(0,0,rmode->fbWidth,rmode->efbHeight,0,1);
		GX_InvVtxCache();
		GX_InvalidateTexAll();
		GX_SetTevOp(GX_TEVSTAGE0, GX_DECAL);
		GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);

		GX_SetNumChans(1);

		GX_LoadTexObj(&texObj, GX_TEXMAP0);

		draw_square(v);

		GX_DrawDone();
		do_copy = GX_TRUE;
		VIDEO_WaitVSync();

		if(pad[0].stickY > 18 || pad[0].stickY < -18)
			movecamera((float)pad[0].stickY/-18);

		PAD_Read(pad);
		if(pad[0].button&PAD_BUTTON_START) {
			void (*reload)() = (void(*)())0x80001800;
			reload();
		}
	}
	return 0;
}


void draw_init()
{
	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_INDEX8);
	GX_SetVtxDesc(GX_VA_CLR0, GX_INDEX8);
	GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);

	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_S16, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

	GX_SetArray(GX_VA_POS, square, 3*sizeof(s16));
	GX_SetArray(GX_VA_CLR0, colors, 4*sizeof(u8));

	GX_SetNumTexGens(1);
	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);

	GX_InvalidateTexAll();
	GX_InitTexObj(&texObj, texture_test.data, texture_test.width, texture_test.height, texture_test.fmt, GX_CLAMP, GX_CLAMP, GX_FALSE);
}

void draw_vert(u8 pos, u8 c, f32 s, f32 t)
{
	GX_Position1x8(pos);
	GX_Color1x8(c);
	GX_TexCoord2f32(s, t);
}

void draw_square(Mtx v)
{
	Mtx m; // model matrix.
	Mtx mv; // modelview matrix.
	Vector axis = {0,0,1};

	guMtxIdentity(m);
	guMtxRotAxisDeg(m, &axis, rotby);
	guMtxTransApply(m, m, 0, 0, -100);

	guMtxConcat(v,m,mv);
	GX_LoadPosMtxImm(mv, GX_PNMTX0);


	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
		draw_vert(0, 0, 0.0, 0.0);
		draw_vert(1, 0, 1.0, 0.0);
		draw_vert(2, 0, 1.0, 1.0);
		draw_vert(3, 0, 0.0, 1.0);
	GX_End();
}

// copy efb to xfb when ready
static void copy_to_xfb(u32 count)
{
	if(do_copy==GX_TRUE) {
		GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
		GX_SetColorUpdate(GX_TRUE);
		GX_CopyDisp(xfb,GX_TRUE);
		GX_Flush();
		do_copy = GX_FALSE;
	}
}

void movecamera(float speed)
{
	Vector v;

	v.x = cam.view.x - cam.pos.x;
	v.y = cam.view.y - cam.pos.y;
	v.z = cam.view.z - cam.pos.z;

	cam.pos.x += v.x * speed;
	cam.pos.z += v.z * speed;
	cam.view.x += v.x * speed;
    cam.view.z += v.z * speed;
}
