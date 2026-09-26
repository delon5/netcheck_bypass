/*
 * netcheck_bypass -- keep the PSN sign-in prompt out of PS Vita games
 *
 * taiHEN user plugin. It only acts inside retail games (title IDs that start
 * with "PCS"); in every other process module_start installs no hooks, so
 * SceShell, system apps and homebrew are never touched.
 *
 * Inside a game the SceNetCheckDialog imports of every module in the process
 * are hooked. When the game asks for the PSN sign-in dialog (mode PSN or
 * PSN_ONLINE) the dialog is never created: the plugin answers the game's
 * status and result queries exactly as if the user had pressed Cancel, so the
 * prompt is never presented and the game carries on the way it does after a
 * manual Cancel. Ad-hoc and PS3-connect dialogs are passed through untouched.
 *
 * The code is public domain.
 */

#include <psp2/appmgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/netcheck_dialog.h>
#include <taihen.h>

/* Library NID of SceCommonDialog, the library that exports the dialog API. */
#define SCE_COMMON_DIALOG_LIB_NID 0xE537816C

#define NID_NETCHECK_INIT      0xA38A4A0D
#define NID_NETCHECK_ABORT     0x2D8EDF09
#define NID_NETCHECK_GETRESULT 0xB05FCE9E
#define NID_NETCHECK_GETSTATUS 0x8027292A
#define NID_NETCHECK_TERM      0x8BE51C15
/*
 * Second sceNetCheckDialogInit entry point hooked by the original plugin. It is
 * not listed in the public NID database; in modules that do not import it the
 * hook simply fails to install, which is harmless.
 */
#define NID_NETCHECK_INIT2     0x243D6A36

/* sceAppMgrAppParamGetString() parameter id of the title ID (param.sfo TITLE_ID). */
#define APP_PARAM_TITLE_ID 12

/* Flags for sceKernelGetModuleList(): user (0x01) and system (0x80) modules. */
#define MODULE_LIST_ALL 0xFF
#define MAX_MODULES 64

/* Non-zero while a PSN dialog request is being answered on the game's behalf. */
static int g_faking;

/* One reference per hooked function, used to call through to the original. */
static tai_hook_ref_t g_init_ref;
static tai_hook_ref_t g_init2_ref;
static tai_hook_ref_t g_abort_ref;
static tai_hook_ref_t g_getresult_ref;
static tai_hook_ref_t g_getstatus_ref;
static tai_hook_ref_t g_term_ref;

/* Every installed hook, so module_stop can release them. */
typedef struct {
  SceUID uid;
  tai_hook_ref_t ref;
} installed_hook_t;

#define NUM_TARGETS 6
static installed_hook_t g_installed[MAX_MODULES * NUM_TARGETS];
static int g_installed_count;

/* ------------------------------------------------------------------ hooks */

static int wants_psn_login(const SceNetCheckDialogParam *param) {
  if (param == NULL) {
    return 0;
  }
  return param->mode == SCE_NETCHECK_DIALOG_MODE_PSN ||
         param->mode == SCE_NETCHECK_DIALOG_MODE_PSN_ONLINE;
}

/*
 * Decide what to do with a dialog request. Returns 1 if the request is a PSN
 * sign-in that is now being faked, 0 if it must go to the real dialog.
 */
static int intercept_init(const SceNetCheckDialogParam *param) {
  if (wants_psn_login(param)) {
    g_faking = 1;
    return 1;
  }
  g_faking = 0;
  return 0;
}

static int sceNetCheckDialogInit_patched(SceNetCheckDialogParam *param) {
  if (intercept_init(param)) {
    return 0;
  }
  return TAI_CONTINUE(int, g_init_ref, param);
}

static int sceNetCheckDialogInit2_patched(SceNetCheckDialogParam *param, void *opt) {
  if (intercept_init(param)) {
    return 0;
  }
  return TAI_CONTINUE(int, g_init2_ref, param, opt);
}

static int sceNetCheckDialogAbort_patched(void) {
  if (g_faking) {
    return 0;
  }
  return TAI_CONTINUE(int, g_abort_ref);
}

static int sceNetCheckDialogGetStatus_patched(void) {
  if (g_faking) {
    return SCE_COMMON_DIALOG_STATUS_FINISHED;
  }
  return TAI_CONTINUE(int, g_getstatus_ref);
}

static int sceNetCheckDialogGetResult_patched(SceNetCheckDialogResult *result) {
  if (g_faking) {
    if (result == NULL) {
      return (int)SCE_COMMON_DIALOG_ERROR_NULL;
    }
    sceClibMemset(result, 0, sizeof(*result));
    result->result = SCE_COMMON_DIALOG_RESULT_USER_CANCELED;
    result->psnModeSucceeded = 0;
    return 0;
  }
  return TAI_CONTINUE(int, g_getresult_ref, result);
}

static int sceNetCheckDialogTerm_patched(void) {
  if (g_faking) {
    g_faking = 0;
    return 0;
  }
  return TAI_CONTINUE(int, g_term_ref);
}

/* ------------------------------------------------------------ installing */

typedef struct {
  uint32_t nid;
  const void *func;
  tai_hook_ref_t *ref;
} hook_target_t;

static const hook_target_t g_targets[NUM_TARGETS] = {
  { NID_NETCHECK_INIT,      sceNetCheckDialogInit_patched,      &g_init_ref },
  { NID_NETCHECK_INIT2,     sceNetCheckDialogInit2_patched,     &g_init2_ref },
  { NID_NETCHECK_ABORT,     sceNetCheckDialogAbort_patched,     &g_abort_ref },
  { NID_NETCHECK_GETRESULT, sceNetCheckDialogGetResult_patched, &g_getresult_ref },
  { NID_NETCHECK_GETSTATUS, sceNetCheckDialogGetStatus_patched, &g_getstatus_ref },
  { NID_NETCHECK_TERM,      sceNetCheckDialogTerm_patched,      &g_term_ref },
};

/*
 * Hook every SceNetCheckDialog import of one module. modname NULL means the
 * process's main module. Modules that do not import a function are skipped.
 * The first hook installed for a function becomes the reference used to call
 * the original; every hook of the same function ends at the same export, so
 * any of them serves.
 */
static void hook_module(const char *modname) {
  int i;

  for (i = 0; i < NUM_TARGETS; i++) {
    const hook_target_t *t = &g_targets[i];
    tai_hook_ref_t ref = 0;
    SceUID uid;

    if (g_installed_count >= MAX_MODULES * NUM_TARGETS) {
      return;
    }
    uid = taiHookFunctionImport(&ref, modname, SCE_COMMON_DIALOG_LIB_NID, t->nid, t->func);
    if (uid < 0) {
      continue;
    }
    g_installed[g_installed_count].uid = uid;
    g_installed[g_installed_count].ref = ref;
    g_installed_count++;
    if (*t->ref == 0) {
      *t->ref = ref;
    }
  }
}

static void hook_all_modules(void) {
  SceUID uids[MAX_MODULES];
  SceSize num = MAX_MODULES;
  SceKernelModuleInfo info;
  SceSize i;

  if (sceKernelGetModuleList(MODULE_LIST_ALL, uids, &num) < 0) {
    hook_module(NULL);
    return;
  }

  for (i = 0; i < num; i++) {
    info.size = sizeof(info);
    if (sceKernelGetModuleInfo(uids[i], &info) < 0) {
      continue;
    }
    hook_module(info.module_name);
  }

  /* Belt and braces: make sure the main module is covered whatever its name. */
  if (g_init_ref == 0) {
    hook_module(NULL);
  }
}

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

void _start() __attribute__ ((weak, alias ("module_start")));
int module_start(SceSize argc, const void *args) {
  (void)argc;
  (void)args;

  if (!running_in_game()) {
    /* Not a game: stay inert. Nothing is hooked, so nothing can be affected. */
    return SCE_KERNEL_START_SUCCESS;
  }

  hook_all_modules();
  return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize argc, const void *args) {
  int i;
  (void)argc;
  (void)args;

  for (i = g_installed_count - 1; i >= 0; i--) {
    taiHookRelease(g_installed[i].uid, g_installed[i].ref);
  }
  g_installed_count = 0;
  return SCE_KERNEL_STOP_SUCCESS;
}
