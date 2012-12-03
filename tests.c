#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "hp2.h"
#include "test_data.h"

void expect_eq(const char* expected, hp2_datum datum) {
  int len = datum.end - datum.start;
  int expected_len = strlen(expected);

  if (len != expected_len) {
    printf("bad strlen. expected = %d, got = %d\n", expected_len, len);
    abort();
  }

  if (strncmp(datum.start, expected, len) != 0) {
    printf("bad str. expected = \"%s\"\n", expected);
    abort();
  }
}


void test_req(const struct message* req) {
  hp2_parser parser;
  hp2_datum d;
  const char* buf = req->raw;
  int raw_len = strlen(req->raw);
  int len = raw_len;
  int num_headers = 0;
  int body_len = strlen(req->body);
  char* body = malloc(body_len);
  int body_read = 0;
  int chunk_len;

  hp2_req_init(&parser);

  d = hp2_parse(&parser, buf, len);
  assert(d.type == HP2_MSG_START);
  assert(d.partial == 0);

  len -= d.end - buf;
  buf = d.end;
  d = hp2_parse(&parser, buf, len);
  assert(d.type == HP2_METHOD);
  assert(d.partial == 0);
  expect_eq(req->method, d);

  len -= d.end - buf;
  buf = d.end;
  d = hp2_parse(&parser, buf, len);
  assert(d.type == HP2_URL);
  assert(d.partial == 0);
  expect_eq(req->request_url, d);

  for (;;) {
    /* field */
    len -= d.end - buf;
    buf = d.end;
    d = hp2_parse(&parser, buf, len);

    if (d.type != HP2_FIELD) break;

    assert(d.type == HP2_FIELD);
    assert(d.partial == 0);
    expect_eq(req->headers[num_headers][0], d);

    /* value */
    len -= d.end - buf;
    buf = d.end;
    d = hp2_parse(&parser, buf, len);
    assert(d.type == HP2_VALUE);
    assert(d.partial == 0);
    expect_eq(req->headers[num_headers][1], d);

    num_headers++;
  }

  assert(d.type == HP2_HEADER_END);
  assert(d.partial == 0);

  /* Now read the body */
  for (;;) {
    len -= d.end - buf;
    buf = d.end;
    d = hp2_parse(&parser, buf, len);

    if (d.type != HP2_BODY) break;

    assert(d.type == HP2_BODY);
    assert(d.partial == 0);

    chunk_len = d.end - d.start;
    memcpy(body + body_read, d.start, chunk_len);
    body_read += chunk_len;
  }

  /* match trailing headers */
  if (num_headers < req->num_headers) {
    /* the last datum read should have been a HP2_FIELD */
    assert(d.type == HP2_FIELD);
    assert(d.partial == 0);
    expect_eq(req->headers[num_headers][0], d);

    /* value */
    len -= d.end - buf;
    buf = d.end;
    d = hp2_parse(&parser, buf, len);
    assert(d.type == HP2_VALUE);
    assert(d.partial == 0);
    expect_eq(req->headers[num_headers][1], d);

    num_headers++;

    for (;;) {
      /* field */
      len -= d.end - buf;
      buf = d.end;
      d = hp2_parse(&parser, buf, len);

      if (d.type != HP2_FIELD) break;

      assert(d.type == HP2_FIELD);
      assert(d.partial == 0);
      expect_eq(req->headers[num_headers][0], d);

      /* value */
      len -= d.end - buf;
      buf = d.end;
      d = hp2_parse(&parser, buf, len);
      assert(d.type == HP2_VALUE);
      assert(d.partial == 0);
      expect_eq(req->headers[num_headers][1], d);

      num_headers++;
    }
  }

  assert(num_headers == req->num_headers);

  assert(body_len == body_read);
  assert(strcmp(body, req->body) == 0);
  printf("bodys match. body_len = %d\n", body_len);

  assert(d.type == HP2_MSG_END);
}


void manual_test_CURL_GET() {
  hp2_parser parser;
  hp2_datum d;
  const struct message* r = &requests[CURL_GET];
  int raw_len =  strlen(r->raw);
  int len = raw_len;
  const char* buf = r->raw;

  hp2_req_init(&parser);

  /* HP2_MSG_START */
  d = hp2_parse(&parser, buf, len);
  assert(d.type == HP2_MSG_START);
  assert(d.partial == 0);
  assert(d.start == d.end);
  assert(d.end == r->raw);

  /* Method = "GET" */
  len -= d.end - buf;
  buf = d.end;
  assert(len == raw_len);
  assert(buf == r->raw);
  d = hp2_parse(&parser, buf, len);
  assert(d.type == HP2_METHOD);
  assert(d.partial == 0);
  expect_eq(r->method, d);

  /* HP2_URL = "/test" */
  len -= d.end - buf;
  buf = d.end;
  d = hp2_parse(&parser, buf, len);
  assert(d.type == HP2_URL);
  assert(d.partial == 0);
  expect_eq(r->request_url, d);

  /* HP2_FIELD = "User-Agent" */
  len -= d.end - buf;
  buf = d.end;
  d = hp2_parse(&parser, buf, len);
  assert(d.type == HP2_FIELD);
  assert(d.partial == 0);
  expect_eq(r->headers[0][0], d);

  assert(parser.version_major == r->http_major);
  assert(parser.version_minor == r->http_minor);

  /* HP2_VALUE = "curl/7.18.0 (i486-pc-linux-gnu) libcurl/7.18.0 OpenSSL/0.9.8g zlib/1.2.3.3 libidn/1.1" */
  len -= d.end - buf;
  buf = d.end;
  d = hp2_parse(&parser, buf, len);
  assert(d.type == HP2_VALUE);
  assert(d.partial == 0);
  expect_eq(r->headers[0][1], d);

  /* HP2_FIELD = "Host" */
  len -= d.end - buf;
  buf = d.end;
  d = hp2_parse(&parser, buf, len);
  assert(d.type == HP2_FIELD);
  assert(d.partial == 0);
  expect_eq(r->headers[1][0], d);

  /* HP2_VALUE = "0.0.0.0=5000" */
  len -= d.end - buf;
  buf = d.end;
  d = hp2_parse(&parser, buf, len);
  assert(d.type == HP2_VALUE);
  assert(d.partial == 0);
  expect_eq(r->headers[1][1], d);

  /* HP2_FIELD = "Accept" */
  len -= d.end - buf;
  buf = d.end;
  d = hp2_parse(&parser, buf, len);
  assert(d.type == HP2_FIELD);
  assert(d.partial == 0);
  expect_eq(r->headers[2][0], d);

  // HP2_VALUE = "*/*"
  len -= d.end - buf;
  buf = d.end;
  d = hp2_parse(&parser, buf, len);
  assert(d.type == HP2_VALUE);
  assert(d.partial == 0);
  expect_eq(r->headers[2][1], d);

  // HP2_HEADER_END
  len -= d.end - buf;
  buf = d.end;
  d = hp2_parse(&parser, buf, len);
  assert(d.type == HP2_HEADER_END);
  assert(d.start == d.end);
  // CURL_GET has not body, so the headers should be at the end of the buffer.
  assert(d.end == r->raw + raw_len);

  // HP2_MSG_END
  len -= d.end - buf;
  buf = d.end;
  d = hp2_parse(&parser, buf, len);
  assert(d.type == HP2_MSG_END);
  assert(d.partial == 0);
  assert(d.start == d.end);
  // We're at the end of the buffer
  assert(d.end == r->raw + raw_len);

  // If we call hp2_parse() again, we get HP2_EAGAIN.
  d = hp2_parse(&parser, d.end, 0);
  assert(d.type == HP2_EAGAIN);
}


int main() {
  int i;

  printf("sizeof(hp2_datum) = %d\n", (int)sizeof(hp2_datum));
  printf("sizeof(hp2_parser) = %d\n", (int)sizeof(hp2_parser));

  manual_test_CURL_GET();

  for (i = 0; requests[i].name; i++) {
    printf("test_req(%d, %s)\n", i, requests[i].name);
    test_req(&requests[i]);
  }

  return 0;
}
