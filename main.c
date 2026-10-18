#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <orbis/Pad.h>
#include <orbis/libkernel.h>
#include <Common.h>

#ifndef attr_public
#define attr_public __attribute__((visibility("default")))
#endif

// Metadati richiesti dal GoldHEN Plugin Loader
attr_public const char *g_pluginName = "xbox_pad_translator";
attr_public const char *g_pluginDesc = "Traduce input Xbox Wired in chiamate OrbisPad";
attr_public const char *g_pluginAuth = "CustomDev";
attr_public uint32_t g_pluginVersion = 0x00000100; // 1.00

#define PORT 9999

#pragma pack(push, 1)
typedef struct {
    uint16_t xinput_buttons;
    uint8_t  lt;
    uint8_t  rt;
    int16_t  lx;
    int16_t  ly;
    int16_t  rx;
    int16_t  ry;
} XboxInputPacket;
#pragma pack(pop)

#define XBOX_DPAD_UP        0x0001
#define XBOX_DPAD_DOWN      0x0002
#define XBOX_DPAD_LEFT      0x0004
#define XBOX_DPAD_RIGHT     0x0008
#define XBOX_START          0x0010
#define XBOX_BACK           0x0020
#define XBOX_LTHUMB         0x0040
#define XBOX_RTHUMB         0x0080
#define XBOX_LB             0x0100
#define XBOX_RB             0x0200
#define XBOX_GUIDE          0x0400
#define XBOX_A              0x1000
#define XBOX_B              0x2000
#define XBOX_X              0x4000
#define XBOX_Y              0x8000

static volatile XboxInputPacket g_currentXboxState = {0, 0, 0, 0, 0, 0, 0};
static volatile int g_hasActiveInput = 0;
static volatile int g_running = 1;
static pthread_t g_netThread;

static inline uint8_t scale_axis(int16_t val, int invert) {
    int32_t v = val;
    if (invert) v = -v - 1;
    int32_t scaled = ((v + 32768) * 255) / 65535;
    if (scaled < 0) scaled = 0;
    if (scaled > 255) scaled = 255;
    return (uint8_t)scaled;
}

static uint32_t translate_buttons(uint16_t xbtns, uint8_t lt, uint8_t rt) {
    uint32_t ps4_btns = 0;

    if (xbtns & XBOX_DPAD_UP)    ps4_btns |= ORBIS_PAD_BUTTON_UP;
    if (xbtns & XBOX_DPAD_DOWN)  ps4_btns |= ORBIS_PAD_BUTTON_DOWN;
    if (xbtns & XBOX_DPAD_LEFT)  ps4_btns |= ORBIS_PAD_BUTTON_LEFT;
    if (xbtns & XBOX_DPAD_RIGHT) ps4_btns |= ORBIS_PAD_BUTTON_RIGHT;

    if (xbtns & XBOX_START)      ps4_btns |= ORBIS_PAD_BUTTON_OPTIONS;
    if (xbtns & XBOX_BACK)       ps4_btns |= ORBIS_PAD_BUTTON_TOUCH_PAD;
    if (xbtns & XBOX_LTHUMB)     ps4_btns |= ORBIS_PAD_BUTTON_L3;
    if (xbtns & XBOX_RTHUMB)     ps4_btns |= ORBIS_PAD_BUTTON_R3;
    if (xbtns & XBOX_LB)         ps4_btns |= ORBIS_PAD_BUTTON_L1;
    if (xbtns & XBOX_RB)         ps4_btns |= ORBIS_PAD_BUTTON_R1;

    if (xbtns & XBOX_A)          ps4_btns |= ORBIS_PAD_BUTTON_CROSS;
    if (xbtns & XBOX_B)          ps4_btns |= ORBIS_PAD_BUTTON_CIRCLE;
    if (xbtns & XBOX_X)          ps4_btns |= ORBIS_PAD_BUTTON_SQUARE;
    if (xbtns & XBOX_Y)          ps4_btns |= ORBIS_PAD_BUTTON_TRIANGLE;

    if (lt > 30)                 ps4_btns |= ORBIS_PAD_BUTTON_L2;
    if (rt > 30)                 ps4_btns |= ORBIS_PAD_BUTTON_R2;

    return ps4_btns;
}

static void apply_xbox_to_orbis(OrbisPadData *pData) {
    if (!pData || !g_hasActiveInput) return;

    XboxInputPacket snap = g_currentXboxState;

    pData->buttons |= translate_buttons(snap.xinput_buttons, snap.lt, snap.rt);

    pData->leftStick.x  = scale_axis(snap.lx, 0);
    pData->leftStick.y  = scale_axis(snap.ly, 1);
    pData->rightStick.x = scale_axis(snap.rx, 0);
    pData->rightStick.y = scale_axis(snap.ry, 1);

    pData->analogButtons.l2 = snap.lt;
    pData->analogButtons.r2 = snap.rt;
    pData->connected = 1;
}

static void* xbox_receiver_thread(void* arg) {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) return NULL;

    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(PORT);

    if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        close(sockfd);
        return NULL;
    }

    XboxInputPacket pkt;
    while (g_running) {
        ssize_t n = recvfrom(sockfd, &pkt, sizeof(XboxInputPacket), 0, NULL, NULL);
        if (n == sizeof(XboxInputPacket)) {
            g_currentXboxState = pkt;
            g_hasActiveInput = 1;
        }
    }

    close(sockfd);
    return NULL;
}

typedef int (*scePadReadState_t)(int32_t handle, OrbisPadData *pData);
typedef int (*scePadRead_t)(int32_t handle, OrbisPadData *pData, int32_t num);

HOOK_INIT(scePadReadState);
HOOK_INIT(scePadRead);

int scePadReadState_hook(int32_t handle, OrbisPadData *pData) {
    int ret = HOOK_CONTINUE(scePadReadState, scePadReadState_t, handle, pData);
    if (ret == 0 && pData != NULL) {
        apply_xbox_to_orbis(pData);
    }
    return ret;
}

int scePadRead_hook(int32_t handle, OrbisPadData *pData, int32_t num) {
    int ret = HOOK_CONTINUE(scePadRead, scePadRead_t, handle, pData, num);
    if (ret >= 0 && pData != NULL) {
        if (ret == 0 && g_hasActiveInput) {
            ret = 1;
        }
        for (int i = 0; i < ret; i++) {
            apply_xbox_to_orbis(&pData[i]);
        }
    }
    return ret;
}

int32_t attr_public module_start(size_t argc, const void* args) {
    g_running = 1;
    pthread_create(&g_netThread, NULL, xbox_receiver_thread, NULL);

    HOOK32(scePadReadState);
    HOOK32(scePadRead);

    return 0;
}

int32_t attr_public module_stop(size_t argc, const void* args) {
    g_running = 0;
    UNHOOK(scePadReadState);
    UNHOOK(scePadRead);
    return 0;
}
