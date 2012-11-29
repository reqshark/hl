#include <stddef.h>
#include <assert.h>
#include "hp2.h"


#define CONNECTION "connection"
#define CONTENT_LENGTH "content-length"
#define TRANSFER_ENCODING "transfer-encoding"
#define UPGRADE "upgrade"
#define CHUNKED "chunked"
#define KEEP_ALIVE "keep-alive"
#define CLOSE "close"

enum state {
  S_METHOD_START = 0,
  S_METHOD = 1,
  S_URL_START = 2,
  S_URL,
  S_REQ_H,
  S_REQ_HT,
  S_REQ_HTT,
  S_REQ_HTTP,
  S_REQ_HTTP_SLASH,
  S_REQ_VMAJOR,
  S_REQ_VPERIOD,
  S_REQ_VMINOR,
  S_REQ_CR,
  S_REQ_CRLF,
  S_FIELD_START,
  S_FIELD_START_CR,
  S_FIELD,
  S_FIELD_COLON,
  S_VALUE_START,
  S_VALUE,
  S_VALUE_CR,
  S_VALUE_CRLF,
  S_BODY_START,

  HS_ANYTHING,
  HS_C,
  HS_CO,
  HS_CON,
  HS_MATCHING_CONNECTION,
  HS_MATCHING_CONTENT_LENGTH,
  HS_MATCHING_TRANSFER_ENCODING,
  HS_MATCHING_KEEP_ALIVE,
  HS_MATCHING_CLOSE
};

void hp2_req_init(hp2_parser* parser) {
  parser->last_datum = HP2_EAGAIN;
  parser->state = S_METHOD_START;
  parser->version_major = 0;
  parser->version_minor = 0;
  parser->upgrade = 0;
  parser->content_length = 0;
  parser->body_read = 0;
  parser->code = 0;
}


#define LOWER(c) ((c) | 0x20)
/* TODO replace with lookup tables. */
#define IS_LETTER(c) (('a' <= (c) && (c) <= 'z') || ('A' <= (c) && (c) <= 'Z'))
#define IS_WHITESPACE(c) ((c) == ' ' || (c) == '\n' || (c) == '\r')
#define IS_NUMBER(c) ('0' <= (c) && (c) <= '9')
#define IS_URL_CHAR(c) is_url_char[(unsigned char)c]
#define IS_METHOD_CHAR(c) (IS_LETTER(c) || c == '-')
#define IS_FIELD_CHAR(c) (IS_LETTER(c) || IS_NUMBER(c) || (c) == '-')


const int is_url_char[] = {
  /*  NUL SOH STX ETX EOT ENQ ACK \a \b \t \n \v \f \r SO SI  */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  /*  DLE DC1 DC2 DC3 DC4 NAK SYN ETB CAN EM SUB ESC FS GS RS US  */
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  /*  SPACE ! " # $ % & ´ ( ) * + , - . /  */
  0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 1,
  /*  0 1 2 3 4 5 6 7 8 9 : ; < = > ?  */
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  /*  @ A B C D E F G H I J K L M N O  */
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  /*  P Q R S T U V W X Y Z [ \ ] ^ _  */
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  /*  ` a b c d e f g h i j k l m n o  */
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  /*  p q r s t u v w x y z { | } ~ DEL */
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 0,
  /* If the 7-bit is set then it's a utf8 char. Accepted. */
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
};



static void headers_complete(hp2_parser* parser,
                             const char* head,
                             hp2_datum* datum) {
  assert(datum->start == NULL);

  /* If the message has zero length, don't emit the HP2_HEADERS_COMPLETE datum,
   * just do HP2_MSG_COMPLETE.
   */
  if (parser->content_length == 0) {
    datum->type = HP2_MSG_COMPLETE;
  } else {
    assert(0 && "implement me");
    datum->type = HP2_HEADERS_COMPLETE;
  }
  datum->last = datum->partial = 0;
  datum->end = head + 1;
  parser->state = S_BODY_START;

  parser->body_read = 0;
}


hp2_datum hp2_parse(hp2_parser* parser, const char* data, size_t len) {
  hp2_datum datum; /* returned datum */
  const char* head; /* parser head */
  const char* end = data + len;

  datum.type = parser->last_datum;
  datum.start = parser->last_datum == HP2_EAGAIN ? NULL : data;
  datum.end = NULL;
  datum.last = 0;
  datum.partial = 0;

  for (head = data; head < end; head++) {
    char c = *head;
    switch (parser->state) {
      case S_METHOD_START: {
        if (IS_METHOD_CHAR(c)) {
          datum.start = head;
          datum.type = HP2_METHOD;
          parser->state = S_METHOD;
        } else if (!IS_WHITESPACE(c)) {
          goto error;
        }
        break;
      }

      case S_METHOD: {
        if (c == ' ') {
          assert(datum.type == HP2_METHOD);
          datum.end = head;
          parser->state = S_URL_START;
          goto datum_complete;
        }

        if (!IS_METHOD_CHAR(c)) {
          goto error;
        }
        break;
      }

      case S_URL_START: {
        if (c != ' ') {
          if (!IS_URL_CHAR(c)) {
            goto error;
          }

          datum.type = HP2_URL;
          datum.start = head;
          parser->state = S_URL;
        }
        break;
      }

      case S_URL: {
        assert(datum.type == HP2_URL);

        if (IS_URL_CHAR(c)) {
          break;
        }

        if (c == ' ' || c == '\r' || c == '\n') {
          datum.end = head;
          switch (c) {
            case ' ':
              parser->state = S_REQ_H;
              break;

            case '\r':
              parser->state = S_REQ_CRLF;
              break;

            case '\n':
              parser->state = S_FIELD_START;
              break;
          }
          goto datum_complete;
        } else {
          goto error;
        }
        break;
      }

      case S_REQ_H: {
        assert(datum.type == HP2_EAGAIN);
        if (c == ' ') {
          ;
        } else if (c == 'H') {
          parser->state = S_REQ_HT;
        } else {
          goto error;
        }
        break;
      }

      case S_REQ_HT: {
        assert(datum.type == HP2_EAGAIN);
        if (c == 'T') {
          parser->state = S_REQ_HTT;
        } else {
          goto error;
        }
        break;
      }

      case S_REQ_HTT: {
        assert(datum.type == HP2_EAGAIN);
        if (c == 'T') {
          parser->state = S_REQ_HTTP;
        } else {
          goto error;
        }
        break;
      }

      case S_REQ_HTTP: {
        assert(datum.type == HP2_EAGAIN);
        if (c == 'P') {
          parser->state = S_REQ_HTTP_SLASH;
        } else {
          goto error;
        }
        break;
      }

      case S_REQ_HTTP_SLASH: {
        if (c == '/') {
          parser->version_major = 0;
          parser->version_minor = 0;
          parser->state = S_REQ_VMAJOR;
        } else {
          goto error;
        }
        break;
      }

      case S_REQ_VMAJOR: {
        assert(datum.type == HP2_EAGAIN);
        if (!IS_NUMBER(c)) {
          goto error;
        }
        parser->version_major += c - '0';
        parser->state = S_REQ_VPERIOD;
        break;
      }

      case S_REQ_VPERIOD: {
        assert(datum.type == HP2_EAGAIN);
        if (c != '.') goto error;
        parser->state = S_REQ_VMINOR;
        break;
      }

      case S_REQ_VMINOR: {
        assert(datum.type == HP2_EAGAIN);
        if (!IS_NUMBER(c)) {
          goto error;
        }
        parser->version_minor += c - '0';
        parser->state = S_REQ_CR;
        break;
      }

      case S_REQ_CR: {
        assert(datum.type == HP2_EAGAIN);
        if (c == '\r') {
          parser->state = S_REQ_CRLF;
        } else if (c == '\n') {
          parser->state = S_FIELD_START;
        } else if (c == ' ') {
          ;
        } else {
          goto error;
        }
        break;
      }

      case S_REQ_CRLF: {
        assert(datum.type == HP2_EAGAIN);
        if (c == '\n') {
          parser->state = S_FIELD_START;
        } else {
          goto error;
        }
        break;
      }

      case S_FIELD_START: {
        assert(datum.type == HP2_EAGAIN);
        if (c == '\r') {
          parser->state = S_FIELD_START_CR;
        } else if (c == '\n') {
          headers_complete(parser, head, &datum);
          goto datum_complete;
        } else if (c == '\t') {
          /* Tabs indicate continued header value */
          assert(0 && "Implement me");
          goto error;
        } else if (c == ' ') {
          ;
        } else if (IS_FIELD_CHAR(c)) {
          datum.type = HP2_FIELD;
          datum.start = head;
          /* We need to read a couple of the headers. In particular
           * Content-Length, Connection, and Transfer-Encoding.
           */
          c = LOWER(c);
          switch (c) {
            case 'c':
              parser->header_state = HS_CO;
              break;

            case 't':
              parser->header_state = HS_MATCHING_TRANSFER_ENCODING;
              parser->match = TRANSFER_ENCODING;
              parser->i = 0;
              break;

            default:
              parser->header_state = HS_ANYTHING;
              break;
          }
          parser->state = S_FIELD;
        }
        break;
      }

      case S_FIELD_START_CR: {
        assert(datum.type == HP2_EAGAIN);
        if (c == '\n') {
          headers_complete(parser, head, &datum);
          goto datum_complete;
        } else {
          goto error;
        }
        break;
      }

      case S_FIELD: {
        assert(datum.type == HP2_FIELD);

        if (parser->header_state != HS_ANYTHING) {
          c = LOWER(c);
          switch (parser->header_state) {
            case HS_C: {
              parser->header_state = c == 'o'? HS_CO : HS_ANYTHING;
              break;
            }

            case HS_CO: {
              parser->header_state = c == 'n'? HS_CON : HS_ANYTHING;
              break;
            }

            case HS_CON: {
              if (c == 't') {
                parser->header_state = HS_MATCHING_CONTENT_LENGTH;
                parser->match = CONTENT_LENGTH;
                parser->i = 4;
              } else if (c == 'n') {
                parser->header_state = HS_MATCHING_CONNECTION;
                parser->match = CONNECTION;
                parser->i = 4;
              } else {
                parser->header_state = HS_ANYTHING;
              }
              break;
            }

            case HS_MATCHING_CONTENT_LENGTH:
            case HS_MATCHING_CONNECTION:
            case HS_MATCHING_TRANSFER_ENCODING:
              if (parser->match[parser->i++] != c) {
                parser->header_state = HS_ANYTHING;
              }
              break;

            default:
              assert(0);
              goto error;
          }
        }

        if (c == ':') {
          if (parser->header_state != HS_ANYTHING &&
              parser->match[parser->i] != '\0') {
            parser->header_state = HS_ANYTHING;
          }

          datum.end = head;
          assert(datum.partial == 0);
          parser->state = S_FIELD_COLON;
          head--; /* XXX back up head... */
          goto datum_complete;
        }

        if (!IS_FIELD_CHAR(c)) {
          goto error;
        }
        break;
      }

      case S_FIELD_COLON: {
        assert(datum.type == HP2_EAGAIN);
        if (c != ':') goto error;
        parser->state = S_VALUE_START;
        break;
      }

      case S_VALUE_START:
        /* Skip spaces at the beginning of values */
        if (c == ' ') break;
        assert(datum.type == HP2_EAGAIN);
        datum.type = HP2_VALUE;
        datum.start = head;
        parser->state = S_VALUE;


        switch (parser->header_state) {
          case HS_MATCHING_CONTENT_LENGTH: {
            parser->content_length = 0;
            break;
          }

          case HS_MATCHING_CONNECTION: {
            c = LOWER(c);
            if (c == 'k') {
              parser->header_state = HS_MATCHING_KEEP_ALIVE;
              parser->match = KEEP_ALIVE;
              parser->i = 0;
            } else if (c == 'c') {
              parser->header_state = HS_MATCHING_CLOSE;
              parser->match = CLOSE;
              parser->i = 0;
            } else {
              parser->header_state = HS_ANYTHING;
            }
            break;
          }

          case HS_MATCHING_TRANSFER_ENCODING: {
            parser->match = CHUNKED;
            parser->i = 0;
            break;
          }

          case HS_ANYTHING:
            break;

          default:
            assert(0 && "should not reach here");
        }
        /* pass-through to S_VALUE */

      case S_VALUE: {
        if (c == '\r' || c == '\n') {
          assert(datum.type == HP2_VALUE);
          datum.end = head;
          parser->state = c == '\r' ? S_VALUE_CR : S_VALUE_CRLF;
          head--; /* XXX back up head */
          goto datum_complete;
        }

        if (parser->header_state != HS_ANYTHING) {
          c = LOWER(c);
          switch (parser->header_state) {
            case HS_MATCHING_CONTENT_LENGTH: {
              if (!IS_NUMBER(c)) goto error;
              parser->content_length *= 10;
              parser->content_length += c - '0';
              break;
            }

            case HS_MATCHING_KEEP_ALIVE:
            case HS_MATCHING_CLOSE:
            case HS_MATCHING_TRANSFER_ENCODING: {
              if (parser->match[parser->i++] != c) {
                parser->header_state = HS_ANYTHING;
              }
              break;
            }

            default:
              assert(0 && "should not reach here");
          }
        }
        break;
      }

      case S_VALUE_CR: {
        assert(datum.type == HP2_EAGAIN);
        if (c != '\r') goto error;
        parser->state = S_VALUE_CRLF;
        break;
      }

      case S_VALUE_CRLF: {
        assert(datum.type == HP2_EAGAIN);
        if (c != '\n') goto error;
        parser->state = S_FIELD_START;
        break;
      }

      case S_BODY_START: {
        if (parser->content_length == 0) {
          datum.type = HP2_MSG_COMPLETE;
          datum.end = head;
          parser->state = S_METHOD_START;
          goto datum_complete;
        }
        assert(0 && "implement me");
        break;
      }
    }
  }

  datum.end = head;
  datum.partial = 1;
  parser->last_datum = datum.type;
  return datum;

datum_complete:
  assert(datum.partial == 0);
  assert(datum.end);
  parser->last_datum = HP2_EAGAIN;
  return datum;

error:
  datum.type = HP2_ERROR;
  datum.last = 1;
  datum.start = NULL;
  datum.end = head;
  return datum;
}
