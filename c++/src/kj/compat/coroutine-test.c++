// Copyright (c) 2017 Sandstorm Development Group, Inc. and contributors
// Licensed under the MIT License:
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "coroutine.h"
#include "http.h"
#include <kj/debug.h>
#include <kj/test.h>
#include <map>

namespace kj {
namespace {

#ifdef KJ_HAVE_COROUTINE

KJ_TEST("Simple coroutine test") {
  EventLoop loop;
  WaitScope waitScope(loop);

  auto i = []() -> kj::Promise<int> {
    co_await kj::Promise<void>(kj::READY_NOW);
    co_return 123;
  }().wait(waitScope);

  EXPECT_EQ(i, 123);
}

KJ_TEST("Simple network test with a coroutine") {
  auto ioContext = setupAsyncIo();
  auto& network = ioContext.provider->getNetwork();

  auto port = newPromiseAndFulfiller<uint>();

  [&]() -> kj::Promise<void> {
    auto address = co_await network.parseAddress("localhost", co_await port.promise);
    auto client = co_await address->connect();
    co_await client->write("foo", 3);
  }().detach([](kj::Exception&& exception) {
    KJ_FAIL_EXPECT(exception);
  });

  kj::String result = [&]() -> kj::Promise<kj::String> {
    auto address = co_await network.parseAddress("*");
    auto listener = address->listen();
    port.fulfiller->fulfill(listener->getPort());
    auto server = co_await listener->accept();
    char receiveBuffer[4];
    auto n = co_await server->tryRead(receiveBuffer, 3, 4);
    EXPECT_EQ(3u, n);
    co_return heapString(receiveBuffer, n);
  }().wait(ioContext.waitScope);

  EXPECT_EQ("foo", result);
}

KJ_TEST("HttpClient to capnproto.org with a coroutine") {
  auto io = kj::setupAsyncIo();

  [&]() -> kj::Promise<void> {
    auto addr = co_await io.provider->getNetwork().parseAddress("capnproto.org", 80);
    auto connection = co_await addr->connect();
    // Successfully connected to capnproto.org. Try doing GET /. We expect to get a redirect to
    // HTTPS, because what kind of horrible web site would serve in plaintext, really?

    HttpHeaderTable table;
    auto client = newHttpClient(table, *connection);

    HttpHeaders headers(table);
    headers.set(HttpHeaderId::HOST, "capnproto.org");

    auto response = co_await client->request(HttpMethod::GET, "/", headers).response;
    KJ_EXPECT(response.statusCode / 100 == 3);
    auto location = KJ_ASSERT_NONNULL(response.headers->get(HttpHeaderId::LOCATION));
    KJ_EXPECT(location == "https://capnproto.org/");

    auto body = co_await response.body->readAllText();
  }().catch_([](kj::Exception&& e) {
    KJ_LOG(WARNING, "skipping test because couldn't connect to capnproto.org");
  }).wait(io.waitScope);
}

#endif  // KJ_HAVE_COROUTINE

}  // namespace
}  // namespace kj
