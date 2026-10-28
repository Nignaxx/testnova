// NovaDeck - launcher RetroArch / Vita / PSP pour PS Vita (VitaSDK + vita2d)
#include <psp2/appmgr.h>
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/net/http.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>
#include <vita2d.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

int _newlib_heap_size_user = 96 * 1024 * 1024;

#define ROOT "ux0:data/NovaDeck"
#define RETRO_CORES "ux0:app/RETROVITA"
#define SW 960
#define SH 544
#define CW 150
#define CH 200
#define GAP 18
#define MAXG 600

typedef struct { const char *id, *name, *dir, *exts, *core, *thumb; unsigned col; } Console;

// Index 0 = jeux Vita installes, 1 = PSP (via Adrenaline), le reste = RetroArch
static const Console C[] = {
 {"VITA","PS Vita","","","","",RGBA8(37,99,235,255)},
 {"PSP","PSP","PSP",".iso.cso",".","Sony - PlayStation Portable",RGBA8(124,58,237,255)},
 {"PS1","PS1","PS1",".cue.chd.pbp.m3u","pcsx_rearmed","Sony - PlayStation",RGBA8(107,114,128,255)},
 {"GBA","Game Boy Advance","GBA",".gba","mgba","Nintendo - Game Boy Advance",RGBA8(147,51,234,255)},
 {"GBC","Game Boy Color","GBC",".gbc","gambatte","Nintendo - Game Boy Color",RGBA8(22,163,74,255)},
 {"GB","Game Boy","GB",".gb","gambatte","Nintendo - Game Boy",RGBA8(101,163,13,255)},
 {"MD","Mega Drive","MD",".md.gen.smd.bin","genesis_plus_gx","Sega - Mega Drive - Genesis",RGBA8(220,38,38,255)},
 {"SNES","Super Nintendo","SNES",".sfc.smc","snes9x2005","Nintendo - Super Nintendo Entertainment System",RGBA8(234,88,12,255)},
 {"NES","NES","NES",".nes","fceumm","Nintendo - Nintendo Entertainment System",RGBA8(185,28,28,255)},
 {"MS","Master System","MS",".sms","genesis_plus_gx","Sega - Master System - Mark III",RGBA8(190,18,60,255)},
 {"GG","Game Gear","GG",".gg","genesis_plus_gx","Sega - Game Gear",RGBA8(13,148,136,255)},
 {"PCE","PC Engine","PCE",".pce","mednafen_pce_fast","NEC - PC Engine - TurboGrafx 16",RGBA8(217,119,6,255)},
 {"NEO","Neo Geo","NEO",".zip","fbalpha2012","SNK - Neo Geo",RGBA8(202,138,4,255)},
 {"MAME","MAME / Arcade","MAME",".zip","mame2003_plus","MAME",RGBA8(219,39,119,255)},
 {"A26","Atari 2600","A26",".a26","stella2014","Atari - 2600",RGBA8(146,64,14,255)},
 {"WS","WonderSwan","WS",".ws.wsc","mednafen_wswan","Bandai - WonderSwan",RGBA8(8,145,178,255)},
 {"LYNX","Atari Lynx","LYNX",".lnx","handy","Atari - Lynx",RGBA8(77,124,15,255)},
};
#define NC ((int)(sizeof(C)/sizeof(C[0])))

typedef struct { char name[80]; char path[256]; char id[12]; vita2d_texture *cov; int nocov; } Game;

static Game *G[NC];
static int NG[NC];
static int ci = 0, gi = 0;
static float fpos = 0, tabpos = 0;
static vita2d_pgf *font;
static unsigned old_btn = 0;
static int net_ok = 0;

static void txt(int x, int y, unsigned c, float s, const char *t) { vita2d_pgf_draw_text(font, x, y, c, s, t); }
static void txtc(int y, unsigned c, float s, const char *t) {
    int w = vita2d_pgf_text_width(font, s, t);
    txt((SW - w) / 2, y, c, s, t);
}
static void clipname(char *dst, const char *src, int max) {
    int n = (int)strlen(src);
    if (n <= max) { strcpy(dst, src); return; }
    memcpy(dst, src, max - 2); dst[max - 2] = '.'; dst[max - 1] = '.'; dst[max] = 0;
}
static int exists(const char *p) { SceIoStat st; return sceIoGetstat(p, &st) >= 0; }
static void mk(const char *p) { sceIoMkdir(p, 0777); }

static void cover_path(char *o, int n, int c, const char *name) {
    snprintf(o, n, ROOT "/covers/%s/%s.png", C[c].id, name);
}

static void setup_dirs(void) {
    char p[200];
    mk("ux0:data"); mk(ROOT); mk(ROOT "/roms"); mk(ROOT "/covers");
    for (int i = 1; i < NC; i++) {
        snprintf(p, sizeof p, ROOT "/covers/%s", C[i].id); mk(p);
        if (i > 1 || 1) { snprintf(p, sizeof p, ROOT "/roms/%s", C[i].dir); mk(p); }
    }
}

// ---- lecture param.sfo (titre des jeux Vita) ----
static int sfo_title(const char *id, char *out, int n) {
    static unsigned char buf[16384];
    char p[128];
    snprintf(p, sizeof p, "ux0:app/%s/sce_sys/param.sfo", id);
    SceUID fd = sceIoOpen(p, SCE_O_RDONLY, 0);
    if (fd < 0) return 0;
    int r = sceIoRead(fd, buf, sizeof buf);
    sceIoClose(fd);
    if (r < 20 || memcmp(buf, "\0PSF", 4)) return 0;
    unsigned kt = *(unsigned *)(buf + 8), dt = *(unsigned *)(buf + 12), cnt = *(unsigned *)(buf + 16);
    for (unsigned i = 0; i < cnt && 20 + i * 16 + 16 <= (unsigned)r; i++) {
        unsigned char *e = buf + 20 + i * 16;
        unsigned ko = *(unsigned short *)e, doff = *(unsigned *)(e + 12);
        if (kt + ko < (unsigned)r && !strcmp((char *)buf + kt + ko, "TITLE")) {
            if (dt + doff >= (unsigned)r) return 0;
            snprintf(out, n, "%s", (char *)buf + dt + doff);
            for (char *q = out; *q; q++) if (*q == '\n' || *q == '\r') *q = ' ';
            return 1;
        }
    }
    return 0;
}

static int cmp(const void *a, const void *b) { return strcasecmp(((const Game *)a)->name, ((const Game *)b)->name); }

static int ext_ok(const char *exts, const char *file) {
    const char *dot = strrchr(file, '.');
    if (!dot) return 0;
    char e[12]; int n = 0;
    for (; dot[n] && n < 10; n++) e[n] = tolower((unsigned char)dot[n]);
    e[n] = 0;
    const char *f = exts;
    while ((f = strstr(f, e))) {
        char after = f[n];
        if (after == 0 || after == '.') return 1;
        f++;
    }
    return 0;
}

static void scan(int c) {
    G[c] = calloc(MAXG, sizeof(Game));
    NG[c] = 0;
    char dir[200];
    if (c == 0) snprintf(dir, sizeof dir, "ux0:app");
    else snprintf(dir, sizeof dir, ROOT "/roms/%s", C[c].dir);
    SceUID d = sceIoDopen(dir);
    if (d < 0) return;
    SceIoDirent e;
    memset(&e, 0, sizeof e);
    while (sceIoDread(d, &e) > 0 && NG[c] < MAXG) {
        Game *g = &G[c][NG[c]];
        int isdir = SCE_S_ISDIR(e.d_stat.st_mode);
        if (c == 0) {
            if (!isdir || !strcmp(e.d_name, "NOVADECK1") || !strcmp(e.d_name, "RETROVITA")) continue;
            snprintf(g->id, sizeof g->id, "%s", e.d_name);
            if (!sfo_title(e.d_name, g->name, sizeof g->name)) continue;
        } else {
            if (isdir || !ext_ok(C[c].exts, e.d_name)) continue;
            snprintf(g->path, sizeof g->path, "%s/%s", dir, e.d_name);
            snprintf(g->name, sizeof g->name, "%s", e.d_name);
            char *dot = strrchr(g->name, '.'); if (dot) *dot = 0;
        }
        NG[c]++;
        memset(&e, 0, sizeof e);
    }
    sceIoDclose(d);
    qsort(G[c], NG[c], sizeof(Game), cmp);
}

static void load_cov(Game *g) {
    if (g->cov || g->nocov) return;
    char p[300];
    if (ci == 0) snprintf(p, sizeof p, "ux0:app/%s/sce_sys/icon0.png", g->id);
    else cover_path(p, sizeof p, ci, g->name);
    g->cov = vita2d_load_PNG_file(p);
    if (!g->cov) g->nocov = 1;
}

static void free_far(int c, int keep_from, int keep_to) {
    int need = 0;
    for (int i = 0; i < NG[c]; i++) if (G[c][i].cov && (i < keep_from || i > keep_to)) need = 1;
    if (!need) return;
    vita2d_wait_rendering_done();
    for (int i = 0; i < NG[c]; i++)
        if (G[c][i].cov && (i < keep_from || i > keep_to)) { vita2d_free_texture(G[c][i].cov); G[c][i].cov = NULL; }
}

// ---- dessin ----
static void draw_scene(void) {
    for (int y = 0; y < SH; y += 8) {
        float t = y / (float)SH;
        vita2d_draw_rectangle(0, y, SW, 8, RGBA8((int)(22 - 16 * t), (int)(40 - 30 * t), (int)(92 - 64 * t), 255));
    }
    txt(30, 40, RGBA8(238, 242, 255, 255), 1.1f, "NOVADECK");
    char cnt[40]; snprintf(cnt, sizeof cnt, "%d jeux", NG[ci]);
    txt(SW - 30 - vita2d_pgf_text_width(font, 0.9f, cnt), 40, RGBA8(139, 149, 184, 255), 0.9f, cnt);

    // onglets (centres sur la console choisie, sans barre de defilement)
    float acc = 0, sel = 0;
    for (int i = 0; i < NC; i++) {
        float w = vita2d_pgf_text_width(font, 0.8f, C[i].id) + 30;
        if (i == ci) sel = acc + w / 2;
        acc += w + 8;
    }
    tabpos += ((SW / 2 - sel) - tabpos) * 0.25f;
    acc = tabpos;
    for (int i = 0; i < NC; i++) {
        float w = vita2d_pgf_text_width(font, 0.8f, C[i].id) + 30;
        if (acc + w > 0 && acc < SW) {
            int on = (i == ci);
            vita2d_draw_rectangle(acc, 62, w, 34, on ? RGBA8(76, 201, 240, 255) : RGBA8(255, 255, 255, 28));
            txt(acc + 15, 85, on ? RGBA8(4, 32, 43, 255) : RGBA8(139, 149, 184, 255), 0.8f, C[i].id);
        }
        acc += w + 8;
    }

    // jaquettes (la selection ne grandit pas : simple contour)
    fpos += (gi - fpos) * 0.25f;
    int y0 = 150;
    if (NG[ci] == 0) {
        txtc(250, RGBA8(238, 242, 255, 255), 1.0f, "Aucun jeu trouve");
        char hint[160];
        if (ci == 0) snprintf(hint, sizeof hint, "Installe des jeux Vita pour les voir ici");
        else snprintf(hint, sizeof hint, "Mets tes ROM dans " ROOT "/roms/%s", C[ci].dir);
        txtc(285, RGBA8(139, 149, 184, 255), 0.8f, hint);
    }
    for (int i = 0; i < NG[ci]; i++) {
        float x = SW / 2 - CW / 2 + (i - fpos) * (CW + GAP);
        if (x < -CW || x > SW) continue;
        Game *g = &G[ci][i];
        if (i == gi) vita2d_draw_rectangle(x - 4, y0 - 4, CW + 8, CH + 8, RGBA8(76, 201, 240, 255));
        load_cov(g);
        if (g->cov) {
            vita2d_draw_texture_scale(g->cov, x, y0, CW / (float)vita2d_texture_get_width(g->cov), CH / (float)vita2d_texture_get_height(g->cov));
        } else {
            vita2d_draw_rectangle(x, y0, CW, CH, C[ci].col);
            vita2d_draw_rectangle(x, y0 + CH / 2, CW, CH / 2, RGBA8(0, 0, 0, 80));
            char ini[4] = {0}; int k = 0;
            for (char *p = g->name; *p && k < 3; p++) if (p == g->name || p[-1] == ' ') if (isalnum((unsigned char)*p)) ini[k++] = *p;
            txt(x + 12, y0 + 70, RGBA8(255, 255, 255, 120), 1.8f, ini);
            char nm[24]; clipname(nm, g->name, 16);
            txt(x + 8, y0 + CH - 12, RGBA8(255, 255, 255, 255), 0.7f, nm);
        }
        if (i != gi) vita2d_draw_rectangle(x, y0, CW, CH, RGBA8(0, 0, 0, 120));
    }

    // infos du jeu choisi
    if (NG[ci] > 0) {
        char nm[60]; clipname(nm, G[ci][gi].name, 48);
        txt(40, 430, RGBA8(238, 242, 255, 255), 1.2f, nm);
        char m[80]; snprintf(m, sizeof m, "%s - %d/%d", C[ci].name, gi + 1, NG[ci]);
        txt(40, 458, RGBA8(139, 149, 184, 255), 0.85f, m);
    }
    txt(40, 515, RGBA8(139, 149, 184, 255), 0.75f, "L/R: console    X: lancer    Triangle: jaquettes    Start: quitter");
}

static void frame(const char *l1, const char *l2) {
    vita2d_start_drawing(); vita2d_clear_screen();
    draw_scene();
    if (l1) {
        vita2d_draw_rectangle(0, 0, SW, SH, RGBA8(0, 0, 0, 190));
        txtc(250, RGBA8(255, 255, 255, 255), 1.2f, l1);
        if (l2) txtc(300, RGBA8(139, 149, 184, 255), 0.9f, l2);
    }
    vita2d_end_drawing(); vita2d_swap_buffers();
}

static void msg(const char *a, const char *b) { for (int i = 0; i < 100; i++) frame(a, b); }

// ---- reseau / scraping ----
static void net_init(void) {
    static char netmem[1024 * 1024];
    sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    sceSysmoduleLoadModule(SCE_SYSMODULE_HTTPS);
    SceNetInitParam p; p.memory = netmem; p.size = sizeof netmem; p.flags = 0;
    if (sceNetInit(&p) < 0) return;
    sceNetCtlInit();
    if (sceHttpInit(1024 * 1024) < 0) return;
    net_ok = 1;
}

static void urlenc(char *o, int n, const char *s) {
    int k = 0;
    for (; *s && k < n - 4; s++) {
        unsigned char c = *s;
        if (strchr("&*/:`<>?\\|", c)) c = '_';
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o[k++] = c;
        else k += sprintf(o + k, "%%%02X", c);
    }
    o[k] = 0;
}

static int http_get(const char *url, const char *dest) {
    int tpl = sceHttpCreateTemplate("NovaDeck/1.0", SCE_HTTP_VERSION_1_1, 1);
    if (tpl < 0) return 0;
    sceHttpsDisableOption(tpl, SCE_HTTPS_FLAG_SERVER_VERIFY);
    sceHttpSetAutoRedirect(tpl, 1);
    int ok = 0;
    int conn = sceHttpCreateConnectionWithURL(tpl, url, 0);
    int req = conn >= 0 ? sceHttpCreateRequestWithURL(conn, SCE_HTTP_METHOD_GET, url, 0) : -1;
    if (req >= 0 && sceHttpSendRequest(req, NULL, 0) >= 0) {
        int st = 0;
        sceHttpGetStatusCode(req, &st);
        if (st == 200) {
            SceUID fd = sceIoOpen(dest, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
            if (fd >= 0) {
                static unsigned char buf[16384];
                int r, tot = 0;
                while ((r = sceHttpReadData(req, buf, sizeof buf)) > 0) { sceIoWrite(fd, buf, r); tot += r; }
                sceIoClose(fd);
                ok = tot > 100;
                if (!ok) sceIoRemove(dest);
            }
        }
    }
    if (req >= 0) sceHttpDeleteRequest(req);
    if (conn >= 0) sceHttpDeleteConnection(conn);
    sceHttpDeleteTemplate(tpl);
    return ok;
}

static void scrape(int c) {
    if (c == 0) { msg("Les jeux Vita utilisent leur propre icone", "Pas de scraping necessaire"); return; }
    int st = 0;
    if (!net_ok || sceNetCtlInetGetState(&st) < 0 || st != SCE_NETCTL_STATE_CONNECTED) {
        msg("Wi-Fi non connecte", "Connecte la Vita au Wi-Fi puis reessaie"); return;
    }
    int ok = 0, ko = 0;
    for (int i = 0; i < NG[c]; i++) {
        Game *g = &G[c][i];
        char dest[300], l1[100], l2[100], sys[200], nm[300], url[900];
        cover_path(dest, sizeof dest, c, g->name);
        if (exists(dest)) continue;
        snprintf(l1, sizeof l1, "Scraping %s : %d/%d", C[c].id, i + 1, NG[c]);
        clipname(l2, g->name, 60);
        frame(l1, l2);
        SceCtrlData pad; sceCtrlPeekBufferPositive(0, &pad, 1);
        if (pad.buttons & SCE_CTRL_CIRCLE) break;
        urlenc(sys, sizeof sys, C[c].thumb);
        // le nom doit correspondre au nom No-Intro de la base libretro
        urlenc(nm, sizeof nm, g->name);
        const char *kinds[] = {"Named_Boxarts", "Named_Titles"};
        int got = 0;
        for (int k = 0; k < 2 && !got; k++) {
            snprintf(url, sizeof url, "https://thumbnails.libretro.com/%s/%s/%s.png", sys, kinds[k], nm);
            got = http_get(url, dest);
        }
        if (got) { ok++; if (g->cov) { vita2d_wait_rendering_done(); vita2d_free_texture(g->cov); g->cov = NULL; } g->nocov = 0; }
        else ko++;
    }
    char r[100]; snprintf(r, sizeof r, "%d jaquettes trouvees, %d introuvables", ok, ko);
    msg("Scraping termine", r);
}

// ---- lancement ----
static void launch(const Game *g) {
    char uri[900];
    if (ci == 0) snprintf(uri, sizeof uri, "psgm:play?titleid=%s", g->id);
    else if (ci == 1) snprintf(uri, sizeof uri, "psgm:play?titleid=PSPEMUCFW");
    else snprintf(uri, sizeof uri, "psgm:play?titleid=RETROVITA&param=%s/%s_libretro.self&param2=%s", RETRO_CORES, C[ci].core, g->path);
    sceAppMgrLaunchAppByUri(0xFFFFF, uri);
    sceKernelDelayThread(10000);
    sceAppMgrLaunchAppByUri(0xFFFFF, uri);
    sceKernelExitProcess(0);
}

static void switch_console(int d) {
    vita2d_wait_rendering_done();
    for (int i = 0; i < NG[ci]; i++) if (G[ci][i].cov) { vita2d_free_texture(G[ci][i].cov); G[ci][i].cov = NULL; }
    ci = (ci + d + NC) % NC; gi = 0; fpos = 0;
}

int main(void) {
    vita2d_init();
    vita2d_set_clear_color(RGBA8(8, 14, 36, 255));
    font = vita2d_load_default_pgf();
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    setup_dirs();
    for (int i = 0; i < NC; i++) scan(i);
    net_init();
    int hold = 0;
    while (1) {
        SceCtrlData pad;
        sceCtrlPeekBufferPositive(0, &pad, 1);
        unsigned pr = pad.buttons & ~old_btn; old_btn = pad.buttons;
        int mv = 0;
        if (pr & SCE_CTRL_LEFT) mv = -1;
        if (pr & SCE_CTRL_RIGHT) mv = 1;
        if (pad.buttons & (SCE_CTRL_LEFT | SCE_CTRL_RIGHT)) {
            if (++hold > 18 && hold % 4 == 0) mv = (pad.buttons & SCE_CTRL_LEFT) ? -1 : 1;
        } else hold = 0;
        if (mv && NG[ci] > 0) { gi += mv; if (gi < 0) gi = 0; if (gi >= NG[ci]) gi = NG[ci] - 1; }
        if (pr & SCE_CTRL_LTRIGGER) switch_console(-1);
        if (pr & SCE_CTRL_RTRIGGER) switch_console(1);
        if ((pr & SCE_CTRL_CROSS) && NG[ci] > 0) launch(&G[ci][gi]);
        if (pr & SCE_CTRL_TRIANGLE) { scrape(ci); old_btn = 0; }
        if (pr & SCE_CTRL_START) break;
        frame(NULL, NULL);
        free_far(ci, gi - 8, gi + 8);
    }
    sceKernelExitProcess(0);
    return 0;
}
