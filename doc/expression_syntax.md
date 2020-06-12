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

* `?:` – if / then / else (ternary operation) used in the form `a?b:c`. If the second operand is omitted (e.g. `a ?: c`) the first will be used in its place.

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
* `min(x,y)` – smaller of two values (overloaded)
* `max(x,y)` – greater of two values (overloaded)
* `schmitt(x,a,b)` – a comparator with hysteresis ([Schmitt trigger](https://en.wikipedia.org/wiki/Schmitt_trigger)) with input `x`, low threshold `a` and high threshold `b`

### Random number generation:
* `uniform(x)` — uniform random distribution between 0 and the given value

### Conversion functions:
* `midiToHz(x)` — convert MIDI note value to Hz
* `hzToMidi(x)` — convert Hz value to MIDI note

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

* `any(x)` — output `1` if **any** of the elements of vector `x` are non-zero, otherwise output `0`
* `all(x)` — output `1` if **all** of the elements of vector `x` are non-zero, otherwise output `0`
* `sum(x)` – output the sum of the elements in vector `x`
* `mean(x)` – output the average (mean) of the elements in vector `x`
* `max(x)` – output the maximum element in vector `x` (overloaded)
* `min(x)` – output the minimum element in vector `x` (overloaded)

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

* `ema = ema{-1} * 0.9 + x * 0.1;``y = x - ema`

Just like the output variable `y` we can initialize past values of user-defined variables before expression evaluation. **Initialization will always be performed first**, after which sub-expressions are evaluated **in the order they are written**. For example, the expression string `y=ema*2; ema=ema{-1}*0.9+x*0.1; ema{-1}=90` will be evaluated in the following order:

1. `ema{-1}=90` — initialize the past value of variable `ema` to `90`
2. `y=ema*2` — set output variable `y` to equal the **current** value of `ema` multiplied
by `2`. The current value of `ema` is `0` since it has not yet been set.
3. `ema=ema{-1}*0.9+x*0.1` — set the current value of `ema` using current value of `x` and the past value of `ema`.

## Instance Management

Signal instancing can also be managed from within the map expression by manipulating a special variable named `alive` that represents the instance state. The use cases for in-map instancing can be complex, but here are some simple examples:

                     | Singleton Destination | Instanced Destination
---------------------|-----------------------|-----------------------
**Singleton Source** | conditional output    | conditional serial instancing
**Instanced Source** | conditional output    | modified instancing

### Conditional output

In the case of a map with a singleton (non-instanced) destination, in-map
instance management can be used for conditional updates. For example,
imagine we want to map `x -> y` but only propagate updates when `x > 10` – we could use the expression:

* `alive = x > 10;``y = x;`

Since in this case the destination signal is not instanced it will not be "released" when `alive` evaluates to False, however any assignments to the output `y` while `alive` is False will not take effect. The statement `alive = x > 10` is evaluated first, and the update `y = x` is only propagated to the destination if `x > 10` evaluates to True (non-zero) **at the time of assignment**. The entire expression is evaluated however, so counters can be incremented etc. even while `alive` is False. There is a more complex example in the section below on Accessing Variable Timetags.

### Conditional serial instancing

When mapping a singleton source signal to an instanced destination signal there are several possible desired behaviours:

1. The source signal controls **one** of the available destination signal instances. The destination instance is activated upon receiving the first update and a release event is triggered when the map is destroyed so the lifetime of the map controls the lifetime of the destination signal instance. This configuration is the default for maps from singleton->instanced signals, and is achieved by setting the map property `use_inst` to True.
2. The source signal controls **all** of the available **active** destination signal instances **in parallel**. This is accomplished by setting the `use_inst` property of the map to False (0). Note that in this case a source update will not activate new instances, so this configuration should probably only be used with destination signals that manage their own instances or that are persistent (non-ephemeral).
    * Example 1: a destination signal named *polyPressure* belongs to a software shim device for interfacing with MIDI. The singleton signal *mouse/position/x* is mapped to *polyPressure*, and the map's `use_inst` property is set to False to enable controlling the poly pressure parameter of all active notes in parallel.
3. The source signal controls available destination signal instances **serially**. This is accomplished by manipulating the `alive` variable as described above. On each rising edge (transition from 0 to non-zero) of the `alive` variable a new instance id map will be generated

### Modified instancing

*currently undocumented*

## Propagation Management

If desired, the expression can be evaluated "silently" so that updates do not propagate to the destination. This is accomplished by manipulating a special variable named `muted`. For maps with singleton destination signals this has an identical effect to manipulating the `alive` variable, but for instanced destinations it enables filtering updates without releasing the associated instance.

The example below implements a "change" filter in which only updates with different input values are sent to the destination:

* `muted=(x==x{-1});``y=x;`

Note that (as above) the value of the `muted` variable must be true (non-zero) **when y is assigned** in order to mute the update; the arbitrary example below will instead mute the next update following the condition `(x==x{-1})`:

* `y=x;``muted=(x==x{-1});`

## Accessing Variable Timetags

The precise time at which a variable is updated is always tracked by libmapper and communicated with the data value. In the future we plan to use this information in the background for discarding out-of-order packets and jitter mitigation, but it may also be useful in your expressions.

The timetag associated with a variable can be accessed using the syntax `t_<variable_name>` – for example the time associated with the current sample `x` is `t_x`, and the timetag associated with the last update of a hypothetical user-defined variable `foo` would be `t_foo`. This syntax can be used anywhere in your expressions:

* `y=t_x` — output the timetag of the input instead of its value
* `y=t_x-t_x{-1}` — output the time interval between subsequent updates

This functionality can be used along with in-map signal instancing to limit the output rate:

* `alive=(t_x-t_y{-1})>0.5;``y=x` — only output if more than 0.5 seconds has elapsed since the last output, otherwise discard input sample.

Also we can calculate a moving average of the sample period:

* `y=y{-1}*0.9+(t_x-t_y{-1})*0.1`

Of course the first value for `(t_x-t_y{-1})` will be very large since the first value for `t_y{-1}` will be `0`. We can easily fix this by initializing the first value for `t_y{-1}` – remember from above that this part of the expression will only be called once so it will not adversely affect the efficiency of out expression:

* `t_y{-1}=t_x;` `y=y{-1}*0.9+(t_x-t_y{-1})*0.1;`

Here's a more complex example with 4 sub-expressions in which the rate is limited but incoming samples are averaged instead of discarding them:

* `alive=(t_x-t_y{-1})>0.1;` `y=B/C;` `B=!alive*B+x;` `C=alive?1:C+1;`

Explanation:

order | step           | expression | description
----- | -------------- | ---------- | -----------
1 | check elapsed time | `alive=(t_x-t_y{-1})>0.1;` | Set `alive` to `1` (true) if more than `0.1` seconds have elapsed since the last output; or `0` otherwise.
2 | conditional output | `y=B/C;` | Output the average `B/C` (if `alive` is true)
3 | update accumulator | `B=!alive*B+x;` | reset accumulator `B` to 0 if `alive` is true, add `x`
4 | update count       | `C=alive?1:C+1;` | increment `C`, reset if `alive` is true
