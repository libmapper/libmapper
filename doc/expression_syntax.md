libmapper Expression Syntax
===========================

* y=abs(x) — absolute value

### Exponential functions:
* y=exp(x) — returns e raised to the given power
* y=exp2(x) — returns 2 raised to the given power
* y=log(x) — computes natural (base e) logarithm (to base e)
* y=log10(x) — computes common (base 10) logarithm
* y=logb(x) — extracts exponent of the number
* y=log2(x)

### Power functions:
* y=sqrt(x) — square root
* y=cbrt(x) — cubic root
* y=hypot(x,n) — square root of the sum of the squares of two given numbers
* y=pow(x,n) — raise a to the power b

### Trigonometric functions:
* y=sin(x) — sine
* y=cos(x) — cosine
* y=tan(x) — tangent
* y=asin(x) — arc sine
* y=acos(x) — arc cosine
* y=atan(x) — arc tangent
* y=atan2(x, n) — arc tangent, using signs to determine quadrants

### Hyperbolic functions:
* y=sinh(x) — hyperbolic sine
* y=cosh(x) — hyperbolic cosine
* y=tanh(x) — hyperbolic tangent

### Nearest integer floating point comparisons:
* y=floor(x) — nearest integer not greater than the given value
* y=round(x) — nearest integer, rounding away from zero in halfway cases
* y=ceil(x) — nearest integer not less than the given value
* y=trunc(x) — nearest integer not greater in magnitude than the given value

### Comparison functions:
* y=min(x,n) – smaller of two values
* y=max(x,n) – greater of two values

### Conversion functions:
* y=midiToHz(x) — convert MIDI note value to Hz
* y=hzToMidi(x) — convert Hz value to MIDI note
* y=uniform(x) — uniform random distribution between 0 and the given value

### Examples using past samples:
* y=x-x{-1} — 2-sample derivative
* y=x+x{-1} — 2-sample integral
* y=y{-1}*0.9+x*0.1 — exponential moving average with weight 0.1
* y=y{-1}+x-1 — leaky integrator with a constant leak
