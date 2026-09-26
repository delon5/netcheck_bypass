/*
 * netcheck_bypass -- keep PS Vita games away from PSN sign-in
 *
 * taiHEN user plugin. It only acts inside retail games (title IDs that start
 * with "PCS"); in every other process module_start installs no hooks, so
 * SceShell, system apps and homebrew are never touched.
 *
 * Inside a game the sceNetCheckDialogInit import is hooked. When the game asks
 * for the PSN sign-in dialog (mode PSN or PSN_ONLINE) the game is terminated on
 * the spot: it is closed and the login prompt is never presented. Ad-hoc and
 * PS3-connect dialogs are passed through untouched.
 *
 * The code is public domain.
 */

#include <psp2/appmgr.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/netcheck_dialog.h>
#include <taihen.h>

/* Library NID of SceCommonDialog, the library that exports the dialog API. */
#define SCE_COMMON_DIALOG_LIB_NID 0xE537816C

/* sceNetCheckDialogInit */
#define SCE_NETCHECK_DIALOG_INIT_NID 0xA38A4A0D

/*
 * Second sceNetCheckDialogInit entry point hooked by the original plugin. It is
 * not listed in the public NID database; in titles that do not import it the
 * hook simply fails to install, which is harmless.
 */
#define SCE_NETCHECK_DIALOG_INIT2_NID 0x243D6A36

/* sceAppMgrAppParamGetString() parameter id of the title ID (param.sfo TITLE_ID). */
#define APP_PARAM_TITLE_ID 12

/* Exit status handed to the kernel when a game is stopped. */
#define STOPPED_GAME_EXIT_STATUS 0

/* Hook handles; -1 means "not installed". */
static SceUID g_init_hook = -1;
static SceUID g_init2_hook = -1;

static tai_hook_ref_t g_init_ref;
static tai_hook_ref_t g_init2_ref;

/*
 * Retail games and PSN releases carry title IDs PCSA..PCSH. System apps are
 * NPXSxxxxx, SceShell is "main" and homebrew picks whatever it likes, so a
 * "PCS" prefix is what identifies a game.
 */
static int running_in_game(void) {
  char titleid[16];
  int ret;

  titleid[0] = '\0';
  ret = sceAppMgrAppParamGetString(0, APP_PARAM_TITLE_ID, titleid, sizeof(titleid));
  if (ret < 0) {
    return 0;
  }
  return titleid[0] == 'P' && titleid[1] == 'C' && titleid[2] == 'S';
}

static int wants_psn_login(const SceNetCheckDialogParam *param) {
  if (param == NULL) {
    return 0;
  }
  return param->mode == SCE_NETCHECK_DIALOG_MODE_PSN ||
         param->mode == SCE_NETCHECK_DIALOG_MODE_PSN_ONLINE;
}

/*
 * Close the game. sceKernelExitProcess() does not return; the value below is
 * only reached if the kernel refused to end the process, in which case the
 * dialog request is rejected so the sign-in prompt still never appears.
 */
static int stop_game(void) {
  sceKernelExitProcess(STOPPED_GAME_EXIT_STATUS);
  return (int)SCE_COMMON_DIALOG_ERROR_NOT_AVAILABLE;
}

static int sceNetCheckDialogInit_patched(SceNetCheckDialogParam *param) {
  if (wants_psn_login(param)) {
    return stop_game();
  }
  return TAI_CONTINUE(int, g_init_ref, param);
}

static int sceNetCheckDialogInit2_patched(SceNetCheckDialogParam *param, void *opt) {
  if (wants_psn_login(param)) {
    return stop_game();
  }
  return TAI_CONTINUE(int, g_init2_ref, param, opt);
}

void _start() __attribute__ ((weak, alias ("module_start")));
int module_start(SceSize argc, const void *args) {
  (void)argc;
  (void)args;

  if (!running_in_game()) {
    /* Not a game: stay inert. Nothing is hooked, so nothing can be affected. */
    return SCE_KERNEL_START_SUCCESS;
  }

  g_init_hook = taiHookFunctionImport(&g_init_ref,
                                      TAI_MAIN_MODULE,
                                      SCE_COMMON_DIALOG_LIB_NID,
                                      SCE_NETCHECK_DIALOG_INIT_NID,
                                      sceNetCheckDialogInit_patched);
  g_init2_hook = taiHookFunctionImport(&g_init2_ref,
                                       TAI_MAIN_MODULE,
                                       SCE_COMMON_DIALOG_LIB_NID,
                                       SCE_NETCHECK_DIALOG_INIT2_NID,
                                       sceNetCheckDialogInit2_patched);
  return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize argc, const void *args) {
  (void)argc;
  (void)args;

  if (g_init2_hook >= 0) taiHookRelease(g_init2_hook, g_init2_ref);
  if (g_init_hook >= 0) taiHookRelease(g_init_hook, g_init_ref);
  return SCE_KERNEL_STOP_SUCCESS;
}
