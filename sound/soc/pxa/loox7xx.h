#define LOOX_DAPM_NONE	0
#define LOOX_DAPM_ENUM	1
#define LOOX_DAPM_VOLSW	2

struct loox_ctl_ {
char *name;
unsigned int type;
unsigned int val;
};

/* kcontrols that are deactivated and thus not visible to higher software levels,
   especially mixer programs. Kcontrols with type other than LOOX_DAPM_NONE are explicitly set
   to value following, otherwise it is left as default (which should be 0)
*/
static const struct loox_ctl_ loox7xx_hidden_controls[] = {
{"3D Switch",LOOX_DAPM_NONE,0}, 
{ "3D Volume",LOOX_DAPM_NONE,0},
{"3D Lower Cut-off",LOOX_DAPM_NONE,0},
{"3D Upper Cut-off",LOOX_DAPM_NONE,0},
{"3D Mode",LOOX_DAPM_NONE,0},
{"Mono Mixer Left Playback Switch",LOOX_DAPM_NONE,0},
{"Mono Mixer Left Bypass Switch",LOOX_DAPM_NONE,0},
{"Mono Mixer Right Playback Switc",LOOX_DAPM_NONE,0},
{"Mono Mixer Right Bypass Switch",LOOX_DAPM_NONE,0},
{"Mono Playback Volume",LOOX_DAPM_NONE,0},
{"Mono Playback ZC Switch",LOOX_DAPM_NONE,0},
{"Bypass Right Playback Volume",LOOX_DAPM_NONE,0},
{"Bypass Left Playback Volume",LOOX_DAPM_NONE,0},
{"Bypass Mono Playback Volume",LOOX_DAPM_NONE,0},
{"ZC Timeout Switch",LOOX_DAPM_NONE,0},
{"Playback Invert Switch",LOOX_DAPM_NONE,0},
{"Right Speaker Playback Invert Switch",LOOX_DAPM_NONE,0},
{"Headphone Playback ZC Switch",LOOX_DAPM_NONE,0},
{"Speaker Playback ZC Switch",LOOX_DAPM_NONE,0},
{"Left Mixer Left Bypass Switch",LOOX_DAPM_NONE,0},
{"Left Mixer Right Playback Switc",LOOX_DAPM_NONE,0},
{"Left Mixer Right Bypass Switch",LOOX_DAPM_NONE,0},
{"Right Mixer Left Playback Switc",LOOX_DAPM_NONE,0},
{"Right Mixer Left Bypass Switch",LOOX_DAPM_NONE,0},
{"Right Mixer Right Bypass Switch",LOOX_DAPM_NONE,0},
{"Left Line Mux",LOOX_DAPM_NONE,0},
{"Right Line Mux",LOOX_DAPM_NONE,0},
{"Right PGA Mux",LOOX_DAPM_NONE,0},
{"Speaker Playback Volume",LOOX_DAPM_NONE,0},
{"Right Mixer Playback Switch",LOOX_DAPM_VOLSW,1},
{"Left Mixer Playback Switch",LOOX_DAPM_VOLSW,1},
{"Left PGA Mux", LOOX_DAPM_ENUM,0},
{"Left ADC Mux", LOOX_DAPM_ENUM,1},
{"Right ADC Mux", LOOX_DAPM_ENUM,1},
{"Differential Mux", LOOX_DAPM_ENUM,0},
{"Out3 Mux",LOOX_DAPM_ENUM,1},
};

/* the alsalib mixer layer is quite picky if it comes to name hints of controls to decide
   wether they are capture or playback related, so we rename the bogus ones to help.
   Note that alsa-lib >= 1.0.14 is needed for "Enum" controls to be correctly sorted into
   capture/playback category!
*/
static const char* loox7xx_renamed_controls[] = {
"ALC Capture Target Volume","ALC Capture Target Capture Volume",
"ALC Capture Max Volume", "ALC Capture Max Capture Volume",
"ALC Capture Function", "ALC Capture Function Capture Enum",
"ALC Capture ZC Switch", "ALC Capture ZC Capture Switch",
"ALC Capture Hold Time", "ALC Capture Hold Time Capture Volume",
"ALC Capture Decay Time", "ALC Capture Decay Time Capture Volume",
"ALC Capture Attack Time", "ALC Capture Attack Time Capture Volume",
"ALC Capture NG Threshold", "ALC Capture NG Threshold Capture Volume",
"ALC Capture NG Type", "ALC Capture NG Type Capture Enum",
"ALC Capture NG Switch", "ALC Capture NG Capture Switch",
"Capture Polarity", "Capture Polarity Capture Enum",
"Capture 6dB Attenuate", "Capture 6dB Attenuate Capture Volume",
"Capture ZC Switch","Capture ZC Capture Switch",
"Mic Boost", "Mic Boost Capture Volume",
"PCM Volume", "PCM Playback Volume",
"Bass Boost", "Bass Boost Playback Enum",
"Bass Filter", "Bass Filter Playback Enum",
"Bass Volume", "Bass Playback Volume",
"Treble Volume", "Treble Playback Volume",
"Treble Cut-off", "Treble Cut-off Playback Volume",
"Headphone Playback Volume","Volume Playback Volume",
};
