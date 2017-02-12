/**
 * Copyright [2017] naokato
 */
#include <cstdio>
#include <cstring>
#include <string>
#include "ts/ts.h"

namespace {
struct ClientData {
  struct InterceptIOChannel {
    InterceptIOChannel() : vio(nullptr), iobuf(nullptr), reader(nullptr) {}
    ~InterceptIOChannel()
    {
      TSError("InterceptIOChannel destructor");
      if (this->reader) {
        TSIOBufferReaderFree(this->reader);
      }
  
      if (this->iobuf) {
        TSIOBufferDestroy(this->iobuf);
      }
    }
    
    TSVIO vio;
    TSIOBuffer iobuf;
    TSIOBufferReader reader;
  };

  ClientData(const TSCont& contp): contp(contp) {}
  ~ClientData() {
    TSError("ClientData destructor");
    if (contp) {
      TSVConnClose(contp);
    }
  }

  void Read(TSCont intercept_contp) {
    this->req_channel.iobuf = TSIOBufferCreate();
    this->req_channel.reader = TSIOBufferReaderAlloc(this->req_channel.iobuf);
    this->req_channel.vio =
        TSVConnRead(this->contp, intercept_contp, this->req_channel.iobuf, INT64_MAX);
  }
  void Write(TSCont intercept_contp) {
    this->res_channel.iobuf = TSIOBufferCreate();
    this->res_channel.reader = TSIOBufferReaderAlloc(this->res_channel.iobuf);
    this->res_channel.vio =
          TSVConnWrite(this->contp, intercept_contp, this->res_channel.reader, INT64_MAX);
  }

  TSCont contp;
  InterceptIOChannel req_channel;
  InterceptIOChannel res_channel;
};
}  // namespace

static int naokato_plugin(TSCont contp, TSEvent event, void *edata);
static int intercept(TSCont contp, TSEvent event, void *edata);
static int handle_read(ClientData *);
static int handle_write(ClientData *);

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
    TSContDestroy(contp);
    return 0;
  }

  switch (event) {
    // TODO(naokato): TS_EVENT_NET_ACCEPT_FAILED pattern

    case TS_EVENT_NET_ACCEPT: {
      TSError("event net accept");
      TSCont client_contp = static_cast<TSCont>(edata);
      // TODO(naokato): use smart pointer 
      auto data = new ClientData(client_contp);
      TSContDataSet(contp, data);

      data->Read(contp);
      data->Write(contp);

      handle_read(data);
      handle_write(data);

      return 0;
    }
    case TS_EVENT_VCONN_READ_COMPLETE: {
      TSError("read complete from client");
      return 0;
    }
    case TS_EVENT_VCONN_READ_READY: {
      TSError("read ready from client");
      ClientData *data = static_cast<ClientData *>(TSContDataGet(contp));
      handle_read(data);
      return 0;
    }
    case TS_EVENT_VCONN_WRITE_COMPLETE: {
      TSError("write complete to client");
      ClientData *data = static_cast<ClientData *>(TSContDataGet(contp));
      TSVConnShutdown(data->contp, 0, 1);
      return 0;
    }
    case TS_EVENT_VCONN_WRITE_READY: {
      TSError("write ready to client");
      ClientData *data = static_cast<ClientData *>(TSContDataGet(contp));
      TSVIOReenable(data->res_channel.vio);
      return 0;
    }
    case TS_EVENT_VCONN_EOS:
    case TS_EVENT_ERROR: {
      TSError("EOS or ERROR:%d", event);
      TSContDestroy(contp);
      ClientData *data = static_cast<ClientData *>(TSContDataGet(contp));
      delete data;
      TSError("intercept end");
      return 0;
    }
    default:
      TSError("other event:%d", event);
      break;
  }
  return 0;
}

static int handle_read(ClientData *data) {

  int64_t towrite = TSVIONTodoGet(data->req_channel.vio);

  if (towrite > 0) {
    TSError("handle_read: data from client request still exists(before read)");

    int64_t avail = TSIOBufferReaderAvail(data->req_channel.reader);
    if (towrite > avail) {
      towrite = avail;
    }
    
    int consumed = 0;
    std::string body = "";
    const char *body_chunk;
    int64_t body_chunk_len;

    TSIOBufferBlock block = TSIOBufferReaderStart(data->req_channel.reader);
    while (block != nullptr) {
      body_chunk = TSIOBufferBlockReadStart(block, data->req_channel.reader, &body_chunk_len);
      consumed += body_chunk_len;
      body.append(body_chunk);

      block = TSIOBufferBlockNext(block);
    }
    TSError("%s", body.c_str());

    TSIOBufferReaderConsume(data->req_channel.reader, consumed);
    TSVIONDoneSet(data->req_channel.vio,
                  TSVIONDoneGet(data->req_channel.vio) + consumed);
  }

  towrite = TSVIONTodoGet(data->req_channel.vio);
  if (towrite > 0) {
    TSError("handle_read: data from client request still exists(after read)");
    TSVIOReenable(data->req_channel.vio);
    return 0;
  }

  TSError("handle_read: read data from client request completed");
  TSVIONBytesSet(data->req_channel.vio, TSVIONDoneGet(data->req_channel.vio));
  TSVIOReenable(data->req_channel.vio);
  return 0;
}

static int handle_write(ClientData *data) {
  TSError("handle_write");
  const std::string response_message =
      "HTTP/1.1 500 Internal Server Error\r\n\r\nplugin response!\r\n";
  TSIOBufferWrite(data->res_channel.iobuf, response_message.data(),
                  response_message.length());
  TSVIONBytesSet(data->res_channel.vio, response_message.length());
  TSVIOReenable(data->res_channel.vio);
  return 0;
}
