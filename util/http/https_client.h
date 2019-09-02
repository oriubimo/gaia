// Copyright 2019, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//

#pragma once

#include <boost/asio/ssl.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http/buffer_body.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/parser.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/write.hpp>

#include "absl/strings/string_view.h"
#include "util/asio/fiber_socket.h"

namespace util {
class IoContext;
class FiberSyncSocket;

namespace http {

using SslStream = ::boost::asio::ssl::stream<FiberSyncSocket>;

class HttpsClient {
 public:
  using error_code = ::boost::system::error_code;

  HttpsClient(absl::string_view host, IoContext* context, ::boost::asio::ssl::context* ssl_ctx);
  HttpsClient(const HttpsClient&) = delete;
  HttpsClient(HttpsClient&&) = delete;

  error_code Connect(unsigned msec);

  template <typename Req> error_code Send(const Req& req);

  template <typename Req, typename Resp> error_code Send(const Req& req, Resp* resp);

  template <typename Parser> error_code ReadHeader(Parser* parser);
  template <typename Parser> error_code Read(Parser* parser);

  error_code DrainResponse(
      ::boost::beast::http::response_parser<::boost::beast::http::buffer_body>* parser);

  SslStream* client() { return client_.get(); }

  void schedule_reconnect() { reconnect_needed_ = true; }
  auto native_handle() { return client_->native_handle(); }

 private:
  error_code HandleError(const error_code& ec);

  error_code ReconnectIfNeeded() {
    if (reconnect_needed_)
      return InitSslClient();
    return error_code{};
  }

  error_code InitSslClient();

  IoContext& io_context_;
  ::boost::asio::ssl::context& ssl_cntx_;

  ::boost::beast::flat_buffer tmp_buffer_;

  std::string host_name_;
  std::unique_ptr<SslStream> client_;

  uint32_t reconnect_msec_ = 1000;
  bool reconnect_needed_ = true;
};

::boost::system::error_code SslConnect(SslStream* stream, unsigned msec);

template <typename Req, typename Resp>
auto HttpsClient::Send(const Req& req, Resp* resp) -> error_code {
  namespace h2 = ::boost::beast::http;
  error_code ec = Send(req);
  if (ec)
    return ec;

  h2::read(*client_, tmp_buffer_, *resp, ec);
  return HandleError(ec);
}

template <typename Req> auto HttpsClient::Send(const Req& req) -> error_code {
  error_code ec = ReconnectIfNeeded();
  if (ec)
    return ec;
  ::boost::beast::http::write(*client_, req, ec);
  return HandleError(ec);
}

// Read methods should not reconnect since they assume some state (i.e. reading http request).
template <typename Parser> auto HttpsClient::ReadHeader(Parser* parser) -> error_code {
  error_code ec;

  ::boost::beast::http::read_header(*client_, tmp_buffer_, *parser, ec);
  return HandleError(ec);
}

template <typename Parser> auto HttpsClient::Read(Parser* parser) -> error_code {
  error_code ec;

  // Note that read returns number of raw bytes read from stream before parsing which
  // does not correlate to the final data stored inside the parser.
  ::boost::beast::http::read(*client_, tmp_buffer_, *parser, ec);
  return HandleError(ec);
}

inline auto HttpsClient::HandleError(const error_code& ec) -> error_code {
  using err = ::boost::beast::http::error;
  if (ec && ec != err::need_buffer && ec != err::partial_message)
    reconnect_needed_ = true;
  return ec;
}

}  // namespace http
}  // namespace util
