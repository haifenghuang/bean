#include "vm.h"
#include "stdio.h"
#include "bstate.h"
#include "bstring.h"
#include "bobject.h"
#include "berror.h"
#include "bstate.h"

static TValue * find_variable(bean_State * B, TValue * name) {
  TValue * res;
  Scope * scope = B->l_G->cScope;
  while (scope) {
    res = hash_get(B, scope->variables, name);
    if (res) break;
    scope = scope -> previous;
  }
  return res;
}

static int compareTwoTValue(bean_State * B, TValue * v1, TValue *v2) {
  TValue * convertV1 = ttisstring(v1) ? v1 : tvalue_inspect(B, v1);
  TValue * convertV2 = ttisstring(v2) ? v2 : tvalue_inspect(B, v2);
  return strcmp(getstr(svalue(convertV1)), getstr(svalue(convertV2)));
}

void * operand_decode(char * buff) {
  size_t i = 0;
  int64_t p = 1;
  while (i < COMMON_POINTER_SIZE) {
    p <<= 8;
    p |= (uint8_t)buff[8 - i - 1];
    i ++;
  }
  return (void*)p;
}

typedef struct Thread {
  TValue ** stack;
  TValue ** esp;
} Thread;

int executeInstruct(bean_State * B) {
  Thread * thread = malloc(sizeof(Thread));
  thread -> stack = malloc(sizeof(TValue*) * 3000);
  thread -> esp = thread -> stack;

  char * ip;
  ip = G(B)->instructionStream->buffer;
  char code;

#define READ_BYTE() *ip++;
#define PUSH(value) (*thread->esp++ = value)
#define POP(value) (*(--thread->esp))
#define DECODE loopStart:                       \
  code = READ_BYTE();                         \
  switch (code)
#define CASE(shortOp) case OP_BEAN_##shortOp
#define LOOP() goto loopStart
#define CAL_STMT(action) do {                                           \
    TValue * v2 = POP();                                                \
    TValue * v1 = POP();                                                \
    if (!ttisnumber(v1)) {                                              \
      eval_error(B, "%s", "left operand of "#action" must be number");  \
    }                                                                   \
    if (!ttisnumber(v2)) {                                              \
      eval_error(B, "%s", "right operand of "#action" must be number"); \
    }                                                                   \
    setnvalue(v1, action(nvalue(v1), nvalue(v2)));                      \
    free(v2);                                                           \
    PUSH(v1);                                                           \
    LOOP();                                                             \
  } while(0)

#define COMPARE_STMT(action) do {                                       \
    TValue * v1 = POP();                                                \
    TValue * v2 = POP();                                                \
    if (ttisnumber(v1) && ttisnumber(v2)) {                             \
      v1 = action(nvalue(v1), nvalue(v2)) ? G(B)->tVal : G(B)->fVal;    \
    } else {                                                            \
      int result = compareTwoTValue(B, v1, v2);                         \
      v1 = action(result, 0) ? G(B)->tVal : G(B)->fVal;                 \
    }                                                                   \
    free(v2);                                                           \
    PUSH(v1);                                                           \
    LOOP();                                                             \
 } while(0)

#define CAL_EQ_STMT(action) do {                                        \
    TValue * v2 = POP();                                                \
    TValue * name = POP();                                              \
    TValue * v1 = find_variable(B, name);                               \
    if (!ttisnumber(v1)) {                                              \
      eval_error(B, "%s", "left operand of "#action" must be number");  \
    }                                                                   \
    if (!ttisnumber(v2)) {                                              \
      eval_error(B, "%s", "right operand of "#action" must be number"); \
    }                                                                   \
    setnvalue(v1, action(nvalue(v1), nvalue(v2)));                      \
    SCSV(B, name, v1);                                                  \
    free(v2);                                                           \
    PUSH(v1);                                                           \
    LOOP();                                                             \
  } while(0)

  DECODE {
    CASE(BINARY_OP): {
      uint8_t op = READ_BYTE();

      switch(op) {
        case(TK_ADD): { // To recognize += or -=
          TValue * v2 = POP();
          TValue * v1 = POP();
          if (ttisnumber(v1) && ttisnumber(v2)) {
            setnvalue(v1, add(nvalue(v1), nvalue(v2)));
          } else {
            v1= concat(B, tvalue_inspect(B, v1), tvalue_inspect(B, v2));
          }
          PUSH(v1);
          LOOP();
        }
        case(TK_SUB):
          CAL_STMT(sub);
        case(TK_DIV):
          CAL_STMT(div);
        case(TK_MUL):
          CAL_STMT(mul);
        case(TK_SHR):
          CAL_STMT(shr);
        case(TK_SHL):
          CAL_STMT(shl);
        case(TK_MOD):
          CAL_STMT(mod);
        case(TK_LOGIC_AND):
          CAL_STMT(logic_and);
        case(TK_LOGIC_OR):
          CAL_STMT(logic_or);
        case(TK_LOGIC_XOR):
          CAL_STMT(logic_xor);
        case(TK_EQ): {
          TValue * v1 = POP();
          TValue * v2 = POP();
          v1 = check_equal(v1, v2) ? G(B)->tVal : G(B)->fVal;
          PUSH(v1);
          free(v2);
          LOOP();
        }
        case(TK_GE):
          COMPARE_STMT(gte);
        case(TK_GT):
          COMPARE_STMT(gt);
        case(TK_LE):
          COMPARE_STMT(lte);
        case(TK_LT):
          COMPARE_STMT(lt);
        case(TK_AND): {
          TValue * ret = POP();
          if (truthvalue(ret)) ret = POP();
          PUSH(ret);
          LOOP();
        }
        case(TK_OR): {
          TValue * ret = POP();
          if (falsyvalue(ret)) ret = POP();
          PUSH(ret);
          LOOP();
        }
        case(TK_NE): {
          TValue * v1 = POP();
          TValue * v2 = POP();
          v1 = !check_equal(v1, v2) ? G(B)->tVal : G(B)->fVal;
          free(v2);
          LOOP();
        }
      }
    }
    CASE(BINARY_OP_WITH_ASSIGN): {
      uint8_t op = READ_BYTE();

      switch(op) {
        case(TK_ADD): { // To recognize += or -=
          TValue * v2 = POP();
          TValue * name = POP();
          TValue * v1 = find_variable(B, name);
          if (ttisnumber(v1) && ttisnumber(v2)) {
            setnvalue(v1, add(nvalue(v1), nvalue(v2)));
          } else {
            v1= concat(B, tvalue_inspect(B, v1), tvalue_inspect(B, v2));
          }
          free(v2);
          SCSV(B, name, v1);
          PUSH(v1);
          LOOP();
        }
        case(TK_SUB):
          CAL_EQ_STMT(sub);
        case(TK_MUL):
          CAL_EQ_STMT(mul);
        case(TK_DIV):
          CAL_EQ_STMT(div);
        case(TK_LOGIC_OR):
          CAL_EQ_STMT(logic_or);
        case(TK_LOGIC_AND):
          CAL_EQ_STMT(logic_and);
        case(TK_LOGIC_XOR):
          CAL_EQ_STMT(logic_xor);
        case(TK_MOD):
          CAL_EQ_STMT(mod);
      }
    }
    CASE(PUSH_NUM): {
      bean_Number * np = operand_decode(ip);
      TValue * number = TV_MALLOC;
      setnvalue(number, *np);
      PUSH(number);
      ip += COMMON_POINTER_SIZE;
      LOOP();
    }
    CASE(PUSH_TRUE): {
      PUSH(G(B)->tVal);
      LOOP();
    }
    CASE(PUSH_FALSE): {
      PUSH(G(B)->fVal);
      LOOP();
    }
    CASE(PUSH_STR): {
      TValue * string = TV_MALLOC;
      TString * str = operand_decode(ip);
      setsvalue(string, str);
      ip += COMMON_POINTER_SIZE;
      PUSH(string);
      LOOP();
    }
    CASE(PUSH_NIL): {
      PUSH(G(B)->nil);
      LOOP();
    }
    CASE(FUNCTION_DEFINE): {
      TValue * function = TV_MALLOC;
      Function * f = operand_decode(ip);
      setfcvalue(function, f);
      ip += COMMON_POINTER_SIZE;
      PUSH(function);
      LOOP();
    }
    CASE(INSPECT): {
      TValue * value = POP();
      TValue * tvString = tvalue_inspect(B, value);
      char * string = getstr(svalue(tvString));
      printf("%s\n", string);
      LOOP();
    }
    CASE(VARIABLE_GET): {
      TValue * name = POP();
      TValue * value = find_variable(B, name);
      if (!value) eval_error(B, "%s", "Can't reference the variable before defined.");
      PUSH(value);
      free(name);
      LOOP();
    }
    CASE(VARIABLE_DEFINE): {
      TValue * value = POP();
      TValue * name = POP();
      SCSV(B, name, value);
      PUSH(value);
      LOOP();
    }
    CASE(ARRAY_PUSH): {
      TValue * array = TV_MALLOC;
      Array * a = init_array(B);
      setarrvalue(array, a);
      PUSH(array);
      LOOP();
    }
    CASE(ARRAY_ITEM): {
      TValue * item = POP();
      TValue * array = POP();
      Array * a = arrvalue(array);
      array_push(B, a, item);
      PUSH(array);
      LOOP();
    }
    CASE(HASH_NEW): {
      TValue * hash = TV_MALLOC;
      Hash * h = init_hash(B);
      sethashvalue(hash, h);
      PUSH(hash);
      LOOP();
    }
    CASE(HASH_VALUE): {
      TValue * value = POP();
      TValue * key = POP();
      TValue * hash = POP();
      Hash * h = hhvalue(hash);
      hash_set(B, h, key, value);
      PUSH(hash);
      LOOP();
    }
    CASE(HANDLE_PREFIX): {
      uint8_t op = READ_BYTE();

      #define PREFIX(operator) do {                           \
        TValue * name = TV_MALLOC;                      \
        TString * ts = operand_decode(ip);              \
        setsvalue(name, ts);                            \
        TValue * value = find_variable(B, name);        \
        bean_Number i = nvalue(value);                  \
        i = i operator 1;                               \
        setnvalue(value, i);                            \
        PUSH(value);                                    \
        ip += COMMON_POINTER_SIZE;                      \
        LOOP();                                         \
      } while(0);

      switch(op) {
        case(TK_ADD): {
          PREFIX(+);
        }
        case(TK_SUB): {
          PREFIX(-);
        }
      }
      #undef PREFIX
    }
    CASE(HANDLE_SUFFIX): {
      uint8_t op = READ_BYTE();
      #define SUFFIX(operator) do {                           \
        TValue * name = POP();                              \
        TValue * value = find_variable(B, name);        \
        bean_Number i = nvalue(value);                  \
        TValue * ret = TV_MALLOC;                       \
        setnvalue(ret, i);                              \
        i = i operator 1;                               \
        setnvalue(value, i);                            \
        PUSH(ret);                                    \
        LOOP();                                         \
      } while(0);

      switch(op) {
        case(TK_ADD): {
          SUFFIX(+);
        }
        case(TK_SUB): {
          SUFFIX(-);
        }
      }
      #undef SUFFIX
    }
    CASE(UNARY): {
      uint8_t op = READ_BYTE();
      TValue * res = TV_MALLOC;
      TValue * v = POP();

      switch(op) {
        case(TK_SUB): {
          setnvalue(res, -nvalue(v));
          goto clear_unary;
        }
        case(TK_ADD): {
          setnvalue(res, nvalue(v));
          goto clear_unary;
        }
        case(TK_BANG):
        case(TK_NOT): {
          res = truthvalue(v) ?  G(B)->fVal : G(B)->tVal;
          goto clear_unary;
        }
        case(TK_TYPE_OF): {
          res = type_statement(B, v);
          goto clear_unary;
        }
        clear_unary:
        free(v);
        PUSH(res);
        LOOP();
      }
    }
    default: {
      break;
    }
  }
  return 0;
}
