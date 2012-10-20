#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <xetypes.h>
#include <input/input.h>
#include <usb/usbmain.h>

#include "input.h"

#include "winlnxdefs.h"
#include "main/main.h"
#include "api/m64p_plugin.h"
#include "r4300/r4300.h"

const char * pad_mode_name[]=
{
	"Stick / D-pad / C-buttons",
	"Stick / C-buttons / D-pad",
	"D-pad / Stick / C-buttons",
	"D-pad / C-buttons / Stick",
	"C-buttons / D-pad / Stick",
	"C-buttons / Stick / D-pad",
};

static CONTROL_INFO ControlInfo;

void initiateControllers(CONTROL_INFO Control_Info)
{
	int i;
	ControlInfo = Control_Info;
	for(i=0;i<4;++i) ControlInfo.Controls[i].Present = TRUE;
	ControlInfo.Controls[0].Plugin = PLUGIN_MEMPAK;
}

#define	STICK_DEAD_ZONE (32768*0.15)
#define	STICK_FACTOR (0.70)

#define TRIGGER_THRESHOLD 100
#define STICK_THRESHOLD 20000

s32 handleStickDeadZone(s32 value)
{
	if(abs(value)<STICK_DEAD_ZONE)
		return 0;

	int sign=-1;
	if (value>=0) sign=1;
	
	int dz=STICK_DEAD_ZONE*sign;
	
	return (value-dz)*STICK_FACTOR;
}


void getKeys(int Control, BUTTONS *Keys)
{
    static struct controller_data_s cdata[4],*c;
    BUTTONS b;

	usb_do_poll();

	get_controller_data(&cdata[Control], Control);
	c=&cdata[Control];

    b.START_BUTTON=c->start;
        
    b.A_BUTTON=c->a;
    b.B_BUTTON=c->b;
    b.Z_TRIG=c->x || c->y || (c->rt>TRIGGER_THRESHOLD) || (c->lt>TRIGGER_THRESHOLD);
        
    b.L_TRIG=c->lb;
    b.R_TRIG=c->rb;
     
	
	switch(pad_mode)
	{
		case PADMODE_SDC:
			b.X_AXIS=handleStickDeadZone(c->s1_x)/256;
			b.Y_AXIS=handleStickDeadZone(c->s1_y)/256;

			b.U_DPAD=c->up;
			b.D_DPAD=c->down;
			b.L_DPAD=c->left;
			b.R_DPAD=c->right;

			b.U_CBUTTON=c->s2_y>STICK_THRESHOLD;
			b.D_CBUTTON=c->s2_y<-STICK_THRESHOLD;
			b.L_CBUTTON=c->s2_x<-STICK_THRESHOLD;
			b.R_CBUTTON=c->s2_x>STICK_THRESHOLD;
			break;
		case PADMODE_SCD:
			b.X_AXIS=handleStickDeadZone(c->s1_x)/256;
			b.Y_AXIS=handleStickDeadZone(c->s1_y)/256;

			b.U_CBUTTON=c->up;
			b.D_CBUTTON=c->down;
			b.L_CBUTTON=c->left;
			b.R_CBUTTON=c->right;

			b.U_DPAD=c->s2_y>STICK_THRESHOLD;
			b.D_DPAD=c->s2_y<-STICK_THRESHOLD;
			b.L_DPAD=c->s2_x<-STICK_THRESHOLD;
			b.R_DPAD=c->s2_x>STICK_THRESHOLD;
			break;
		case PADMODE_DSC:
			b.U_DPAD=c->s1_y>STICK_THRESHOLD;
			b.D_DPAD=c->s1_y<-STICK_THRESHOLD;
			b.L_DPAD=c->s1_x<-STICK_THRESHOLD;
			b.R_DPAD=c->s1_x>STICK_THRESHOLD;

			b.X_AXIS=(c->left?-128:0) + (c->right?128:0);
			b.Y_AXIS=(c->up?-128:0) + (c->down?128:0);

			b.U_CBUTTON=c->s2_y>STICK_THRESHOLD;
			b.D_CBUTTON=c->s2_y<-STICK_THRESHOLD;
			b.L_CBUTTON=c->s2_x<-STICK_THRESHOLD;
			b.R_CBUTTON=c->s2_x>STICK_THRESHOLD;
			break;
		case PADMODE_DCS:
			b.U_DPAD=c->s1_y>STICK_THRESHOLD;
			b.D_DPAD=c->s1_y<-STICK_THRESHOLD;
			b.L_DPAD=c->s1_x<-STICK_THRESHOLD;
			b.R_DPAD=c->s1_x>STICK_THRESHOLD;

			b.U_CBUTTON=c->up;
			b.D_CBUTTON=c->down;
			b.L_CBUTTON=c->left;
			b.R_CBUTTON=c->right;

			b.X_AXIS=handleStickDeadZone(c->s2_x)/256;
			b.Y_AXIS=handleStickDeadZone(c->s2_y)/256;
			break;
		case PADMODE_CDS:
			b.U_CBUTTON=c->s1_y>STICK_THRESHOLD;
			b.D_CBUTTON=c->s1_y<-STICK_THRESHOLD;
			b.L_CBUTTON=c->s1_x<-STICK_THRESHOLD;
			b.R_CBUTTON=c->s1_x>STICK_THRESHOLD;

			b.U_DPAD=c->up;
			b.D_DPAD=c->down;
			b.L_DPAD=c->left;
			b.R_DPAD=c->right;

			b.X_AXIS=handleStickDeadZone(c->s2_x)/256;
			b.Y_AXIS=handleStickDeadZone(c->s2_y)/256;
			break;
		case PADMODE_CSD:
			b.U_CBUTTON=c->s1_y>STICK_THRESHOLD;
			b.D_CBUTTON=c->s1_y<-STICK_THRESHOLD;
			b.L_CBUTTON=c->s1_x<-STICK_THRESHOLD;
			b.R_CBUTTON=c->s1_x>STICK_THRESHOLD;

			b.X_AXIS=(c->left?-128:0) + (c->right?128:0);
			b.Y_AXIS=(c->up?-128:0) + (c->down?128:0);

			b.U_DPAD=c->s2_y>STICK_THRESHOLD;
			b.D_DPAD=c->s2_y<-STICK_THRESHOLD;
			b.L_DPAD=c->s2_x<-STICK_THRESHOLD;
			b.R_DPAD=c->s2_x>STICK_THRESHOLD;
			break;
	}

    Keys->Value = b.Value;
}

void controllerCommand(int Control, unsigned char *Command)
{

}

void readController(int Control, unsigned char *Command)
{
}

