/**
 * Copyright [2017] naokato
 */
#include <cstdio>
#include <cstring>
#include "ts/ts.h"

namespace {
struct MyData {
  TSVIO vio;
  TSIOBuffer buffer;
  TSIOBufferReader reader;
  TSCont contp;
};
static MyData *my_data_alloc(TSCont contp) {
  MyData *data = static_cast<MyData *>(TSmalloc(sizeof(MyData)));
  data->contp = contp;
  data->vio = nullptr;
  data->buffer = nullptr;
  data->reader = nullptr;
  return data;
}
static void my_data_destroy(MyData *data) {
  if (data) {
    if (data->buffer) {
      TSIOBufferDestroy(data->buffer);
    }
    TSfree(data);
  }
}
}  // namespace

static int naokato_plugin(TSCont contp, TSEvent event, void *edata);
static int intercept(TSCont contp, TSEvent event, void *edata);
static int handle_event(MyData *);

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

static int naokato_plugin(TSCont contp, TSEvent event, void *edata) {
  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);
  TSError("naokato_plugin");

  switch (event) {
    case TS_EVENT_HTTP_READ_REQUEST_HDR: {
      TSCont intercept_contp = TSContCreate(intercept, TSMutexCreate());
      TSHttpTxnIntercept(intercept_contp, txnp);
      break;
    }
    default:
      break;
  }

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

static int intercept(TSCont contp, TSEvent event, void *edata) {
  TSError("intercept");

  if (TSVConnClosedGet(contp)) {
    TSError("intercept closed");
    // TODO(naokato): edata must be MyData*, but need to check
    my_data_destroy(static_cast<MyData *>(TSContDataGet(contp)));
    TSContDestroy(contp);
    return 0;
  }

  switch (event) {
    // TODO(naokato): TS_EVENT_NET_ACCEPT_FAILED pattern

    case TS_EVENT_NET_ACCEPT: {
      TSError("event net accept");
      TSCont client_contp = static_cast<TSCont>(edata);
      MyData *request_data = my_data_alloc(client_contp);
      TSContDataSet(contp, request_data);

      request_data->buffer = TSIOBufferCreate();
      request_data->reader = TSIOBufferReaderAlloc(request_data->buffer);
      request_data->vio =
          TSVConnRead(client_contp, contp, request_data->buffer, INT64_MAX);

      handle_event(request_data);

      return 0;
    }
    case TS_EVENT_VCONN_EOS:
    case TS_EVENT_VCONN_READ_COMPLETE: {
      TSError("read complete from client");
      MyData *data = static_cast<MyData *>(TSContDataGet(contp));
      TSVConnShutdown(data->contp, 0, 1);
      return 0;
    }
    case TS_EVENT_VCONN_READ_READY: {
      TSError("read ready from client");
      MyData *data = static_cast<MyData *>(TSContDataGet(contp));
      handle_event(data);
      return 0;
    }
    default:
      TSError("other event:%d", event);
      break;
  }
  return 0;
}

static int handle_event(MyData *request_data) {
  int64_t towrite = TSVIONTodoGet(request_data->vio);

  if (towrite > 0) {
    TSError("handle_event: data from client request still exists(before read)");

    int64_t avail = TSIOBufferReaderAvail(request_data->reader);
    if (towrite > avail) {
      towrite = avail;
    }

    TSIOBufferReaderConsume(request_data->reader, towrite);
    TSVIONDoneSet(request_data->vio,
                  TSVIONDoneGet(request_data->vio) + towrite);
  }

  towrite = TSVIONTodoGet(request_data->vio);
  if (towrite > 0) {
    TSError("handle_event: data from client request still exists(after read)");
    TSVIOReenable(request_data->vio);
    return 0;
  }

  TSError("handle_event: read data from client request completed");
  TSVIONBytesSet(request_data->vio, TSVIONDoneGet(request_data->vio));
  TSVIOReenable(request_data->vio);
  return 0;
}
