libmapper Expression Syntax
===========================

Connections between signals that are maintained by libmapper
can be configured with optional signal processing in the
form of an expression.

General Syntax
--------------

Expressions in libmapper must always be presented in the form
`y = x`, where `x` refers to the updated source value and `y` is
the computed value to be forwarded to the destination.

Available Functions
-------------------

### Absolute value:

* `y = abs(x)` — absolute value

### Exponential functions:
* `y = exp(x)` — returns e raised to the given power
* `y = exp2(x)` — returns 2 raised to the given power
* `y = log(x)` — computes natural ( base e ) logarithm ( to base e )
* `y = log10(x)` — computes common ( base 10 ) logarithm
* `y = log2(x)` – computes the binary ( base 2 ) logarithm
* `y = logb(x)` — extracts exponent of the number

### Power functions:
* `y = sqrt(x)` — square root
* `y = cbrt(x)` — cubic root
* `y = hypot(x, n)` — square root of the sum of the squares of two given numbers
* `y = pow(x, n)` — raise a to the power b

### Trigonometric functions:
* `y = sin(x)` — sine
* `y = cos(x)` — cosine
* `y = tan(x)` — tangent
* `y = asin(x)` — arc sine
* `y = acos(x)` — arc cosine
* `y = atan(x)` — arc tangent
* `y = atan2(x, n)` — arc tangent, using signs to determine quadrants

### Hyperbolic functions:
* `y = sinh(x)` — hyperbolic sine
* `y = cosh(x)` — hyperbolic cosine
* `y = tanh(x)` — hyperbolic tangent

### Nearest integer floating point comparisons:
* `y = floor(x)` — nearest integer not greater than the given value
* `y = round(x)` — nearest integer, rounding away from zero in halfway cases
* `y = ceil(x)` — nearest integer not less than the given value
* `y = trunc(x)` — nearest integer not greater in magnitude than the given value

### Comparison functions:
* `y = min(x, n)` – smaller of two values
* `y = max(x, n)` – greater of two values

### Random number generation:
* `y = uniform(x)` — uniform random distribution between 0 and the given value

### Conversion functions:
* `y = midiToHz(x)` — convert MIDI note value to Hz
* `y = hzToMidi(x)` — convert Hz value to MIDI note


FIR and IIR Filters
===================

Past samples of expression input and output can be accessed using the notation
`<variable>{<index>}`. The index specifies the amount of delay in samples, and
obviously must be `<=0` for the input and `<0` for the output ( i.e. it cannot
be a value that has not been provided or computed yet ).

Using only delayed samples of the expression *input* `x` we can create **Finite
Impulse Response** ( FIR ) filters - here are some simple examples:

* `y = x - x{-1}` — 2-sample derivative
* `y = x + x{-1}` — 2-sample integral

Using delayed samples of the expression *output* `y` we can create **Infinite
Impulse Response** ( IIR ) filters - here are some simple examples:

* `y = y{-1} * 0.9 + x * 0.1` — exponential moving average with current-sample-weight of `0.1`
* `y = y{-1} + x - 1` — leaky integrator with a constant leak of `1`

Of course the filter can contain references to past samples of both `x` and `y` -
currently libmapper will reject expressions referring to sample delays `> 100`.