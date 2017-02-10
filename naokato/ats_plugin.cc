/**
 * Copyright [2017] naokato
 */
#include <cstdio>
#include <cstring>
#include "ts/ts.h"

static int naokato_plugin(TSCont contp, TSEvent event, void *edata) {
  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);
  TSError("naokato_plugin");

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

void TSPluginInit(int argc, const char *argv[]) {
  TSPluginRegistrationInfo info;

  info.plugin_name = "naokato_plugin";
  info.vendor_name = "MyCompany";
  info.support_email = "naokato@example.com";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[Nokato Ats Plugin] Plugin registration failed.");
  }

  // TODO(naokato): check ats sdk version

  TSCont contp = TSContCreate(naokato_plugin, NULL);
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, contp);
}
