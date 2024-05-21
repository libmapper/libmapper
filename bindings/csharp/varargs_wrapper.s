.global _varargs_wrapper

// varargs_wrapper(void* function_pointer, char *fmt, int len, uint64_t *args)
// used to call functions with variadic arguments on apple silicon
// calls function_pointer, assuming it has a signature of (char *fmt, ...)
// len is the number of variadic arguments,
// args is a pointer to an array of 8-byte arguments
// the return type is transparent; whatever the passed function returns is returned
_varargs_wrapper:
  // save frame pointer and link register
  stp x29, x30, [sp, -16]!
  mov x29, sp // save frame pointer
  stp x19, x20, [sp, -16]!

  // copy len to a local variable
  mov x9, x2

  // if x9 is odd, add one (sp must be 16-byte aligned)
  and x10, x9, #1
  add x9, x9, x10

  // multiply len by 8 to get the number of bytes to allocate on the stack
  lsl x9, x9, #3

  // save len * 8 to x19 (x19 is callee-saved)
  mov x19, x9

  // make room on the stack for arguments
  sub sp, sp, x9
  // initialize loop counter
  mov x10, #0
  // initialize pointer to args
  mov x11, x3

  // loop and push each argument onto the stack
loop:
  // if (loop counter >= len) break;
  cmp x10, x2
  bge end

  // load the next argument
  ldr x12, [x11, x10, lsl 3]
  // store the argument on the stack
  str x12, [sp, x10, lsl 3]

  // increment and loop
  add x10, x10, #1
  b loop

end:
  // move function pointer out of the way
  mov x12, x0
  // move first argument into place
  mov x0, x1
  // call the function
  blr x12

  // restore stack pointer
  add sp, sp, x19

  // restore x19 and x20
  ldp x19, x20, [sp, 0]

  // restore frame pointer and link register
  ldp x29, x30, [sp, 16]

  add sp, sp, #32

  ret