#include <stddef.h>
#include <assert.h>
#include "hp2.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))

enum flag {
  F_CONNECTION_KEEP_ALIVE     = 0x01,
  F_CONNECTION_CLOSE          = 0x02,
  F_TRANSFER_ENCODING_CHUNKED = 0x04,
  F_TRAILER                   = 0x08
};

enum state {
  S_REQ_START,
  S_MSG_END,

  S_METHOD_START,
  S_METHOD,
  S_URL_START,
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
  S_IDENTITY_CONTENT,

  S_CHUNK_START,
  S_CHUNK_LEN,
  S_CHUNK_LEN_CRLF,
  S_CHUNK_KV,
  S_CHUNK_CONTENT,
  S_CHUNK_CONTENT_CR,
  S_CHUNK_CONTENT_CRLF,

  HS_ANYTHING,
  HS_C,
  HS_CO,
  HS_CON,
  HS_MATCH_CONNECTION,
  HS_MATCH_CONTENT_LENGTH,
  HS_MATCH_TRANSFER_ENCODING,
  HS_MATCH_KEEP_ALIVE,
  HS_MATCH_CLOSE
};

void hp2_req_init(hp2_parser* parser) {
  parser->last = HP2_EAGAIN;
  parser->state = S_REQ_START;
}


#define LOWER(c) ((c) | 0x20)
/* TODO replace with lookup tables. */
#define IS_LETTER(c) (('a' <= (c) && (c) <= 'z') || ('A' <= (c) && (c) <= 'Z'))
#define IS_WHITESPACE(c) ((c) == ' ' || (c) == '\n' || (c) == '\r')
#define IS_NUMBER(c) ('0' <= (c) && (c) <= '9')
#define IS_URL_CHAR(c) is_url_char[(unsigned char)c]
#define IS_METHOD_CHAR(c) (IS_LETTER(c) || c == '-')
#define IS_FIELD_CHAR(c) (IS_LETTER(c) || IS_NUMBER(c) || (c) == '-')
#define IS_CHUNK_KV_CHAR(c) (IS_LETTER(c) || IS_NUMBER(c) || (c) == '=' || \
                             (c) == ' ' || (c) == ';')
#define UNHEX(c) ((int)unhex[(unsigned char)c])


const char is_url_char[] = {
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
  /* If the 7th bit is set, then it's part of a UTF8 char. Accepted. */
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
};

const char unhex[] = {
  /*  NUL SOH STX ETX EOT ENQ ACK \a \b \t \n \v \f \r SO SI  */
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  /*  DLE DC1 DC2 DC3 DC4 NAK SYN ETB CAN EM SUB ESC FS GS RS US  */
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  /*  SPACE ! " # $ % & ´ ( ) * + , - . /  */
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  /*  0 1 2 3 4 5 6 7 8 9 : ; < = > ?  */
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, -1, -1, -1, -1, -1, -1,
  /*  @ A B C D E F G H I J K L M N O  */
  -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  /*  P Q R S T U V W X Y Z [ \ ] ^ _  */
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  /*  ` a b c d e f g h i j k l m n o  */
  -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  /*  p q r s t u v w x y z { | } ~ DEL */
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  /* 7bit set */
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

#define CONNECTION "connection"
#define CONTENT_LENGTH "content-length"
#define TRANSFER_ENCODING "transfer-encoding"
#define UPGRADE "upgrade"
#define CHUNKED "chunked"
#define KEEP_ALIVE "keep-alive"
#define CLOSE "close"


/* Don't call this directly, Use HEADER_COMPLETE macro.  This is code to be run
 * when the normal header is complete. The HEADER_COMPLETE macro handles both
 * normal and trialing header.
 */
static void basic_header_complete(hp2_parser* parser,
                                  const char* head,
                                  hp2_token* token) {
  assert(token->start == NULL);
  assert(!(parser->flags & F_TRAILER));

  if (parser->flags & F_TRANSFER_ENCODING_CHUNKED) {
    parser->state = S_CHUNK_START;
  } else {
    if (parser->content_length <= 0) {
      /* XXX should check connection header to see if we're accepting more */
      parser->state = S_MSG_END;
    } else {
      parser->state = S_IDENTITY_CONTENT;
    }
  }

  token->kind = HP2_HEADER_END;
  token->partial = 0;
  token->start = token->end = head + 1;

  parser->content_read = 0;
}

#define HEADER_COMPLETE()                           \
  do {                                              \
    if (parser->flags & F_TRAILER) {                \
      parser->state = S_MSG_END;                    \
    } else {                                        \
      basic_header_complete(parser, head, &token);  \
      goto token_complete;                          \
    }                                               \
  } while(0)


hp2_token hp2_parse(hp2_parser* parser, const char* data, size_t len) {
  hp2_token token; /* returned token */
  const char* head = data; /* parser head */
  const char* end = data + len;
  int to_read;
  int value;

  token.kind = parser->last;
  token.start = parser->last == HP2_EAGAIN ? NULL : data;
  token.end = NULL;
  token.partial = 0;

  for (; head < end || parser->state == S_MSG_END; head++) {
    char c = *head;
    switch (parser->state) {
      default: assert(0);

      case S_REQ_START: {
        if (IS_METHOD_CHAR(c)) {
          /* Reset the parser */
          parser->flags = 0;
          parser->content_length = -1; /* Indicates no content-length header. */
          parser->version_major = 0;
          parser->version_minor = 0;
          parser->upgrade = 0;
          parser->content_read = 0;

          token.start = token.end = head;
          token.kind = HP2_MSG_START;
          parser->state = S_METHOD_START;
          goto token_complete;
        } else if (!IS_WHITESPACE(c)) {
          goto error;
        }
        break;
      }

      case S_MSG_END: {
        token.kind = HP2_MSG_END;
        token.start = token.end = head;
        /* TODO check to see if we have persistant connection */
        parser->state = S_REQ_START;
        goto token_complete;
        break;
      }

      case S_METHOD_START: {
        /* We already checked this in S_REQ_START */
        assert(IS_METHOD_CHAR(c));

        token.start = head;
        token.kind = HP2_METHOD;
        parser->state = S_METHOD;
        break;
      }

      case S_METHOD: {
        if (c == ' ') {
          assert(token.kind == HP2_METHOD);
          token.end = head;
          parser->state = S_URL_START;
          goto token_complete;
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

          token.kind = HP2_URL;
          token.start = head;
          parser->state = S_URL;
        }
        break;
      }

      case S_URL: {
        assert(token.kind == HP2_URL);

        if (!IS_URL_CHAR(c)) {
          token.end = head;
          parser->state = S_REQ_H;
          goto token_complete;
        }
        break;
      }

      case S_REQ_H: {
        assert(token.kind == HP2_EAGAIN);
        if (c == ' ') {
          ;
        } else if (c == 'H') {
          parser->state = S_REQ_HT;
        } else if (c == '\r') {
          parser->state = S_REQ_CRLF;
        } else if (c == '\n') {
          parser->state = S_FIELD_START;
        } else {
          goto error;
        }
        break;
      }

      case S_REQ_HT: {
        assert(token.kind == HP2_EAGAIN);
        if (c == 'T') {
          parser->state = S_REQ_HTT;
        } else {
          goto error;
        }
        break;
      }

      case S_REQ_HTT: {
        assert(token.kind == HP2_EAGAIN);
        if (c == 'T') {
          parser->state = S_REQ_HTTP;
        } else {
          goto error;
        }
        break;
      }

      case S_REQ_HTTP: {
        assert(token.kind == HP2_EAGAIN);
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
        assert(token.kind == HP2_EAGAIN);
        if (!IS_NUMBER(c)) {
          goto error;
        }
        parser->version_major += c - '0';
        parser->state = S_REQ_VPERIOD;
        break;
      }

      case S_REQ_VPERIOD: {
        assert(token.kind == HP2_EAGAIN);
        if (c != '.') goto error;
        parser->state = S_REQ_VMINOR;
        break;
      }

      case S_REQ_VMINOR: {
        assert(token.kind == HP2_EAGAIN);
        if (!IS_NUMBER(c)) {
          goto error;
        }
        parser->version_minor += c - '0';
        parser->state = S_REQ_CR;
        break;
      }

      case S_REQ_CR: {
        assert(token.kind == HP2_EAGAIN);
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
        assert(token.kind == HP2_EAGAIN);
        if (c == '\n') {
          parser->state = S_FIELD_START;
        } else {
          goto error;
        }
        break;
      }

      case S_FIELD_START: {
        assert(token.kind == HP2_EAGAIN);

        if (c == '\r') {
          parser->state = S_FIELD_START_CR;
        } else if (c == '\n') {
          HEADER_COMPLETE();
        } else if (c == '\t') {
          /* Tabs indicate continued header value */
          assert(0 && "Implement me");
          goto error;
        } else if (c == ' ') {
          ;
        } else if (IS_FIELD_CHAR(c)) {
          token.kind = HP2_FIELD;
          token.start = head;
          if (parser->flags & F_TRAILER) {
            /* Trailing field/values don't need to be parsed. */
            parser->header_state = HS_ANYTHING;
          } else {
            /* We need to read a couple of the headers. In particular
             * Content-Length, Connection, and Transfer-Encoding.
             */
            c = LOWER(c);
            switch (c) {
              case 'c':
                parser->header_state = HS_C;
                break;

              case 't':
                parser->header_state = HS_MATCH_TRANSFER_ENCODING;
                parser->match = TRANSFER_ENCODING;
                parser->i = 1;
                break;

              default:
                parser->header_state = HS_ANYTHING;
                break;
            }
          }
          parser->state = S_FIELD;
        }
        break;
      }

      case S_FIELD_START_CR: {
        assert(token.kind == HP2_EAGAIN);
        if (c == '\n') {
          HEADER_COMPLETE();
        } else {
          goto error;
        }
        break;
      }

      case S_FIELD: {
        assert(token.kind == HP2_FIELD);

        if (c == ':') {
          if (parser->header_state != HS_ANYTHING &&
              parser->match[parser->i] != '\0') {
            parser->header_state = HS_ANYTHING;
          }

          token.end = head;
          assert(token.partial == 0);
          parser->state = S_FIELD_COLON;
          head--; /* XXX back up head... */
          goto token_complete;
        }

        if (parser->header_state != HS_ANYTHING) {
          c = LOWER(c);
          switch (parser->header_state) {
            case HS_C: {
              parser->header_state = c == 'o' ? HS_CO : HS_ANYTHING;
              break;
            }

            case HS_CO: {
              parser->header_state = c == 'n' ? HS_CON : HS_ANYTHING;
              break;
            }

            case HS_CON: {
              if (c == 't') {
                parser->header_state = HS_MATCH_CONTENT_LENGTH;
                parser->match = CONTENT_LENGTH;
                parser->i = 4;
                break;
              } else if (c == 'n') {
                parser->header_state = HS_MATCH_CONNECTION;
                parser->match = CONNECTION;
                parser->i = 4;
                break;
              } else {
                parser->header_state = HS_ANYTHING;
              }
              break;
            }

            case HS_MATCH_CONTENT_LENGTH:
            case HS_MATCH_CONNECTION:
            case HS_MATCH_TRANSFER_ENCODING:
              if (parser->match[parser->i++] == c) break;
              parser->header_state = HS_ANYTHING;
              break;

            default:
              assert(0);
              goto error;
          }
        }

        if (!IS_FIELD_CHAR(c)) {
          goto error;
        }
        break;
      }

      case S_FIELD_COLON: {
        assert(token.kind == HP2_EAGAIN);
        if (c != ':') goto error;
        parser->state = S_VALUE_START;
        break;
      }

      case S_VALUE_START:
        /* Skip spaces at the beginning of values */
        if (c == ' ') break;
        assert(token.kind == HP2_EAGAIN);
        token.kind = HP2_VALUE;
        token.start = head;
        parser->state = S_VALUE;

        switch (parser->header_state) {
          case HS_ANYTHING:
            break;

          case HS_MATCH_CONTENT_LENGTH: {
            parser->content_length = 0;
            break;
          }

          case HS_MATCH_CONNECTION: {
            c = LOWER(c);
            if (c == 'k') {
              parser->header_state = HS_MATCH_KEEP_ALIVE;
              parser->match = KEEP_ALIVE;
              parser->i = 0;
            } else if (c == 'c') {
              parser->header_state = HS_MATCH_CLOSE;
              parser->match = CLOSE;
              parser->i = 0;
            } else {
              parser->header_state = HS_ANYTHING;
            }
            break;
          }

          case HS_MATCH_TRANSFER_ENCODING: {
            parser->match = CHUNKED;
            parser->i = 0;
            break;
          }

          default:
            assert(0 && "should not reach here");
        }
        /* pass-through to S_VALUE */

      case S_VALUE: {
        /* End of header value */
        if (c == '\r' || c == '\n') {
          assert(token.kind == HP2_VALUE);
          token.end = head;
          parser->state = c == '\r' ? S_VALUE_CR : S_VALUE_CRLF;

          if (parser->header_state != HS_ANYTHING) {
            switch (parser->header_state) {
              case HS_MATCH_KEEP_ALIVE: {
                if (parser->match[parser->i] == '\0') {
                  parser->flags |= F_CONNECTION_KEEP_ALIVE;
                }
                break;
              }

              case HS_MATCH_CLOSE: {
                if (parser->match[parser->i] == '\0') {
                  parser->flags |= F_CONNECTION_CLOSE;
                }
                break;
              }

              case HS_MATCH_TRANSFER_ENCODING: {
                if (parser->match[parser->i] == '\0') {
                  parser->flags |= F_TRANSFER_ENCODING_CHUNKED;
                }
                break;
              }
            }
          }

          head--; /* XXX back up head */
          goto token_complete;
        }

        /* header value */
        if (parser->header_state != HS_ANYTHING) {
          c = LOWER(c);
          switch (parser->header_state) {
            case HS_MATCH_CONTENT_LENGTH: {
              if (!IS_NUMBER(c)) goto error;
              parser->content_length *= 10;
              parser->content_length += c - '0';
              break;
            }

            case HS_MATCH_KEEP_ALIVE:
            case HS_MATCH_CLOSE:
            case HS_MATCH_TRANSFER_ENCODING: {
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
        assert(token.kind == HP2_EAGAIN);
        if (c != '\r') goto error;
        parser->state = S_VALUE_CRLF;
        break;
      }

      case S_VALUE_CRLF: {
        assert(token.kind == HP2_EAGAIN);
        if (c != '\n') goto error;
        parser->state = S_FIELD_START;
        break;
      }

      case S_IDENTITY_CONTENT: {
        token.kind = HP2_BODY;
        token.start = head;
        to_read = MIN(end - head,
                      parser->content_length - parser->content_read);
        parser->content_read += to_read;
        head += to_read;

        if (parser->content_length == parser->content_read) {
          assert(token.kind == HP2_BODY);
          token.end = head;
          parser->state = S_MSG_END;
          goto token_complete;
        }
        break;
      }

      case S_CHUNK_START: {
        value = UNHEX(c);
        if (value < 0) goto error;
        parser->chunk_read = 0;
        parser->chunk_len = value;
        parser->state = S_CHUNK_LEN;
        break;
      }

      case S_CHUNK_LEN: {
        value = UNHEX(c);
        if (value < 0) {
          if (c == '\r') {
            parser->state = S_CHUNK_LEN_CRLF;
          } else if (IS_CHUNK_KV_CHAR(c)) {
            parser->state = S_CHUNK_KV;
          } else {
            goto error;
          }
        } else {
          parser->chunk_len *= 16;
          parser->chunk_len += value;
        }
        break;
      }

      case S_CHUNK_KV: {
        /* We completely ignore chunked key-value pairs */
        if (IS_CHUNK_KV_CHAR(c)) {
          ;
        } else if (c == '\r') {
          parser->state = S_CHUNK_LEN_CRLF;
        } else {
          goto error;
        }
        break;
      }

      case S_CHUNK_LEN_CRLF: {
        if (c != '\n') goto error;
        assert(parser->chunk_read == 0);

        if (parser->chunk_len == 0) {
          /* Last chunk. There may be trailing headers. RFC 2616 14.40. */
          parser->state = S_FIELD_START;
          parser->flags |= F_TRAILER;
        } else {
          parser->state = S_CHUNK_CONTENT;
        }
        break;
      }

      case S_CHUNK_CONTENT: {
        assert(parser->chunk_len > 0);
        token.kind = HP2_BODY;
        token.start = head;
        to_read = MIN(end - head,
                      parser->chunk_len - parser->chunk_read);
        parser->chunk_read += to_read;
        head += to_read;

        if (parser->chunk_len == parser->chunk_read) {
          assert(token.kind == HP2_BODY);
          token.end = head;
          parser->state = S_CHUNK_CONTENT_CR;
          goto token_complete;
        }
        break;
      }

      case S_CHUNK_CONTENT_CR: {
        if (c != '\r') goto error;
        parser->state = S_CHUNK_CONTENT_CRLF;
        /* We shouldn't get here in the case that we're on the last chunk. */
        assert(parser->chunk_len > 0);
        break;
      }

      case S_CHUNK_CONTENT_CRLF: {
        if (c != '\n') goto error;

        /* We shouldn't get here in the case that we're on the last chunk. */
        assert(parser->chunk_len > 0);

        parser->state = S_CHUNK_START;
        break;
      }
    }
  }

  token.end = head;
  token.partial = 1;
  parser->last = token.kind;
  return token;

token_complete:
  assert(token.partial == 0);
  assert(token.end);
  parser->last = HP2_EAGAIN;
  return token;

error:
  token.kind = HP2_ERROR;
  token.start = NULL;
  token.end = head;
  return token;
}
