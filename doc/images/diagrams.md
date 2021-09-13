# Diagrams

This document includes the code used to generate diagrams for libmapper documentation.

## Device lifecycle

This diagram is used in the tutorials for various language bindings.

<div><pre><code class="language-dot">digraph {
    rankdir="LR";
    node [style=rounded];
    creation [shape=box];
    poll [shape=box];
    destruction [shape=box];
    creation:e -> poll:w;
    poll:e -> destruction:w;
    poll -> poll [dir=back];
}</code></pre></div>

## Convergent Maps

These diagrams are used in the [expression documentation](../expression_syntax.md)

### interleaved updates (naïve convergent maps)

<div><pre><code class="language-dot">digraph naive {
    A [fillcolor="#FF000088", style=filled];
    B [fillcolor="#FFFF0088", style=filled];
    C [fillcolor="#0000FF66", style=filled];
    A -> C:nw [xlabel="y=x  "];
    B -> C:ne [xlabel="y=x"];
}</code></pre></div>

### partial vector updates

<div><pre><code class="language-dot">digraph vector {
    A [fillcolor="#FF000088", style=filled];
    B [fillcolor="#FFFF0088", style=filled];
    C [fillcolor="#0000FF66", style=filled];
    A -> C:nw [xlabel="y[0]=x  "];
    B -> C:ne [xlabel="y[1]=x"];
}</code></pre></div>

### shared instance pools

<div><pre><code class="language-dot">digraph pooled {
    A [fillcolor="#FF000088", style=filled];
    B [fillcolor="#FFFF0088", style=filled];
    C [fillcolor="#0000FF66", style=filled];
    A -> C:nw [xlabel="y=x  ", color="black:black"];
    B -> C:ne [xlabel="y=x", color="black:black"];
}</code></pre></div>


### destination value references

<div><pre><code class="language-dot">digraph iir {
    A [fillcolor="#FF000088", style=filled];
    B [fillcolor="#FFFF0088", style=filled];
    C [fillcolor="#0000FF66", style=filled];
    A -> C:nw [headlabel="y=\ny{-1}+x     "];
    B -> C:ne [headlabel="y=\n     y{-1}-x"];
}</code></pre></div>

### convergent maps

<div><pre><code class="language-dot">digraph convergent {
    A [fillcolor="#FF000088", style=filled];
    B [fillcolor="#FFFF0088", style=filled];
    C [fillcolor="#0000FF66", style=filled];
    map [shape=point];
    A:se -> map [arrowhead=none];
    B:sw -> map [arrowhead=none];
    map -> C [xlabel="   y=x$0+x$1\n "];
}</code></pre></div>

