#include "MonsterMeshSerial.h"
#include <Preferences.h>

bool g_mmSerialQuiet = false;

void mmSerialSetQuiet(bool on)
{
    g_mmSerialQuiet = on;
    Preferences p;
    if (p.begin("monstermesh", false)) {
        p.putBool("quiet", on);
        p.end();
    }
}

// Called once at boot from MonsterMeshModule setup to restore persisted state.
static bool s_loaded = false;
void mmSerialLoadPersisted()
{
    if (s_loaded) return;
    s_loaded = true;
    Preferences p;
    if (p.begin("monstermesh", true)) {
        g_mmSerialQuiet = p.getBool("quiet", false);
        p.end();
    }
}
