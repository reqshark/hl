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
  const char* buf = req->raw;
  int raw_len = strlen(req->raw);
  int len = raw_len;
  int num_headers = 0;
  int body_len = strlen(req->body);
  char* body = malloc(body_len);
  int body_read = 0;
  int chunk_len;

  hp2_parser parser;
  hp2_req_init(&parser);

  hp2_datum d = hp2_parse(&parser, buf, len);
  assert(d.type == HP2_METHOD);
  assert(d.partial == 0);
  assert(d.last == 0);
  expect_eq(req->method, d);

  len -= d.end - buf;
  buf = d.end;
  d = hp2_parse(&parser, buf, len);
  assert(d.type == HP2_URL);
  assert(d.partial == 0);
  assert(d.last == 0);
  expect_eq(req->request_url, d);

  for (;;) {
    /* field */
    len -= d.end - buf;
    buf = d.end;
    d = hp2_parse(&parser, buf, len);

    if (d.type != HP2_FIELD) break;

    assert(d.type == HP2_FIELD);
    assert(d.partial == 0);
    assert(d.last == 0);
    expect_eq(req->headers[num_headers][0], d);

    /* value */
    len -= d.end - buf;
    buf = d.end;
    d = hp2_parse(&parser, buf, len);
    assert(d.type == HP2_VALUE);
    assert(d.partial == 0);
    assert(d.last == 0);
    expect_eq(req->headers[num_headers][1], d);

    num_headers++;
  }

  /* We're done. There is no body. The datum is HP2_MSG_COMPLETE. */
  if (parser.content_length == 0) goto msg_complete;

  assert(d.type == HP2_HEADERS_COMPLETE);
  assert(d.partial == 0);
  assert(d.last == 0);
  assert(d.start == NULL);

  /* Now read the body */
  for (;;) {
    len -= d.end - buf;
    buf = d.end;
    d = hp2_parse(&parser, buf, len);

    if (d.type != HP2_BODY) break;

    assert(d.type == HP2_BODY);
    assert(d.partial == 0);
    assert(d.last == 0);

    chunk_len = d.end - d.start;
    memcpy(body + body_read, d.start, chunk_len);
    body_read += chunk_len;
  }

msg_complete:
  assert(body_len == body_read);
  assert(strcmp(body, req->body) == 0);
  printf("bodys match. body_len = %d\n", body_len);

  assert(d.type == HP2_MSG_COMPLETE);
  assert(d.partial == 0);
  assert(d.last == 0);
  assert(d.start == NULL);
}


void manual_test_CURL_GET() {
  const struct message* r = &requests[CURL_GET];

  hp2_parser parser;
  hp2_req_init(&parser);

  int raw_len =  strlen(r->raw);

  int len = raw_len;
  const char* buf = r->raw;

  /* Method = "GET" */
  hp2_datum d = hp2_parse(&parser, buf, len);
  assert(d.type == HP2_METHOD);
  assert(d.partial == 0);
  assert(d.last == 0);
  expect_eq(r->method, d);

  /* URL = "/test" */
  len -= d.end - buf;
  buf = d.end;
  d = hp2_parse(&parser, buf, len);
  assert(d.type == HP2_URL);
  assert(d.partial == 0);
  assert(d.last == 0);
  expect_eq(r->request_url, d);

  /* FIELD = "User-Agent" */
  len -= d.end - buf;
  buf = d.end;
  d = hp2_parse(&parser, buf, len);
  assert(d.type == HP2_FIELD);
  assert(d.partial == 0);
  assert(d.last == 0);
  expect_eq(r->headers[0][0], d);

  assert(parser.version_major == r->http_major);
  assert(parser.version_minor == r->http_minor);

  /* VALUE = "curl/7.18.0 (i486-pc-linux-gnu) libcurl/7.18.0 OpenSSL/0.9.8g zlib/1.2.3.3 libidn/1.1" */
  len -= d.end - buf;
  buf = d.end;
  d = hp2_parse(&parser, buf, len);
  assert(d.type == HP2_VALUE);
  assert(d.partial == 0);
  assert(d.last == 0);
  expect_eq(r->headers[0][1], d);

  /* FIELD = "Host" */
  len -= d.end - buf;
  buf = d.end;
  d = hp2_parse(&parser, buf, len);
  assert(d.type == HP2_FIELD);
  assert(d.partial == 0);
  assert(d.last == 0);
  expect_eq(r->headers[1][0], d);

  /* VALUE = "0.0.0.0=5000" */
  len -= d.end - buf;
  buf = d.end;
  d = hp2_parse(&parser, buf, len);
  assert(d.type == HP2_VALUE);
  assert(d.partial == 0);
  assert(d.last == 0);
  expect_eq(r->headers[1][1], d);

  /* FIELD = "Accept" */
  len -= d.end - buf;
  buf = d.end;
  d = hp2_parse(&parser, buf, len);
  assert(d.type == HP2_FIELD);
  assert(d.partial == 0);
  assert(d.last == 0);
  expect_eq(r->headers[2][0], d);

  // VALUE = "*/*"
  len -= d.end - buf;
  buf = d.end;
  d = hp2_parse(&parser, buf, len);
  assert(d.type == HP2_VALUE);
  assert(d.partial == 0);
  assert(d.last == 0);
  expect_eq(r->headers[2][1], d);

  // MSG_COMPLETE
  len -= d.end - buf;
  buf = d.end;
  d = hp2_parse(&parser, buf, len);
  assert(d.type == HP2_MSG_COMPLETE);
  assert(d.partial == 0);
  assert(d.start == NULL);

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
