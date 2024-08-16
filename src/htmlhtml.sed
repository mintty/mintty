/<\/style/ i\
       .h3 {font-size: 13pt;}\
       .h4 {}

/<h1/ a\
Sections:\
<div style="column-count: 4;">

/<hr/ i\
</div>

s,<b>,<b class=h3>,
/^<h2>USAGE/,$ s,<b>,<br 1><b class=h4>,
t
/^<h2>USAGE/,$ s,,<br 2>,
t
