# Expression Syntax for *libmpr*

Connections between signals that are maintained by *libmpr* can be configured with
optional signal processing described in the form of an expression.

## General Syntax

Expressions in libmpr must always be presented in the form `y = x`, where `x`
refers to the updated source value and `y` is the computed value to be forwarded
to the destination. Sub-expressions can be used if separated by a semicolon (`;`). Spaces may be freely used within the expression, they will have no effect on the
generated output.

## Available operators

### Arithmetic operators

* `+` – addition
* `-` – subtraction
* `*` – multiplication
* `/` – division
* `%` – modulo

### Comparison operators

* `>` – greater than
* `>=` – greater than or equal
* `<` – less than
* `<=` – less than or equal
* `==` – equal
* `!=` – not equal


### Conditional operators

* `?:` – if / then / else (ternary operation) used in the form `a?b:c`. If the second operand is not included the first will be used in its place. If the last operand is missing (the "else" argument) the function will not generate an output if `a` is false

### Logical operators

* `!` – logical **NOT**
* `&&` – logical **AND**
* `||` – logical **OR**

### Bitwise operators

* `<<` – left bitshift
* `>>` – right bitshift
* `&` – bitwise **AND**
* `|` – bitwise **OR**
* `^` – bitwise **XOR**

## Function List

### Absolute value:

* `abs(x)` — absolute value

### Exponential functions:
* `exp(x)` — returns e raised to the given power
* `exp2(x)` — returns 2 raised to the given power
* `log(x)` — computes natural ( base e ) logarithm ( to base e )
* `log10(x)` — computes common ( base 10 ) logarithm
* `log2(x)` – computes the binary ( base 2 ) logarithm
* `logb(x)` — extracts exponent of the number

### Power functions:
* `sqrt(x)` — square root
* `cbrt(x)` — cubic root
* `hypot(x, n)` — square root of the sum of the squares of two given numbers
* `pow(x, n)` — raise a to the power b

### Trigonometric functions:
* `sin(x)` — sine
* `cos(x)` — cosine
* `tan(x)` — tangent
* `asin(x)` — arc sine
* `acos(x)` — arc cosine
* `atan(x)` — arc tangent
* `atan2(x, n)` — arc tangent, using signs to determine quadrants

### Hyperbolic functions:
* `sinh(x)` — hyperbolic sine
* `cosh(x)` — hyperbolic cosine
* `tanh(x)` — hyperbolic tangent

### Nearest integer floating point:
* `floor(x)` — nearest integer not greater than the given value
* `round(x)` — nearest integer, rounding away from zero in halfway cases
* `ceil(x)` — nearest integer not less than the given value
* `trunc(x)` — nearest integer not greater in magnitude than the given value

### Comparison functions:
* `min(x,y)` – smaller of two values
* `max(x,y)` – greater of two values
* `schmitt(x,a,b)` – a comparator with hysteresis ([Schmitt trigger](https://en.wikipedia.org/wiki/Schmitt_trigger)) with input `x`, low threshold `a` and high threshold `b`

### Random number generation:
* `uniform(x)` — uniform random distribution between 0 and the given value

### Conversion functions:
* `midiToHz(x)` — convert MIDI note value to Hz
* `hzToMidi(x)` — convert Hz value to MIDI note

### Vector functions
* `any(x)` — output `1` if **any** of the elements of vector `x` are non-zero, otherwise output `0`
* `all(x)` — output `1` if **all** of the elements of vector `x` are non-zero, otherwise output `0`
* `sum(x)` – output the sum of the elements in vector `x`
* `mean(x)` – output the average (mean) of the elements in vector `x`
* `max(x)` – output the maximum element in vector `x` (overloaded function)
* `min(x)` – output the minimum element in vector `x` (overloaded function)

### Filters
* `ema(x,w)` – a cheap low-pass filter: calculate a running *exponential moving average* with input `x` and a weight `w` applied to the current sample.

## Vectors

Individual elements of variable values can be accessed using the notation
`<variable>[<index>]`. The index specifies the vector element, and
obviously must be `>=0`. Expressions must match the vector lengths of the
source and destination signals, and can be used to translate between
signals with different vector lengths.

### Vector examples

* `y = x[0]` — simple vector indexing
* `y = x[1:2]` — specify a range within the vector
* `y = [x[1],x[2],x[0]]` — rearranging vector elements
* `y[1] = x` — apply update to a specific element of the output
* `y[0:2] = x` — apply update to elements `0-2` of the output vector
* `[y[0],y[2]] = x` — apply update to output vector elements `y[0]` and `y[2]` but
leave `y[1]` unchanged.

### Vector functions

There are several special functions that operate across all elements of the vector:

* `any(x)` — output `1` if **any** of the elements of `x` are non-zero, otherwise output `0`
* `all(x)` — output `1` if **all** of the elements of `x` are non-zero, otherwise output `0`
* `max(x)` — output the largest element of the vector
* `min(x)` — output the smallest element of the vector

## FIR and IIR Filters

Past samples of expression input and output can be accessed using the notation
`<variable>{<index>}`. The index specifies the history index in samples, and must be `<=0` for the input (with `0` representing the present input sample) and `<0` for the expression output ( i.e. it cannot be a value that has not been provided or computed yet ).

Using only past samples of the expression *input* `x` we can create **Finite
Impulse Response** ( FIR ) filters - here are some simple examples:

* `y = x - x{-1}` — 2-sample derivative
* `y = x + x{-1}` — 2-sample integral

Using past samples of the expression *output* `y` we can create **Infinite
Impulse Response** ( IIR ) filters - here are some simple examples:

* `y = y{-1} * 0.9 + x * 0.1` — exponential moving average with current-sample-weight of `0.1`
* `y = y{-1} + x - 1` — leaky integrator with a constant leak of `1`

Note that `y{-n}` does not refer to the expression output, but rather to the *actual
value* of the destination signal which may have been set locally or by another map
since the last time the expression was evaluated. If you wish to reference past samples
of the expression output you will need to cache the output using a **user-defined
variable** (see below), e.g.:

* `output = output + x - 1`;`y = output`

Of course the filter can contain references to past samples of both `x` and `y` -
currently libmpr will reject expressions referring to sample delays `> 100`.

### Initializing filters

Past values of the filter output `y{-n}` can be set using additional sub-expressions, separated using semicolons:

* `y = y{-1} + x`; `y{-1} = 100`

Filter initialization takes place the first time the expression evaluator is called
for a given signal instance; after this point the initialization sub-expressions will
not be evaluated. This means the filter could be initialized with the first sample of
`x` for example:

* `y = y{-1} + x`; `y{-1} = x * 2`

A function could also be used for initialization:

* `y = y{-1} + x`; `y{-1} = uniform(1000)` — initialize `y{-1}` to a random value

Any past values that are not explicitly initialized are given the value `0`.


## User-Declared Variables

Up to 8 additional variables can be declared as-needed in the expression. The variable
names can be any string except for the reserved variable names `x` and `y`.  The values
of these variables are stored per-instance with the map context and can be accessed in
subsequent calls to the evaluator. In the following example, the user-defined variable
`ema` is used to keep track of the `exponential moving average` of the input signal
value `x`, *independent* of the output value `y` which is set to give the difference
between the current sample and the moving average:

* `ema = ema{-1} * 0.9 + x * 0.1; y = x - ema`

Just like the output variable `y` we can initialize past values of user-defined variables before expression evaluation. **Initialization will always be performed first**, after which sub-expressions are evaluated **in the order they are written**. For example, the expression string `y=ema*2; ema=ema{-1}*0.9+x*0.1; ema{-1}=90` will be evaluated in the following order:

1. `ema{-1}=90` — initialize the past value of variable `ema` to `90`
2. `y=ema*2` — set output variable `y` to equal the **current** value of `ema` multiplied
by `2`. The current value of `ema` is `0` since it has not yet been set.
3. `ema=ema{-1}*0.9+x*0.1` — set the current value of `ema` using current value of `x` and the past value of `ema`

## Accessing Variable Timetags

The precise time at which a variable is updated is always tracked by libmapper and communicated with the data value. We plan to use this information in the background for discarding out-of-order packets and jitter mitigation, but it may also be useful in your expressions.

The timetag associated with a variable can be accessed using the syntax `t_<variable_name>` – for example the time associated with the current sample `x` is `t_x`, and the timetag associated with the last update of a hypothetical user-defined variable `foo` would be `t_foo`. This syntax can be used anywhere in your expressions:

* `y=t_x` — output the timetag of the input instead of its value
* `y=t_x-t_x{-1}` — output the time interval between subsequent updates

This functionality can be used to limit the output rate:

* `y=(t_x-t_y{-1})>0.5?x` — only output if more than 0.5 seconds has elapsed since the last output, otherwise discard input sample.

Also we can calculate a moving average of the sample period:

* `y=y{-1}*0.9+(t_x-t_y{-1})*0.1`

Of course this moving average will start with a very large first value for `(t_x-t_y{-1})` since the first value for `t_y{-1}` will be `0`. We can easily fix this by initializing the first value for `t_y{-1}` – remember from above that this part of the expression will only be called once so it will not adversely affect the efficiency of out expression:

* `t_y{-1}=t_x;` `y=y{-1}*0.9+(t_x-t_y{-1})*0.1;`

Here's a more complex example with 4 sub-expressions in which the rate is limited but incoming samples are averaged instead of discarding them:

* `A=(t_x-t_y{-1})>0.1;` `y=A?B/C;` `B=!A*B+x;` `C=A?1:C+1;`

Explanation:

order | step   | expression | description
----- | ------ | ------ | ------
1 | check elapsed time | `A=(t_x-t_y{-1})>0.1;` | Set `A` to `1` (true) if more than `0.1` seconds have elapsed since the last output; or `0` otherwise.
2 | conditional output | `y=A?B/C;` | Output `B/C` (the average) if `A` is true; otherwise do nothing
3 | update accumulator | `B=!A*B+x;` | reset accumulator `B` to 0 if `A` is true, add `x`
4 | update count | `C=A?1:C+1;` | increment `C`, reset if `A` is true
