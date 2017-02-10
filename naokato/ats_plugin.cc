/**
 * Copyright [2017] naokato
 */
#include <cstdio>
#include <cstring>
#include <string>
#include "ts/ts.h"

namespace {
struct MyData {
  TSVIO request_vio;
  TSIOBuffer request_buffer;
  TSIOBufferReader request_reader;
  
  TSVIO response_vio;
  TSIOBuffer response_buffer;
  TSIOBufferReader response_reader;

  TSCont contp;
};
static MyData *my_data_alloc(TSCont contp) {
  MyData *data = static_cast<MyData *>(TSmalloc(sizeof(MyData)));
  data->contp = contp;

  data->request_vio = nullptr;
  data->request_buffer = nullptr;
  data->request_reader = nullptr;

  data->response_vio = nullptr;
  data->response_buffer = nullptr;
  data->response_reader = nullptr;

  return data;
}
static void my_data_destroy(MyData *data) {
  if (data) {
    if (data->request_buffer) {
      TSIOBufferDestroy(data->request_buffer);
    }
    if (data->response_buffer) {
      TSIOBufferDestroy(data->response_buffer);
    }
    TSfree(data);
  }
}
}  // namespace

static int naokato_plugin(TSCont contp, TSEvent event, void *edata);
static int intercept(TSCont contp, TSEvent event, void *edata);
static int handle_read(MyData *);
static int handle_write(MyData *);

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
      MyData *data = my_data_alloc(client_contp);
      TSContDataSet(contp, data);

      data->request_buffer = TSIOBufferCreate();
      data->request_reader = TSIOBufferReaderAlloc(data->request_buffer);
      data->request_vio =
          TSVConnRead(client_contp, contp, data->request_buffer, INT64_MAX);
      
      data->response_buffer = TSIOBufferCreate();
      data->response_reader = TSIOBufferReaderAlloc(data->response_buffer);
      data->response_vio =
          TSVConnWrite(client_contp, contp, data->response_reader, INT64_MAX);

      handle_read(data);
      handle_write(data);

      return 0;
    }
    case TS_EVENT_VCONN_EOS:
    case TS_EVENT_VCONN_READ_COMPLETE: {
      TSError("read complete from client");
      return 0;
    }
    case TS_EVENT_VCONN_READ_READY: {
      TSError("read ready from client");
      MyData *data = static_cast<MyData *>(TSContDataGet(contp));
      handle_read(data);
      return 0;
    }
    case TS_EVENT_VCONN_WRITE_COMPLETE: {
      TSError("write complete to client");
      MyData *data = static_cast<MyData *>(TSContDataGet(contp));
      TSVConnShutdown(data->contp, 0, 1);
      return 0;
    }
    case TS_EVENT_VCONN_WRITE_READY: {
      TSError("write ready to client");
      MyData *data = static_cast<MyData *>(TSContDataGet(contp));
      TSVIOReenable(data->response_vio);
      return 0;
    }
    default:
      TSError("other event:%d", event);
      break;
  }
  return 0;
}

static int handle_read(MyData *data) {
  int64_t towrite = TSVIONTodoGet(data->request_vio);

  if (towrite > 0) {
    TSError("handle_read: data from client request still exists(before read)");

    int64_t avail = TSIOBufferReaderAvail(data->request_reader);
    if (towrite > avail) {
      towrite = avail;
    }
  
    TSIOBufferReaderConsume(data->request_reader, towrite);
    TSVIONDoneSet(data->request_vio,
                  TSVIONDoneGet(data->request_vio) + towrite);
  }

  towrite = TSVIONTodoGet(data->request_vio);
  if (towrite > 0) {
    TSError("handle_read: data from client request still exists(after read)");
    TSVIOReenable(data->request_vio);
    return 0;
  }

  TSError("handle_read: read data from client request completed");
  TSVIONBytesSet(data->request_vio, TSVIONDoneGet(data->request_vio));
  TSVIOReenable(data->request_vio);
  return 0;
}

static int handle_write(MyData *data) {
  TSError("handle_write");
  const std::string response_message = "HTTP/1.1 500 Internal Server Error\r\n\r\nplugin response!\n";
  TSIOBufferWrite(data->response_buffer, response_message.data(), response_message.length());
  TSVIONBytesSet(data->response_vio, response_message.length());
  TSVIOReenable(data->response_vio);
  return 0;
}
