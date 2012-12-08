#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "hp2.h"
#include "test_data.h"

void expect_eq(const char* expected, hp2_token token) {
  int len = token.end - token.start;
  int expected_len = strlen(expected);

  if (len != expected_len) {
    printf("bad strlen. expected = %d, got = %d\n", expected_len, len);
    abort();
  }

  if (strncmp(token.start, expected, len) != 0) {
    printf("bad str. expected = \"%s\"\n", expected);
    abort();
  }
}


void test_pipeline(const struct message* req1,
                   const struct message* req2,
                   const struct message* req3) {
  hp2_parser parser;
  hp2_token token;

  /* TODO */
}


void test_req(const struct message* req) {
  hp2_parser parser;
  hp2_token token;
  const char* buf = req->raw;
  int raw_len = strlen(req->raw);
  int len = raw_len;
  int num_headers = 0;
  int body_len = strlen(req->body);
  char* body = malloc(body_len);
  int body_read = 0;
  int chunk_len;

  hp2_req_init(&parser);

  token = hp2_parse(&parser, buf, len);
  assert(token.kind == HP2_MSG_START);
  assert(token.partial == 0);

  len -= token.end - buf;
  buf = token.end;
  token = hp2_parse(&parser, buf, len);
  assert(token.kind == HP2_METHOD);
  assert(token.partial == 0);
  expect_eq(req->method, token);

  len -= token.end - buf;
  buf = token.end;
  token = hp2_parse(&parser, buf, len);
  assert(token.kind == HP2_URL);
  assert(token.partial == 0);
  expect_eq(req->request_url, token);

  for (;;) {
    /* field */
    len -= token.end - buf;
    buf = token.end;
    token = hp2_parse(&parser, buf, len);

    if (token.kind != HP2_FIELD) break;

    assert(token.kind == HP2_FIELD);
    assert(token.partial == 0);
    expect_eq(req->headers[num_headers][0], token);

    /* value */
    len -= token.end - buf;
    buf = token.end;
    token = hp2_parse(&parser, buf, len);
    assert(token.kind == HP2_VALUE);
    assert(token.partial == 0);
    expect_eq(req->headers[num_headers][1], token);

    num_headers++;
  }

  assert(token.kind == HP2_HEADER_END);
  assert(token.partial == 0);

  /* Check HTTP version matches */
  assert(parser.version_major == req->http_major);
  assert(parser.version_minor == req->http_minor);

  /* Now read the body */
  for (;;) {
    len -= token.end - buf;
    buf = token.end;
    token = hp2_parse(&parser, buf, len);

    if (token.kind != HP2_BODY) break;

    assert(token.kind == HP2_BODY);
    assert(token.partial == 0);

    chunk_len = token.end - token.start;
    memcpy(body + body_read, token.start, chunk_len);
    body_read += chunk_len;
  }

  /* match trailing headers */
  if (num_headers < req->num_headers) {
    /* the last token read should have been a HP2_FIELD */
    assert(token.kind == HP2_FIELD);
    assert(token.partial == 0);
    expect_eq(req->headers[num_headers][0], token);

    /* value */
    len -= token.end - buf;
    buf = token.end;
    token = hp2_parse(&parser, buf, len);
    assert(token.kind == HP2_VALUE);
    assert(token.partial == 0);
    expect_eq(req->headers[num_headers][1], token);

    num_headers++;

    for (;;) {
      /* field */
      len -= token.end - buf;
      buf = token.end;
      token = hp2_parse(&parser, buf, len);

      if (token.kind != HP2_FIELD) break;

      assert(token.kind == HP2_FIELD);
      assert(token.partial == 0);
      expect_eq(req->headers[num_headers][0], token);

      /* value */
      len -= token.end - buf;
      buf = token.end;
      token = hp2_parse(&parser, buf, len);
      assert(token.kind == HP2_VALUE);
      assert(token.partial == 0);
      expect_eq(req->headers[num_headers][1], token);

      num_headers++;
    }
  }

  assert(num_headers == req->num_headers);

  assert(body_len == body_read);
  assert(strcmp(body, req->body) == 0);
  printf("bodys match. body_len = %d\n", body_len);

  assert(token.kind == HP2_MSG_END);

  /* After the message should either get HP2_EOF or HP2_EAGAIN depending on
   * req->should_keep_alive. This corresponds roughly to the setting of the
   * Connection header. (HTTP pipelining involves the protocol version too.)
   */

  len -= token.end - buf;
  buf = token.end;
  token = hp2_parse(&parser, buf, len);
  if (req->upgrade) {
    assert(token.kind == HP2_EOF);
    token.end = buf + len;
    expect_eq(req->upgrade, token);
  } else if (req->should_keep_alive) {
    assert(token.kind == HP2_EAGAIN);
  } else {
    assert(token.kind == HP2_EOF);
  }
}


void manual_test_CURL_GET() {
  hp2_parser parser;
  hp2_token token;
  const struct message* r = &requests[CURL_GET];
  int raw_len =  strlen(r->raw);
  int len = raw_len;
  const char* buf = r->raw;

  hp2_req_init(&parser);

  /* HP2_MSG_START */
  token = hp2_parse(&parser, buf, len);
  assert(token.kind == HP2_MSG_START);
  assert(token.partial == 0);
  assert(token.start == token.end);
  assert(token.end == r->raw);

  /* Method = "GET" */
  len -= token.end - buf;
  buf = token.end;
  assert(len == raw_len);
  assert(buf == r->raw);
  token = hp2_parse(&parser, buf, len);
  assert(token.kind == HP2_METHOD);
  assert(token.partial == 0);
  expect_eq(r->method, token);

  /* HP2_URL = "/test" */
  len -= token.end - buf;
  buf = token.end;
  token = hp2_parse(&parser, buf, len);
  assert(token.kind == HP2_URL);
  assert(token.partial == 0);
  expect_eq(r->request_url, token);

  /* HP2_FIELD = "User-Agent" */
  len -= token.end - buf;
  buf = token.end;
  token = hp2_parse(&parser, buf, len);
  assert(token.kind == HP2_FIELD);
  assert(token.partial == 0);
  expect_eq(r->headers[0][0], token);

  assert(parser.version_major == r->http_major);
  assert(parser.version_minor == r->http_minor);

  /* HP2_VALUE = "curl/7.18.0 (i486-pc-linux-gnu) libcurl/7.18.0 OpenSSL/0.9.8g zlib/1.2.3.3 libidn/1.1" */
  len -= token.end - buf;
  buf = token.end;
  token = hp2_parse(&parser, buf, len);
  assert(token.kind == HP2_VALUE);
  assert(token.partial == 0);
  expect_eq(r->headers[0][1], token);

  /* HP2_FIELD = "Host" */
  len -= token.end - buf;
  buf = token.end;
  token = hp2_parse(&parser, buf, len);
  assert(token.kind == HP2_FIELD);
  assert(token.partial == 0);
  expect_eq(r->headers[1][0], token);

  /* HP2_VALUE = "0.0.0.0=5000" */
  len -= token.end - buf;
  buf = token.end;
  token = hp2_parse(&parser, buf, len);
  assert(token.kind == HP2_VALUE);
  assert(token.partial == 0);
  expect_eq(r->headers[1][1], token);

  /* HP2_FIELD = "Accept" */
  len -= token.end - buf;
  buf = token.end;
  token = hp2_parse(&parser, buf, len);
  assert(token.kind == HP2_FIELD);
  assert(token.partial == 0);
  expect_eq(r->headers[2][0], token);

  // HP2_VALUE = "*/*"
  len -= token.end - buf;
  buf = token.end;
  token = hp2_parse(&parser, buf, len);
  assert(token.kind == HP2_VALUE);
  assert(token.partial == 0);
  expect_eq(r->headers[2][1], token);

  // HP2_HEADER_END
  len -= token.end - buf;
  buf = token.end;
  token = hp2_parse(&parser, buf, len);
  assert(token.kind == HP2_HEADER_END);
  assert(token.start == token.end);
  // CURL_GET has not body, so the headers should be at the end of the buffer.
  assert(token.end == r->raw + raw_len);

  // HP2_MSG_END
  len -= token.end - buf;
  buf = token.end;
  token = hp2_parse(&parser, buf, len);
  assert(token.kind == HP2_MSG_END);
  assert(token.partial == 0);
  assert(token.start == token.end);
  // We're at the end of the buffer
  assert(token.end == r->raw + raw_len);

  // If we call hp2_parse() again, we get HP2_EAGAIN.
  token = hp2_parse(&parser, token.end, 0);
  assert(token.kind == HP2_EAGAIN);
}


int main() {
  int i;

  printf("sizeof(hp2_token) = %d\n", (int)sizeof(hp2_token));
  printf("sizeof(hp2_parser) = %d\n", (int)sizeof(hp2_parser));

  manual_test_CURL_GET();

  for (i = 0; requests[i].name; i++) {
    printf("test_req(%d, %s)\n", i, requests[i].name);
    test_req(&requests[i]);
  }

  return 0;
}
