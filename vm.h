#ifndef _BEAN_VM_H
#define _BEAN_VM_H

typedef enum {
  OP_BEAN_ADD=2,
  OP_BEAN_PUSH_FALSE,
  OP_BEAN_PUSH_TRUE,
  OP_BEAN_SUB,
  OP_BEAN_MUL,
  OP_BEAN_DIV,
  OP_BEAN_VARIABLE_GET,
  OP_BEAN_VARIABLE_DEFINE,
  OP_BEAN_FUNCTION_DEFINE,
  OP_BEAN_FUNCTION_CALL,
} OpCode;

int executeInstruct();

#endif
