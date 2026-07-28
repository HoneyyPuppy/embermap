#ifndef CONFIG_SERVICE_H
#define CONFIG_SERVICE_H
// DEPRECATED: ConfigService has been split into smaller modules.
// Use these headers instead:
//   ConfigStorage.h  - init(), loadConfig(), saveConfig()
//   ResetButton.h    - check(), startTask()
//   CaptivePortal.h  - start(), handle(), isActive()
//   DashboardServer.h - (master_node only) start(), handle(), OTA state
#error "ConfigService.h is deprecated. Include the specific module headers listed above."
#endif
