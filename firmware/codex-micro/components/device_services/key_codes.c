#include "key_codes.h"
#include <string.h>
#include <stdlib.h>
typedef struct { const char *name; uint16_t usage; } entry_t;
static const entry_t consumer[]={
    {"MPLY",0xcd},{"MUTE",0xe2},{"VOLU",0xe9},{"VOLD",0xea},
    {"MNXT",0xb5},{"MPRV",0xb6},{"MSTP",0xb7},{"EJCT",0xb8},
    {"MFFD",0xb3},{"MRWD",0xb4},{"MSEL",0x183},{"BRIU",0x6f},{"BRID",0x70}
};
uint16_t key_code_consumer_usage(unsigned index)
{ return index<KEY_CONSUMER_COUNT?consumer[index].usage:0; }
bool key_code_lookup(const char *t,uint8_t *report,uint16_t *usage)
{
    if (!t || !report || !usage) return false;
    *report=0; *usage=0;
    if (!strcmp(t,"KC_NONE") || !strcmp(t,"KC_NO")) return true;
    if (strncmp(t,"KC_",3)) return false;
    t+=3;
    if (t[0] && !t[1]) {
        if (*t>='A' && *t<='Z') { *report=1; *usage=4+*t-'A'; return true; }
        if (*t>='1' && *t<='9') { *report=1; *usage=30+*t-'1'; return true; }
        if (*t=='0') { *report=1; *usage=39; return true; }
    }
    if (t[0]=='F' && t[1]>='1' && t[1]<='9') {
        char *end; long n=strtol(t+1,&end,10);
        if (!*end && n>=1 && n<=24) { *report=1; *usage=n<=12?57+n:91+n; return true; }
    }
    static const entry_t keyboard[]={
        {"ENT",40},{"ESC",41},{"BSPC",42},{"TAB",43},{"SPC",44},
        {"MINS",45},{"EQL",46},{"LBRC",47},{"RBRC",48},{"BSLS",49},{"NUHS",50},
        {"SCLN",51},{"QUOT",52},{"GRV",53},{"COMM",54},{"DOT",55},{"SLSH",56},
        {"CAPS",57},{"PSCR",70},{"SLCK",71},{"PAUS",72},{"INS",73},{"HOME",74},
        {"PGUP",75},{"DEL",76},{"END",77},{"PGDN",78},{"RGHT",79},{"LEFT",80},
        {"DOWN",81},{"UP",82},{"NLCK",83},{"PSLS",84},{"PAST",85},{"PMNS",86},
        {"PPLS",87},{"PENT",88},{"P1",89},{"P2",90},{"P3",91},{"P4",92},
        {"P5",93},{"P6",94},{"P7",95},{"P8",96},{"P9",97},{"P0",98},{"PDOT",99},
        {"NUBS",100},{"APP",101},{"KB_POWER",102},{"PEQL",103},{"EXEC",116},
        {"HELP",117},{"MENU",118},{"SELECT",119},{"STOP",120},{"AGAIN",121},
        {"UNDO",122},{"CUT",123},{"COPY",124},{"PASTE",125},{"FIND",126},
        {"LCAP",130},{"LSCR",132},{"PCMM",133},
        {"LCTL",224},{"LSFT",225},{"LALT",226},{"LGUI",227},
        {"RCTL",228},{"RSFT",229},{"RALT",230},{"RGUI",231}
    };
    for (unsigned i=0;i<sizeof(keyboard)/sizeof(keyboard[0]);++i) if (!strcmp(t,keyboard[i].name)) {
        *report=1; *usage=keyboard[i].usage; return true;
    }
    for (unsigned i=0;i<KEY_CONSUMER_COUNT;++i) if (!strcmp(t,consumer[i].name)) {
        *report=2; *usage=consumer[i].usage; return true;
    }
    const char *system[]={"PWR","SLEP","WAKE"};
    for (unsigned i=0;i<3;++i) if (!strcmp(t,system[i])) { *report=3; *usage=i; return true; }
    return false;
}
